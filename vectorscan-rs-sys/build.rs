use std::path::PathBuf;
use std::process::Command;

/// Get the environment variable with the given name, panicking if it is not set.
fn env(name: &str) -> String {
    std::env::var(name).unwrap_or_else(|_| panic!("`{}` should be set in the environment", name))
}

fn main() {
    // Note: use `rerun-if-changed=build.rs` to indicate that this build script *shouldn't* be
    // rerun: see https://doc.rust-lang.org/cargo/reference/build-scripts.html#change-detection
    println!("cargo:rerun-if-changed=build.rs");

    let manifest_dir = PathBuf::from(env("CARGO_MANIFEST_DIR"));
    let out_dir = PathBuf::from(env("OUT_DIR"));

    let include_dir = out_dir
        .join("include")
        .into_os_string()
        .into_string()
        .unwrap();

    // Choose appropriate C++ runtime library
    {
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

    let vectorscan_src_dir = manifest_dir.join("vectorscan");

    // Build with cmake
    {
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

        let dst = cfg.build();

        println!("cargo:rustc-link-lib=static=hs");
        println!("cargo:rustc-link-search={}", dst.join("lib").display());
        println!("cargo:rustc-link-search={}", dst.join("lib64").display());
    }

    // Run hyperscan unit test suite
    #[cfg(feature = "unit_hyperscan")]
    {
        let unittests = out_dir.join("build").join("bin").join("unit-hyperscan");
        match Command::new(unittests).status() {
            Ok(rc) if rc.success() => {}
            Ok(rc) => panic!("Failed to run unit tests: exit with code {rc}"),
            Err(e) => panic!("Failed to run unit tests: {e}"),
        }
    }

    // Run bindgen if needed, or else use the pre-generated bindings
    #[cfg(feature = "bindgen")]
    {
        let config = bindgen::Builder::default()
            .allowlist_function("hs_.*")
            .allowlist_type("hs_.*")
            .allowlist_var("HS_.*")
            .header("wrapper.h")
            .clang_arg(format!("-I{}", &include_dir));
        config
            .generate()
            .expect("Unable to generate bindings")
            .write_to_file(out_dir.join("bindings.rs"))
            .expect("Failed to write Rust bindings to Vectorscan");
    }
    #[cfg(not(feature = "bindgen"))]
    {
        std::fs::copy("src/bindings.rs", out_dir.join("bindings.rs"))
            .expect("Failed to write Rust bindings to Vectorscan");
    }
}
