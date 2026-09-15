# Building the Engine, Dependencies, and Examples

Building Bonsai is pretty straight-forward.  The main entry point for the build
is a shell script, `make.sh`.

NOTE: The officially supported compiler is clang-15.  Different versions may
work, but may also emit warnings, or errors.  If the following instructions do
not work for you, by all means open an issue and I will do what I can to assist.

## Dependencies

Follow the instructions for fetching dependencies for bonsai_stdlib [https://github.com/scallyw4g/bonsai_stdlib/blob/master/docs/dependencies.md](https://github.com/scallyw4g/bonsai_stdlib/blob/master/docs/dependencies.md)

## macOS

The toolchain dependency is the Xcode Command Line Tools, `xcode-select --install`;
Cocoa, OpenGL and IOKit come from the SDK.  Nothing else is needed -- in particular
not a compiler from Homebrew, which would shadow Apple clang.

```bash
git clone --recursive https://github.com/nsillik/bonsai bonsai && cd bonsai
./make.sh && ./make.sh RunTests
./bin/game_loader ./bin/game_libs/terrain_gen_loadable.dylib
```

Two things about the macOS build are worth knowing before reading a diff:

* **It cross-targets x86_64 and runs under Rosetta 2.**  The SIMD layer is SSE/AVX-only, and
  `-mssse3 -mavx -mavx2 -mfma` are hard errors for an arm64 target, so
  `scripts/setup_for_cxx.sh` passes `-target x86_64-apple-macos11`.  On Apple Silicon the
  binaries are therefore x86_64 and are executed through Rosetta; installing it is part of
  the `build-macos` CI job for the same reason.  Anything that inspects the *host* arch, or
  reads `_SC_PAGESIZE` under Rosetta, is looking at the translation, not the machine.
* **Every file is compiled as Objective-C++** (`-x objective-c++`), because the engine is one
  translation unit per target and `platform/macos/macos_platform.cpp` uses AppKit directly.
  There is no `.mm` shim.  Objective-C++ is a superset of C++, so nothing else in the tree
  notices.

The macOS backend drives a **4.1 core** GL context, which is the highest macOS offers.  A few
GL 4.3/4.5 entry points the tree used are therefore unavailable; where that changed the code
rather than just the loader, the site carries a `NOTE(nsillik)(macos)` explaining it.  The
port as a whole is written up in [docs/macos_port.md](macos_port.md).

### Reference Linux build

Comparing a renderer across platforms needs a Linux build of the same commit.  Build in a
**copied** tree, not a bind mount of the working repo -- `./make.sh` writes to `./bin`, so a
bind mount silently overwrites the host's macOS binaries with ELF objects:

```bash
tar --exclude=.git --exclude=bin --exclude='*.dSYM' -cf - . | (cd /tmp/linuxsrc && tar -xf -)
docker run --rm --platform linux/amd64 -v /tmp/linuxsrc:/src -w /src ubuntu:24.04 bash -lc '
  apt-get update -qq &&
  DEBIAN_FRONTEND=noninteractive apt-get install -y -qq clang-18 libx11-dev freeglut3-dev xvfb &&
  ln -sf /usr/bin/clang++-18 /usr/local/bin/clang++ &&
  ./make.sh'
```

`make.sh` hardcodes `COMPILER="clang++"`, and the Ubuntu package installs `clang++-18` only,
which is what the symlink is for.

Under llvmpipe both `MESA_GL_VERSION_OVERRIDE=4.6` and `MESA_GLSL_VERSION_OVERRIDE=460` are
required: without them it reports GL 4.5, `#version 460` fails to compile, and
`InitializeShadowRenderGroup` asserts with an empty log.  That is a software-rendering
limitation, not a code problem.

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
