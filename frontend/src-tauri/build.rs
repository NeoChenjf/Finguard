use std::path::PathBuf;

/// 将 finguard sidecar 需要的 DLL 从 finguard/build/Release 复制到 Cargo 输出目录，
/// 使 `tauri dev` 和 `tauri build` 都能找到它们。
fn copy_sidecar_dlls() {
    // sidecar DLL 源目录（C++ Release 构建产物）
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let dll_src = manifest
        .parent()       // frontend/
        .unwrap()
        .parent()       // AI_Investment/
        .unwrap()
        .join("finguard")
        .join("build")
        .join("Release");

    // Cargo 输出目录 —— 与最终可执行文件同级
    // OUT_DIR 形如 target/debug/build/<crate>/out，向上取 3 级到 target/debug
    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let target_dir = out_dir
        .ancestors()
        .nth(3)
        .expect("cannot resolve target dir from OUT_DIR");
    let binaries_dir = manifest.join("binaries");

    std::fs::create_dir_all(&binaries_dir).expect("failed to create binaries dir");

    let entries = std::fs::read_dir(&dll_src).unwrap_or_else(|e| {
        panic!("failed to read sidecar DLL dir {}: {e}", dll_src.display())
    });
    for entry in entries.flatten() {
        let src = entry.path();
        let is_dll = src
            .extension()
            .and_then(|ext| ext.to_str())
            .map(|ext| ext.eq_ignore_ascii_case("dll"))
            .unwrap_or(false);
        if !is_dll {
            continue;
        }
        let file_name = entry.file_name();
        let dll_name = file_name.to_string_lossy();

        let target_dst = target_dir.join(&file_name);
        std::fs::copy(&src, &target_dst).unwrap_or_else(|e| {
            panic!("failed to copy {} to {}: {e}", src.display(), target_dst.display())
        });

        let binary_dst = binaries_dir.join(&file_name);
        std::fs::copy(&src, &binary_dst).unwrap_or_else(|e| {
            panic!("failed to copy {} to {}: {e}", src.display(), binary_dst.display())
        });

        println!("cargo:warning=synced sidecar DLL: {dll_name}");
    }

    // 仅在源 DLL 目录变化时重跑
    println!("cargo:rerun-if-changed={}", dll_src.display());

    // ── 复制 config/ 目录（finguard 使用 cwd/config/llm.json） ──
    let config_src = manifest
        .parent().unwrap()
        .parent().unwrap()
        .join("finguard")
        .join("config");
    let config_dst = target_dir.join("config");
    if config_src.is_dir() {
        std::fs::create_dir_all(&config_dst).ok();
        for entry in std::fs::read_dir(&config_src).unwrap() {
            let entry = entry.unwrap();
            let src_file = entry.path();
            if src_file.is_file() {
                let dst_file = config_dst.join(entry.file_name());
                std::fs::copy(&src_file, &dst_file).unwrap_or_else(|e| {
                    panic!("failed to copy config {}: {e}", src_file.display())
                });
                println!("cargo:warning=copied config: {}", entry.file_name().to_string_lossy());
            }
        }
    }
    println!("cargo:rerun-if-changed={}", config_src.display());
}

fn main() {
    copy_sidecar_dlls();
    tauri_build::build();
}
