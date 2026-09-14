# Building the Engine, Dependencies, and Examples

Building Bonsai is pretty straight-forward.  The main entry point for the build
is a shell script, `make.sh`.

NOTE: The officially supported compiler is clang-18.1 or newer.  Different
versions may work, but may also emit warnings, or errors.  If the following
instructions do not work for you, by all means open an issue and I will do what
I can to assist.

## Supported platforms

Linux, Windows (MinGW) and macOS.  See [Platform notes](#platform-notes) below
for what each one requires.

## Dependencies

Follow the instructions for fetching dependencies for bonsai_stdlib [https://github.com/scallyw4g/bonsai_stdlib/blob/master/docs/dependencies.md](https://github.com/scallyw4g/bonsai_stdlib/blob/master/docs/dependencies.md)

Toolchain and system packages are declared in `.mise.toml` at the repo root:

```
mise bootstrap packages apply && mise install
```

`mise bootstrap packages apply` installs the Linux system packages (libx11-dev,
freeglut3-dev) and `mise install` installs the pinned clang.  Both entries are
filtered to Linux with `os = ["linux"]`, so running this on macOS is a no-op
rather than an error.

`mise run build` and `mise run test` wrap `./make.sh` and
`./make.sh RunTests`.

## Quickstart

```
git clone --recursive https://github.com/scallyw4g/bonsai bonsai && cd bonsai
./make.sh
```

## Platform notes

### macOS

The one true prerequisite is the Xcode Command Line Tools, which mise cannot
install:

```
xcode-select --install
```

`./make.sh` then builds with the system Apple clang.  Do **not** install clang
through mise on macOS -- it would shadow Apple clang, and the mise clang pin is
for Linux.

macOS builds are cross-compiled to **x86_64** and run under Rosetta 2, because
the SIMD layer is SSE/AVX-only.  `-mssse3 -mavx -mavx2 -mfma` are hard errors
for an `arm64-apple-darwin` target, so on Apple Silicon the build passes
`-target x86_64-apple-macos11` and the output runs under Rosetta.  Rosetta is
preinstalled on most Macs; if not:

```
softwareupdate --install-rosetta --agree-to-license
```

Native arm64 is tracked separately and will drop the `-target` flag along with
those four options.

Because the whole tree is compiled as a single translation unit per target and
the macOS backend uses AppKit directly, macOS targets are built as
Objective-C++ (`-x objective-c++`).  There is no `.mm` shim.

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
