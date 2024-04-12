# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).


## Unreleased

### Fixes

- The compilation of the vendored version of `vectorscan` no longer uses the `-march=native` C and C++ compiler option when the `cpu_native` feature is not specified ([#1](https://github.com/bradlarsen/vectorscan-rs/pulls/1)).
  Previously, `-march=native` was used unconditionally, which could cause non-portable code to be generated, leading to `SIGILL` crashes at runtime.


## [v0.0.1](https://github.com/bradlarsen/vectorscan-rs/releases/v0.0.1) (2024-04-04)

This is the initial release of the `vectorscan-rs` and `vectorscan-rs-sys` crates.
These crates were extracted from the [Nosey Parker project](https://github.com/praetorian-inc/noseyparker).

The `vectorscan-rs-sys` crate builds a vendored copy of [Vectorscan](https://github.com/Vectorcamp/vectorscan) 5.4.11.
The `vectorscan-rs` crate provides minimal Rust bindings to Vectorscan's block-based matching APIs.
