# Building the Engine, Dependencies, and Examples

Building Bonsai is pretty straight-forward.  The main entry point for the build
is a shell script, `make.sh`.

NOTE: The officially supported compiler is clang-15.  Different versions may
work, but may also emit warnings, or errors.  If the following instructions do
not work for you, by all means open an issue and I will do what I can to assist.

## Dependencies

Follow the instructions for fetching dependencies for bonsai_stdlib [https://github.com/scallyw4g/bonsai_stdlib/blob/master/docs/dependencies.md](https://github.com/scallyw4g/bonsai_stdlib/blob/master/docs/dependencies.md)

## macOS

The only toolchain dependency is the Xcode Command Line Tools, `xcode-select --install`;
Cocoa, OpenGL and IOKit come from the SDK.

```bash
git clone --recursive https://github.com/scallyw4g/bonsai bonsai && cd bonsai
./make.sh && ./make.sh RunTests
./bin/game_loader ./bin/game_libs/terrain_gen_loadable.dylib
```

Two things about the macOS build are worth knowing before reading a diff:

* **It cross-targets x86_64 and runs under Rosetta 2.**  The SIMD layer is SSE/AVX-only, and
  `-mssse3 -mavx -mavx2 -mfma` are hard errors for an arm64 target, so
  `external/bonsai_stdlib/scripts/setup_for_cxx.sh` passes
  `-target x86_64-apple-macos11`.  On Apple Silicon the binaries are therefore x86_64 and
  are executed through Rosetta; installing it is part of the `build-macos` CI job for the
  same reason.
* **Every file is compiled as Objective-C++** (`-x objective-c++`), because the engine is one
  translation unit per target and `platform/macos/macos_platform.cpp` uses AppKit directly.
  Objective-C++ is a superset of C++ meaning there should be no effect.

The macOS backend drives a **4.1 core** GL context, which is the highest macOS offers.  A few
GL 4.3/4.5 features are therefore unavailable; where that changed the code (rather than just
the loader), the site carries a `NOTE(nsillik)(macos)` explaining it.

See [macos_port.md](macos_port.md) for the port itself.

## Quickstart

```
git clone --recursive https://github.com/scallyw4g/bonsai bonsai && cd bonsai
./make.sh
```

## Build Options

Envoke the make script from the root directory by typing `./make.sh`

By default, the script is configured to build everything that should build with
low resistance in release mode.

The following options can be appended to the make script to control which targets are built.

* BuildExecutables

Builds various standalone tools that the engine relies on, including the
game_loader, which is the entry point when running a game.

* BuildBundledExamples

Builds all the examples bundled with the engine.

* BuildSingleExample

Builds a single example.  Can target a bundled example, or a custom game.  More
information on [02_create_new_project.md](02_create_new_project.md).  You may
pass this option multiple times.

* BuildTests

Builds the test suites for the bonsai stdlib.

* BuildDebugOnlyTests

Builds tests that are only valid in debug mode, due to breaking in -O2 mode.

NOTE(Jesse): This is a long-standing kludge that should be removed, but it
requires writing an instruction decoder, which I was not smart enough to do
when I started the project.  I could do it now, but haven't gotten around to
it.

* RunTests

Runs the test suites

* BuildDebugSystem

Builds the debug system, which the engine uses to do performance and memory
allocation profiling.

* MakeDebugLibRelease

Builds and bundles the assets suitable for creating a debug system release,
which makes it easy to integrate into external projects that do not depend
on Bonsai or bonsai_stdlib.

* RunPoof

Runs the metaprogramming compiler, `poof`.  See [poof](https://github.com/scallyw4g/poof) for details.

* BuildWithEMCC

Builds the engine using emcc, targeting WASM.

NOTE(Jesse): This is currently broken, though it should be pretty easy to resurrect.

* -Od
* -O0
* -O1
* -O2

Controls the optimization level passed to the compiler.
