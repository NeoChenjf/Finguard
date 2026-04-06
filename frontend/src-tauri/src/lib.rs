use std::sync::{Arc, Mutex, atomic::{AtomicBool, Ordering}};
use std::path::{Path, PathBuf};
use tauri::{
    menu::{Menu, MenuItem},
    tray::{TrayIconBuilder, TrayIconEvent, MouseButton, MouseButtonState},
    AppHandle, Emitter, Manager,
};
use tauri_plugin_shell::{process::CommandChild, ShellExt};

/// 判断当前是否在 tauri build 生产模式下运行
/// 生产包中 exe 旁不会有 `Cargo.toml`（dev 模式的 target/debug 里也没有，
/// 所以改为检测 `<exe_dir>/resources` 或 Tauri 资源路径是否存在打包标志）。
/// 简单方案：检查 exe 路径中是否包含 "target"（dev 模式的标志）。
fn is_production() -> bool {
    let exe = std::env::current_exe().unwrap_or_default();
    let exe_str = exe.to_string_lossy();
    // tauri dev 时 exe 在 target/debug/ 或 target/release/ 下
    !exe_str.contains("target")
}

/// 获取 AppData 下的 finguard 工作目录 (%APPDATA%/finguard/)
fn appdata_work_dir() -> PathBuf {
    // %APPDATA% 通常为 C:\Users\<user>\AppData\Roaming
    let appdata = std::env::var("APPDATA")
        .unwrap_or_else(|_| {
            let home = std::env::var("USERPROFILE").unwrap_or_else(|_| ".".to_string());
            format!("{home}\\AppData\\Roaming")
        });
    PathBuf::from(appdata).join("finguard")
}

/// 递归复制目录
fn copy_dir_recursive(src: &Path, dst: &Path) -> std::io::Result<()> {
    if !dst.exists() {
        std::fs::create_dir_all(dst)?;
    }

    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());

        if src_path.is_dir() {
            copy_dir_recursive(&src_path, &dst_path)?;
        } else {
            std::fs::copy(&src_path, &dst_path)?;
        }
    }
    Ok(())
}

/// 首次启动时，将安装目录的 config/ 和 data/ 复制到 AppData 工作目录
fn ensure_resources_in_appdata(install_dir: &Path) {
    let appdata_dir = appdata_work_dir();

    // 1. 复制 config/ 目录
    let src_config = install_dir.join("config");
    let dst_config = appdata_dir.join("config");

    if dst_config.join("llm.json").exists() {
        // 已存在，跳过复制（用户可能已修改过配置）
        println!("[config] AppData config already exists, skipping copy");
    } else {
        // 创建目标目录
        if let Err(e) = std::fs::create_dir_all(&dst_config) {
            println!("[config] failed to create {}: {e}", dst_config.display());
        } else {
            // 复制每个配置文件
            if src_config.is_dir() {
                if let Ok(entries) = std::fs::read_dir(&src_config) {
                    for entry in entries.flatten() {
                        let src_file = entry.path();
                        if src_file.is_file() {
                            let dst_file = dst_config.join(entry.file_name());
                            match std::fs::copy(&src_file, &dst_file) {
                                Ok(_) => println!("[config] copied {} → {}", src_file.display(), dst_file.display()),
                                Err(e) => println!("[config] failed to copy {}: {e}", src_file.display()),
                            }
                        }
                    }
                }
            } else {
                println!("[config] source config not found: {}", src_config.display());
            }
        }
    }

    // 2. 复制 data/fundamentals.db（主数据库）
    let src_main_db = install_dir.join("data").join("fundamentals.db");
    let dst_main_db = appdata_dir.join("data").join("fundamentals.db");

    if dst_main_db.exists() {
        println!("[data] main database already exists, skipping copy (preserving user data)");
    } else if src_main_db.exists() {
        if let Err(e) = std::fs::create_dir_all(dst_main_db.parent().unwrap()) {
            println!("[data] failed to create data directory: {e}");
        } else {
            match std::fs::copy(&src_main_db, &dst_main_db) {
                Ok(_) => println!("[data] copied main database {} → {}", src_main_db.display(), dst_main_db.display()),
                Err(e) => println!("[data] failed to copy main database: {e}"),
            }
        }
    } else {
        println!("[data] source main database not found: {}", src_main_db.display());
    }

    // 3. 复制 data/examples/ 目录（demo 数据库）
    let src_examples = install_dir.join("data").join("examples");
    let dst_examples = appdata_dir.join("data").join("examples");

    if dst_examples.exists() {
        println!("[data] examples directory already exists, skipping copy");
    } else if src_examples.exists() {
        match copy_dir_recursive(&src_examples, &dst_examples) {
            Ok(_) => println!("[data] copied examples directory {} → {}", src_examples.display(), dst_examples.display()),
            Err(e) => println!("[data] failed to copy examples directory: {e}"),
        }
    } else {
        println!("[data] source examples directory not found: {}", src_examples.display());
    }
}

/// 获取 sidecar exe 所在目录（作为 cwd，使 finguard 能找到 config/ 等相对路径资源）
///
/// - **开发模式** (tauri dev)：cwd = exe 同目录（target/debug/），config/ 由 build.rs 复制到此
/// - **生产模式** (tauri build)：cwd = %APPDATA%/finguard/，首次启动自动从安装目录复制 config/
fn sidecar_dir(app: &AppHandle) -> std::path::PathBuf {
    let exe = std::env::current_exe().unwrap_or_default();
    let exe_dir = exe.parent().unwrap_or(std::path::Path::new(".")).to_path_buf();

    if is_production() {
        // 生产模式：首次复制 config 和 data → AppData，返回 AppData 路径
        ensure_resources_in_appdata(&exe_dir);
        let work_dir = appdata_work_dir();
        println!("[sidecar] production mode, cwd = {}", work_dir.display());
        let _ = app;
        work_dir
    } else {
        // 开发模式：保持原有行为
        println!("[sidecar] dev mode, cwd = {}", exe_dir.display());
        let _ = app;
        exe_dir
    }
}

/// 保存 sidecar 子进程句柄，供后续 kill 使用
pub struct SidecarState {
    pub child: Mutex<Option<CommandChild>>,
    /// 标记是否为主动退出（防止触发自动重启）
    pub shutting_down: Arc<AtomicBool>,
}

/// 终止 sidecar 进程
fn kill_sidecar(app: &AppHandle) {
    let state = app.state::<SidecarState>();
    state.shutting_down.store(true, Ordering::SeqCst);
    if let Ok(mut guard) = state.child.lock() {
        if let Some(child) = guard.take() {
            let _ = child.kill();
        }
    };
}

/// 启动 sidecar，并在意外退出时自动重启（最多 3 次）
fn spawn_sidecar(app: &AppHandle, restart_count: u32) {
    let handle = app.clone();
    let shutting_down = app.state::<SidecarState>().shutting_down.clone();

    match handle.shell().sidecar("finguard") {
        Err(e) => {
            println!("[sidecar] sidecar() error: {e}");
            app.emit("sidecar-status", format!("error: {e}")).ok();
        }
        Ok(cmd) => {
            // 设置工作目录为 exe 所在目录，使 finguard 能找到 config/llm.json
            let cwd = sidecar_dir(&handle);
            let cmd = cmd.current_dir(cwd);
            println!("[sidecar] cmd created, spawning...");
            match cmd.spawn() {
                Err(e) => {
                    println!("[sidecar] spawn error: {e}");
                    handle.emit("sidecar-status", format!("error: {e}")).ok();
                }
                Ok((mut rx, child)) => {
                    println!("[sidecar] spawned OK");
                    *app.state::<SidecarState>().child.lock().unwrap() = Some(child);
                    handle.emit("sidecar-status", "starting").ok();

                    tauri::async_runtime::spawn(async move {
                        use tauri_plugin_shell::process::CommandEvent;
                        while let Some(event) = rx.recv().await {
                            if let CommandEvent::Terminated(status) = event {
                                println!("[sidecar] terminated: {status:?}");
                                if shutting_down.load(Ordering::SeqCst) {
                                    break;
                                }
                                handle.emit("sidecar-status", "stopped").ok();
                                if restart_count < 3 {
                                    tokio::time::sleep(std::time::Duration::from_secs(2)).await;
                                    spawn_sidecar(&handle, restart_count + 1);
                                } else {
                                    handle.emit("sidecar-status", "error: max restarts reached").ok();
                                }
                                break;
                            }
                        }
                    });
                }
            }
        }
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let app = tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_store::Builder::new().build())
        .manage(SidecarState {
            child: Mutex::new(None),
            shutting_down: Arc::new(AtomicBool::new(false)),
        })
        .setup(|app| {
            // ── 启动 sidecar ──
            spawn_sidecar(app.handle(), 0);

            // ── 系统托盘 ──
            let show_item = MenuItem::with_id(app, "show", "显示窗口", true, None::<&str>)?;
            let quit_item = MenuItem::with_id(app, "quit", "退出 FinGuard", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&show_item, &quit_item])?;

            let _tray = TrayIconBuilder::new()
                .icon(app.default_window_icon().unwrap().clone())
                .menu(&menu)
                .tooltip("FinGuard — AI 资产配置中台")
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "show" => {
                        if let Some(win) = app.get_webview_window("main") {
                            let _ = win.show();
                            let _ = win.set_focus();
                        }
                    }
                    "quit" => {
                        kill_sidecar(app);
                        app.exit(0);
                    }
                    _ => {}
                })
                .on_tray_icon_event(|tray, event| {
                    if let TrayIconEvent::Click {
                        button: MouseButton::Left,
                        button_state: MouseButtonState::Up,
                        ..
                    } = event
                    {
                        let app = tray.app_handle();
                        if let Some(win) = app.get_webview_window("main") {
                            let _ = win.show();
                            let _ = win.set_focus();
                        }
                    }
                })
                .build(app)?;

            Ok(())
        })
        // 关闭按钮 → kill sidecar → 真正退出
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { .. } = event {
                // 先终止 sidecar，再让窗口正常关闭
                kill_sidecar(window.app_handle());
            }
        })
        .build(tauri::generate_context!())
        .expect("error while building tauri application");

    // 注册 RunEvent::Exit 兜底：确保进程退出前 sidecar 一定被清理
    app.run(|app_handle, event| {
        if let tauri::RunEvent::Exit = event {
            kill_sidecar(app_handle);
        }
    });
}
