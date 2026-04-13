use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Get the environment variable with the given name, panicking if it is not set.
fn env(name: &str) -> String {
    std::env::var(name).unwrap_or_else(|_| panic!("`{}` should be set in the environment", name))
}

#[derive(Debug)]
struct Target {
    triple: String,
    os: String,
    env: String,
    arch: String,
}

impl Target {
    fn from_env() -> Self {
        Self {
            triple: env("TARGET"),
            os: env("CARGO_CFG_TARGET_OS"),
            env: std::env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default(),
            arch: env("CARGO_CFG_TARGET_ARCH"),
        }
    }

    fn is_windows(&self) -> bool {
        self.os == "windows"
    }

    fn is_musl(&self) -> bool {
        self.triple.ends_with("-musl")
    }

    fn is_windows_gnullvm(&self) -> bool {
        self.triple.ends_with("gnullvm")
    }
}

fn main() {
    // Note: use `rerun-if-changed=build.rs` to indicate that this build script *shouldn't* be
    // rerun: see https://doc.rust-lang.org/cargo/reference/build-scripts.html#change-detection
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=HYPERSCAN_ROOT");
    println!("cargo:rerun-if-env-changed=MINGW_PREFIX");
    println!("cargo:rerun-if-env-changed=MSYSTEM_PREFIX");
    println!("cargo:rerun-if-env-changed=PATH");

    let manifest_dir = PathBuf::from(env("CARGO_MANIFEST_DIR"));
    let out_dir = PathBuf::from(env("OUT_DIR"));
    let target = Target::from_env();

    let build = match std::env::var_os("HYPERSCAN_ROOT") {
        Some(hs_root) => BuildOutput {
            include_dir: PathBuf::from(&hs_root).join("include"),
            lib_dirs: lib_dirs_for_root(Path::new(&hs_root)),
            source_built: false,
        },
        None => build_vendored_vectorscan(&manifest_dir, &out_dir, &target),
    };

    link_library_dirs(&build.lib_dirs);
    println!("cargo:rustc-link-lib=static=hs");
    link_cpp_runtime(&target);

    run_hyperscan_unit_tests(&out_dir, &target, build.source_built);
    write_bindings(&manifest_dir, &out_dir, &build.include_dir);
}

struct BuildOutput {
    include_dir: PathBuf,
    lib_dirs: Vec<PathBuf>,
    source_built: bool,
}

fn build_vendored_vectorscan(manifest_dir: &Path, out_dir: &Path, target: &Target) -> BuildOutput {
    let include_dir = out_dir.join("include");
    let vectorscan_src_dir = manifest_dir.join("vectorscan");
    let mut cfg = cmake::Config::new(&vectorscan_src_dir);

    macro_rules! cfg_define_feature {
        ($cmake_feature: tt, $cargo_feature: tt) => {
            cfg.define(
                $cmake_feature,
                if cfg!(feature = $cargo_feature) {
                    "ON"
                } else {
                    "OFF"
                },
            )
        };
    }

    let profile = {
        // See https://doc.rust-lang.org/cargo/reference/profiles.html#opt-level for possible values
        match env("OPT_LEVEL").as_str() {
            "0" => "Debug",
            "s" | "z" => "MinSizeRel",
            _ => "Release",
        }
    };

    cfg.profile(profile)
        .define("CMAKE_INSTALL_INCLUDEDIR", &include_dir)
        .define("CMAKE_VERBOSE_MAKEFILE", "ON")
        .define("BUILD_SHARED_LIBS", "OFF")
        .define("BUILD_STATIC_LIBS", "ON")
        .define("FAT_RUNTIME", "OFF")
        .define("WARNINGS_AS_ERRORS", "OFF")
        .define("BUILD_EXAMPLES", "OFF")
        .define("BUILD_BENCHMARKS", "OFF")
        .define("BUILD_DOC", "OFF")
        .define("BUILD_TOOLS", "OFF");

    cfg_define_feature!("BUILD_UNIT", "unit_hyperscan");
    cfg_define_feature!("USE_CPU_NATIVE", "cpu_native");

    if cfg!(feature = "asan") {
        cfg.define("SANITIZE", "address");
    }

    // NOTE: Several Vectorscan feature flags can be set based on available CPU SIMD features.
    // Enabling these according to availability on the build system CPU is fragile, however:
    // the resulting binary will not work correctly on machines with CPUs with different SIMD
    // support.
    //
    // By default, we simply disable these options. However, using the `simd-specialization`
    // feature flag, these Vectorscan features will be enabled if the build system's CPU
    // supports them.
    //
    // See
    // https://doc.rust-lang.org/reference/attributes/codegen.html#the-target_feature-attribute
    // for supported target_feature values.
    if cfg!(feature = "simd_specialization") {
        macro_rules! x86_64_feature {
            ($feature: tt) => {{
                #[cfg(target_arch = "x86_64")]
                if std::arch::is_x86_feature_detected!($feature) {
                    "ON"
                } else {
                    "OFF"
                }
                #[cfg(not(target_arch = "x86_64"))]
                "OFF"
            }};
        }

        macro_rules! aarch64_feature {
            ($feature: tt) => {{
                #[cfg(target_arch = "aarch64")]
                if std::arch::is_aarch64_feature_detected!($feature) {
                    "ON"
                } else {
                    "OFF"
                }
                #[cfg(not(target_arch = "aarch64"))]
                "OFF"
            }};
        }

        cfg.define("BUILD_AVX2", x86_64_feature!("avx2"));
        // XXX use avx512vbmi as a proxy for this, as it's not clear which particular avx512
        // instructions are needed
        cfg.define("BUILD_AVX512", x86_64_feature!("avx512vbmi"));
        cfg.define("BUILD_AVX512VBMI", x86_64_feature!("avx512vbmi"));

        cfg.define("BUILD_SVE", aarch64_feature!("sve"));
        cfg.define("BUILD_SVE2", aarch64_feature!("sve2"));
        cfg.define("BUILD_SVE2_BITPERM", aarch64_feature!("sve2-bitperm"));
    } else {
        cfg.define("BUILD_AVX2", "OFF")
            .define("BUILD_AVX512", "OFF")
            .define("BUILD_AVX512VBMI", "OFF")
            .define("BUILD_SVE", "OFF")
            .define("BUILD_SVE2", "OFF")
            .define("BUILD_SVE2_BITPERM", "OFF");
    }

    // Under cargo-zigbuild for musl targets, Vectorscan's configure-time probes can incorrectly
    // miss posix_memalign/unistd. Scope this workaround to musl targets only.
    if target.is_musl() {
        cfg.define("HAVE_UNISTD_H", "1")
            .define("HAVE_POSIX_MEMALIGN", "1");
    }

    if target.is_windows() && target.arch == "aarch64" {
        cfg.define("CMAKE_SYSTEM_NAME", "Windows")
            .define("CMAKE_SYSTEM_PROCESSOR", "ARM64");
    }

    let dst = cfg.build();

    BuildOutput {
        include_dir,
        lib_dirs: vec![dst.join("lib"), dst.join("lib64")],
        source_built: true,
    }
}

fn lib_dirs_for_root(root: &Path) -> Vec<PathBuf> {
    vec![root.join("lib"), root.join("lib64")]
}

fn link_library_dirs(lib_dirs: &[PathBuf]) {
    for lib_dir in lib_dirs {
        println!("cargo:rustc-link-search=native={}", lib_dir.display());
    }
}

fn link_cpp_runtime(target: &Target) {
    if target.is_windows() {
        if target.env == "msvc" {
            // MSVC selects its own C++ runtime through the selected Rust/C toolchain.
        } else if target.is_windows_gnullvm() {
            // On clang/LLVM MinGW targets, prefer static LLVM runtime linkage to avoid runtime
            // DLL dependencies.
            add_windows_runtime_search_dirs(
                target,
                &["libc++.a", "libc++abi.a", "libunwind.a"],
                &windows_clang_tools(target),
            );
            println!("cargo:rustc-link-lib=static=c++");
            println!("cargo:rustc-link-lib=static=c++abi");
            println!("cargo:rustc-link-lib=static=unwind");
        } else if target.env == "gnu" {
            // On MinGW GNU targets, prefer static GNU C++ runtime linkage to avoid runtime DLL
            // dependencies.
            add_windows_runtime_search_dirs(
                target,
                &["libstdc++.a", "libgcc.a", "libwinpthread.a"],
                &windows_gnu_tools(target),
            );
            println!("cargo:rustc-link-lib=static=stdc++");
            println!("cargo:rustc-link-lib=static=gcc");
            println!("cargo:rustc-link-lib=static=winpthread");
        } else {
            println!("cargo:rustc-link-lib=stdc++");
        }
        return;
    }

    if target.os == "macos" {
        println!("cargo:rustc-link-lib=c++");
        return;
    }

    let compiler_version_out = String::from_utf8(
        Command::new("c++")
            .args(["-v"])
            .output()
            .expect("Failed to get C++ compiler version")
            .stderr,
    )
    .unwrap();

    if compiler_version_out.contains("gcc") {
        println!("cargo:rustc-link-lib=stdc++");
    } else if compiler_version_out.contains("clang") {
        println!("cargo:rustc-link-lib=c++");
    } else {
        panic!("No compatible compiler found: either clang or gcc is needed");
    }
}

fn windows_gnu_tools(target: &Target) -> Vec<&'static str> {
    match target.arch.as_str() {
        "x86_64" => vec![
            "x86_64-w64-mingw32-g++",
            "x86_64-w64-mingw32-gcc",
            "g++",
            "gcc",
            "c++",
        ],
        "aarch64" => vec![
            "aarch64-w64-mingw32-g++",
            "aarch64-w64-mingw32-gcc",
            "g++",
            "gcc",
            "c++",
        ],
        _ => vec!["g++", "gcc", "c++"],
    }
}

fn windows_clang_tools(target: &Target) -> Vec<&'static str> {
    match target.arch.as_str() {
        "x86_64" => vec![
            "x86_64-w64-mingw32-clang++",
            "x86_64-w64-mingw32-clang",
            "clang++",
            "clang",
        ],
        "aarch64" => vec![
            "aarch64-w64-mingw32-clang++",
            "aarch64-w64-mingw32-clang",
            "clang++",
            "clang",
        ],
        _ => vec!["clang++", "clang"],
    }
}

fn add_windows_runtime_search_dirs(target: &Target, lib_names: &[&str], tools: &[&str]) {
    if !target.is_windows() {
        return;
    }

    let mut dirs = BTreeSet::new();

    for tool in tools {
        for lib_name in lib_names {
            if let Some(dir) = runtime_lib_dir_from_tool(tool, lib_name) {
                dirs.insert(dir);
            }
        }
    }

    for prefix_var in ["MINGW_PREFIX", "MSYSTEM_PREFIX"] {
        if let Some(prefix) = std::env::var_os(prefix_var) {
            let lib_dir = PathBuf::from(prefix).join("lib");
            if lib_names.iter().any(|lib_name| {
                resolve_existing_path(lib_dir.join(lib_name))
                    .map(|lib_path| lib_path.is_file())
                    .unwrap_or(false)
            }) {
                if let Some(dir) = resolve_existing_path(lib_dir) {
                    dirs.insert(dir);
                }
            }
        }
    }

    for dir in dirs {
        println!("cargo:rustc-link-search=native={}", dir.display());
    }
}

fn runtime_lib_dir_from_tool(tool: &str, lib_name: &str) -> Option<PathBuf> {
    let output = Command::new(tool)
        .arg(format!("-print-file-name={lib_name}"))
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }

    let stdout = String::from_utf8(output.stdout).ok()?;
    let lib_path = PathBuf::from(stdout.trim());
    if lib_path.as_os_str().is_empty() || lib_path == PathBuf::from(lib_name) {
        return None;
    }

    let lib_path = resolve_existing_path(lib_path)?;
    if !lib_path.is_file() {
        return None;
    }

    lib_path.parent().map(Path::to_path_buf)
}

fn resolve_existing_path(path: PathBuf) -> Option<PathBuf> {
    if path.exists() {
        return Some(path);
    }

    let path_str = path.to_string_lossy();
    if !path_str.starts_with('/') {
        return None;
    }

    let output = Command::new("cygpath")
        .args(["-m", path_str.as_ref()])
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }

    let stdout = String::from_utf8(output.stdout).ok()?;
    let converted = PathBuf::from(stdout.trim());
    converted.exists().then_some(converted)
}

fn write_bindings(manifest_dir: &Path, out_dir: &Path, include_dir: &Path) {
    #[cfg(feature = "bindgen")]
    {
        let _ = manifest_dir;
        let config = bindgen::Builder::default()
            .allowlist_function("hs_.*")
            .allowlist_type("hs_.*")
            .allowlist_var("HS_.*")
            .header("wrapper.h")
            .clang_arg(format!("-I{}", include_dir.display()));
        config
            .generate()
            .expect("Unable to generate bindings")
            .write_to_file(out_dir.join("bindings.rs"))
            .expect("Failed to write Rust bindings to Vectorscan");
    }
    #[cfg(not(feature = "bindgen"))]
    {
        let _ = include_dir;
        std::fs::copy(
            manifest_dir.join("src").join("bindings.rs"),
            out_dir.join("bindings.rs"),
        )
        .expect("Failed to write Rust bindings to Vectorscan");
    }
}

fn run_hyperscan_unit_tests(out_dir: &Path, target: &Target, source_built: bool) {
    #[cfg(feature = "unit_hyperscan")]
    {
        if !source_built {
            panic!("The unit_hyperscan feature requires building the vendored Vectorscan source");
        }

        let executable_name = if target.is_windows() {
            "unit-hyperscan.exe"
        } else {
            "unit-hyperscan"
        };
        let unittests = out_dir.join("build").join("bin").join(executable_name);
        match Command::new(unittests).status() {
            Ok(rc) if rc.success() => {}
            Ok(rc) => panic!("Failed to run unit tests: exit with code {rc}"),
            Err(e) => panic!("Failed to run unit tests: {e}"),
        }
    }

    #[cfg(not(feature = "unit_hyperscan"))]
    {
        let _ = (out_dir, target, source_built);
    }
}
