# `vectorscan-rs-sys`


## Overview
This crate implements minimal Rust bindings to the [Vectorscan](https://github.com/Vectorcamp/vectorscan) fork of [Hyperscan](https://github.com/intel/hyperscan), the high-performance regular expression engine.
This crate builds a vendored copy of Vectorscan from source.


## Dependencies
- [Boost](https://boost.org) >= 1.57
- [CMake](https://cmake.org)
- Optional: [Clang](https://clang.llvm.org), when building with the `bindgen` feature

This has been tested on x86_64 Linux, x86_64 macOS, and aarch64 macOS.
The CI matrix also exercises x86_64 Windows with MSYS2 MINGW64 and aarch64 Windows with MSYS2 CLANGARM64.

Set `HYPERSCAN_ROOT` to the install prefix of an existing Vectorscan/Hyperscan installation to link against that installation instead of building the bundled source.
The build script expects headers under `$HYPERSCAN_ROOT/include` and libraries under `$HYPERSCAN_ROOT/lib` or `$HYPERSCAN_ROOT/lib64`.
When `HYPERSCAN_ROOT` is set, the build script prefers static linkage if `libhs.a` is present, otherwise it uses a dynamic/import library if one is present.
Set `HYPERSCAN_LINK_KIND=static` or `HYPERSCAN_LINK_KIND=dynamic` to choose explicitly.


## Implementation Notes
This crate was originally written as part of [Nosey Parker](https://github.com/praetorian-inc/noseyparker).
It was adapted from the [pyperscan](https://github.com/vlaci/pyperscan) project, which uses Rust to expose Hyperscan to Python.
(That project is released under either the Apache 2.0 or MIT license.)

The only bindings exposed at present are for Vectorscan's block-based matching APIs.
The various other APIs such as stream- and vector-based matching are not exposed.
Other features, such as the Chimera PCRE library, test code, benchmark code, and supporting utilities are disabled.

The source of Vectorscan 5.4.12 is included here in the `vectorscan` directory.
It has been modified with a few patches:

- The CMake-based build system is modified to eliminate the build-time dependency on `ragel`
- The precompiled version of 4 [Ragel](https://github.com/adrian-thurston/ragel) `.rl` files are added to the source tree
- The CMake-based build system is modified to allow disabling several components that are not used in this crate

Previously these modifications were stored in a patchfile that was applied to a pristine copy of the Vectorscan sources at build time.
However, that approach proved problematic with the upgrade to Vectorscan 5.4.12, which is a 20MB tarball, putting the Cargo crates over the 10MB limit for Crates.io.
Hence, we go with the grungier approach of keeping a modified vendored copy of the sources within this repository.

If you want to see the exact contents of the modifications that have been applied to Vectorscan, diff the `vectorscan` directory against a pristine version of the 5.4.12 release tarball from the Vectorscan repo.


## License
This project is licensed under either of

- [Apache License, Version 2.0](https://www.apache.org/licenses/LICENSE-2.0)
  ([LICENSE-APACHE](../LICENSE-APACHE))

- [MIT License](https://opensource.org/licenses/MIT)
  ([LICENSE-MIT](../LICENSE-MIT))

at your option.

This project contains a vendored copy of [Vectorscan](https://github.com/Vectorcamp/vectorscan), which is released under a 3-clause BSD license.
See the [NOTICE](../NOTICE) file for details.


## Contributing
Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in `vectorscan-rs-sys` by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any additional terms or conditions.
