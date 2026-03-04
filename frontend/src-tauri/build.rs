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

    let dlls: &[&str] = &[
        "brotlicommon.dll",
        "brotlidec.dll",
        "brotlienc.dll",
        "cares.dll",
        "drogon.dll",
        "jsoncpp.dll",
        "libcrypto-3-x64.dll",
        "libssl-3-x64.dll",
        "trantor.dll",
        "zlib1.dll",
    ];

    for dll in dlls {
        let src = dll_src.join(dll);
        if src.exists() {
            let dst = target_dir.join(dll);
            std::fs::copy(&src, &dst).unwrap_or_else(|e| {
                panic!("failed to copy {}: {e}", src.display())
            });
            println!("cargo:warning=copied sidecar DLL: {dll}");
        } else {
            println!("cargo:warning=sidecar DLL not found: {}", src.display());
        }
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
