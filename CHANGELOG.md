# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).


## Unreleased

### Additions
- Added Windows build support for MinGW GNU/LLVM toolchains on x86_64 and aarch64.
  Windows targets build the vendored Vectorscan source by default when `HYPERSCAN_ROOT` is unset.
- Added a `HYPERSCAN_ROOT` override for linking against an existing Vectorscan/Hyperscan installation.
  The build script prefers static `libhs.a` when available, falls back to dynamic/import libraries, and accepts `HYPERSCAN_LINK_KIND=static` or `HYPERSCAN_LINK_KIND=dynamic` to choose explicitly.
- Added Windows CI coverage for release, test, and `cpu_native` builds on x86_64 and aarch64.

### Fixes
- Fixed a Windows compile error in Vectorscan's FDR code caused by passing `size_t` and `unsigned long` values to `std::min`.
  The comparison now uses matching `size_t` arguments, preserving the existing stride selection behavior while compiling on Windows' 64-bit type model.
- Fixed a Windows MinGW test-profile link error when building Rust test binaries against the vendored Vectorscan library.
  The vendored x86 `SuperVector` implementation no longer emits duplicate copy-constructor definitions.
- Fixed musl cross-compilation by supplying Vectorscan's `unistd.h` and `posix_memalign` configure results for musl targets.


## [v0.0.6](https://github.com/bradlarsen/vectorscan-rs/releases/v0.0.6) (2026-03-12)

### Changes
- Upgraded vendored version of Vectorscan from 5.4.11 to 5.4.12 ([#11](https://github.com/bradlarsen/vectorscan-rs/pull/11)).

- Vectorscan is now redistributed as an extracted source directory within the vectorscan-rs-sys tree.
  This replaces the previous pristine tarball + build-time `patch` approach.
  This change was necessary to keep the crate size below the 10MB limit imposed by Crates.io.

- Updated GitHub Actions `checkout` and `upload-artifact` steps to the latest versions.

### Fixes
- Fixed a typo in the build script that caused the `cpu_native` feature to not work ([#12](https://github.com/bradlarsen/vectorscan-rs/pull/12)).


## [v0.0.5](https://github.com/bradlarsen/vectorscan-rs/releases/v0.0.5) (2024-12-09)

### Additions
- Added `BlockDatabase::size` and `StreamingDatabase::size`, which return the size in bytes of the database.
- Added `StreamingDatabase::stream_size`, which returns the size in bytes of a stream for the database.
- A new `asan` feature enables Address Sanitizer in the vendored version of `vectorscan`.


## [v0.0.4](https://github.com/bradlarsen/vectorscan-rs/releases/v0.0.4) (2024-11-07)

### Additions
- The streaming APIs are now exposed ([#5](https://github.com/bradlarsen/vectorscan-rs/pull/5)).

- Documentation has been added for many of the Rust APIs ([#5](https://github.com/bradlarsen/vectorscan-rs/pull/5)).

### Fixes
- Debug builds are fixed on macOS using Xcode 15.


## [v0.0.3](https://github.com/bradlarsen/vectorscan-rs/releases/v0.0.3) (2024-08-21)

### Additions
- Several additional Hyperscan functions are exposed, and exposed types implement more traits ([#3](https://github.com/bradlarsen/vectorscan-rs/pull/3)).
  Specifically, the `Flag`, `Pattern`, `ScanMode`, `Scratch`, and `BlockDatabase` types now implement `Clone`, `Debug`, `Send`, and `Sync`.


## [v0.0.2](https://github.com/bradlarsen/vectorscan-rs/releases/v0.0.2) (2024-04-18)

### Additions
- A new `unit_hyperscan` feature causes the Vectorscan unit test suite to be built and run at crate build time ([#2](https://github.com/bradlarsen/vectorscan-rs/pull/2)).

### Fixes
- The compilation of the vendored version of `vectorscan` no longer uses the `-march=native` C and C++ compiler option when the `cpu_native` feature is not specified ([#1](https://github.com/bradlarsen/vectorscan-rs/pull/1)).
  Previously, `-march=native` was used unconditionally, which could cause non-portable code to be generated, leading to `SIGILL` crashes at runtime.


## [v0.0.1](https://github.com/bradlarsen/vectorscan-rs/releases/v0.0.1) (2024-04-04)

This is the initial release of the `vectorscan-rs` and `vectorscan-rs-sys` crates.
These crates were extracted from the [Nosey Parker project](https://github.com/praetorian-inc/noseyparker).

The `vectorscan-rs-sys` crate builds a vendored copy of [Vectorscan](https://github.com/Vectorcamp/vectorscan) 5.4.11.
The `vectorscan-rs` crate provides minimal Rust bindings to Vectorscan's block-based matching APIs.
