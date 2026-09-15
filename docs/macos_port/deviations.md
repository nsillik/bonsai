# macOS Port: Deviations from the Plan

Every place implementation turned out to differ from [plan.md](plan.md), in the order they were
found, plus the risks that were realised rather than anticipated.  Referenced from the code as
`#<n>`; the operational summary of the still-load-bearing ones is the gotcha list in
[progress.md](progress.md#next-session).

---
## Deviations from this plan

Corrections found during implementation. Each is recorded where it was discovered, in the
Phase 0/1/2 sections below; this is the index.

### 1. Phase 1 needed the Phase 2 platform backend (~485 lines)

This plan puts `macos_platform.{h,cpp}` in Phase 2 and only build-script changes in Phase 1. That
does not work: `platform.h` and `platform.cpp` must dispatch *somewhere* for `BONSAI_MACOS`, and
`./make.sh` must **link**, which requires every window/OS symbol to be defined. So Phase 1 already
created the file: `macos_platform.h` (158 L) + `macos_platform.cpp` (327 L).

Consequence: Phase 2 is now only the *interactive* and *correctness* work — key/mouse mapping, Retina
assertions, live resize, and making tests pass — not the creation of the backend. The plan's "~500 L"
estimate was accurate; it just landed one phase early.

### 2. Objective-C++ is required, and applies to every target

Not anticipated. The engine is one translation unit per target, and `macos_platform.cpp` uses AppKit
directly, so the macOS block sets `-x objective-c++` and **all** macOS targets compile as
Objective-C++. There is no `.mm` shim, contrary to the usual approach. Two consequences: Objective-C++
accepts a superset of C++ so the rest of the tree is unaffected, and any future macOS-specific code
can use AppKit inline rather than needing a boundary.

### 3. `Fract` collides with the SDK (blocked the build outright)

Not anticipated, and the single largest surprise. `<MacTypes.h>` declares `typedef SInt32 Fract;`,
and it arrives transitively through **every** Foundation-based header — Cocoa, AppKit, Foundation,
`NSWindow.h`, `NSEvent.h`. Only `<OpenGL/OpenGL.h>` is clean. C++ does not let a typedef and a
function share a name, so `maff.h`'s `f32 Fract(f32)` fails with:

```
error: redefinition of 'Fract' as different kind of symbol
```

→ 18 cascading errors across `maff.h`, `noise.h`, `random.h`, `perlin.h`, `entity.cpp`.
`macos_platform.h` renames the SDK's typedef for the duration of the include:

```c
#define Fract BonsaiSdkFract
#import <Cocoa/Cocoa.h>
#undef Fract
```

`MacTypes.h` is include-guarded, so the rename is one-shot and consistent. The alternative — renaming
our `Fract` — touches four headers plus a call site in a submodule we do not own, to fix a name that
is fine on Linux and Windows. **Flagged on the PR as worth a second opinion.**

### 4. `link_weak` does not exist on Darwin; needed `-Wl,-U` per hook

Not anticipated. `link_weak` is `extern "C" __attribute__((weak))` — the ELF rule for "may be
undefined; bind to 0". ld64 has no equivalent for a **main executable**: it treats the reference as a
strong undefined symbol and fails the link. Measured: `weak_import` does not help either, because it
only relaxes a link against a dylib that *does* define the symbol.

`setup_for_cxx.sh` therefore names each hook explicitly with `-Wl,-U,_Symbol` — six of them:
`EntityUserDataSerialize`, `EntityUserDataDeserialize`, `EntityUserDataEditorUi`,
`GameEntityUpdate`, `BindEngineUniform`, `WorkerThread_BeforeSleep`. All are null-checked at their
call sites (`if (EntityUserDataSerialize)`), so a 0 binding is the intended behaviour.

Deliberately **not** `-Wl,-undefined,dynamic_lookup`, which links with one flag but disables
undefined-symbol checking for the whole link, turning genuine typos and missing libraries into
runtime crashes. Tradeoff: a new `link_weak` hook requires editing that list. Failure mode is a link
error naming the symbol, and the comment in `setup_for_cxx.sh` says so.

Any future port to a Darwin-like linker hits this. If it recurs, the alternatives are
`-Wl,-undefined,dynamic_lookup` (rejected above) or converting the hooks to function pointers
(`link_weak f(void)` → `link_weak f = 0`), which is the portable spelling and would be the right fix
if the list keeps growing.

### 5. `ErrnoToString` was **not** ported (plan said Phase 2 must supply it)

The Phase 2 list below names `ErrnoToString` alongside `PlatformCreateDir`/`PlatformDeleteDir` as
something `macos_platform.h` must supply. It has **zero callers**: the only reference in the tree is
commented out at `linux_file.cpp:126`, which `macos_platform.cpp` includes.

More decisively, it cannot be ported as written: the Linux version is a 745-line switch over 122
`errno` constants, and macOS defines only **85 of those 122**. So a copy would silently drop 37
cases, and the function would still `Assert(Result)` on any of them.

Decision: do not port it. If something ever needs errno strings on macOS, use `strerror(3)` — which
is what the macOS `PlatformSetThreadPriority` branch does. Removing it from the Phase 2 list.

### 6. `SetVSync`: the plan's item 9 is moot

Phase 3 item 9 (macOS branch in `gl.cpp`) and the Phase 2 list both reference `SetVSync`. It is still
never called — both call sites remain commented out (`initialize.cpp:72`, `linux_platform.cpp:89`),
exactly as this plan notes. The swap interval is instead set directly in
`OpenAndInitializeWindow` via `[NSOpenGLContext setValues:&n forParameter:NSOpenGLCPSwapInterval]`,
which is where the context is created and is the only place the value is available. No `gl.cpp`
change is needed unless `SetVSync` is ever wired up for both platforms.

### 7. Test discovery glob matched `.dSYM` bundles

Not anticipated. `run_tests.sh` globbed `./bin/tests/*`; on macOS clang emits a `.dSYM` bundle
directory beside every binary, so the loop tried to *execute* the directory and reported 90 spurious
suite failures on a fully-passing build. Now
`find ./bin/tests -maxdepth 1 -type f -perm -u+x`, shared with Linux. This is the kind of thing that
looks like 90 test failures and is not.

### 8. `uintptr_t` is not in scope in the platform headers — broke Linux

CI caught this on the first push of Phase 1, after it had compiled cleanly on macOS. 18 instances of
`unknown type name 'uintptr_t'` on `build-ubuntu-22`.

`<stdint.h>` is commented out in `primitives.h:156`, and neither `posix_platform.h` nor
`linux_platform.h` includes it. `uintptr_t` resolved on macOS only because the Cocoa headers happen
to drag it in — which is exactly the trap this whole phase is about.

→ Cast through `umm` instead: the codebase's own pointer-sized integer, asserted at
`primitives.h:81` (`CAssert(sizeof(umm) == sizeof(void*))`). No new include on either platform.

**Lesson for later phases: a clean macOS build is not evidence that a shared-header change is
portable.** Any edit to `posix_platform.{h,cpp}` must be built on Linux before pushing. See the
Docker recipe under Phase 1 progress above.

### 9. Stacked PRs trigger no CI without an extra change

`.github/workflows/build.yml`'s `pull_request.branches` filter matches the **base** branch. Phase 0
added `master` to it; that covers Phase 0's PR (base `master`) but **not** Phase 1's (base
`port/macos`), which reported "no checks reported" until the filter was removed entirely. Each PR in
a stack needs its own CI run. The `branches` filter is now absent from `pull_request`; `push` keeps
its `["master", "develop"]` filter.

### 10. `gh stack` CLI is unusable against a fork

`gh` v2.100.0 with the `gh-stack` extension (v0.1.1) resolves the repository to the fork's
**parent**: `gh stack init` writes `github.com:scallyw4g/bonsai` into `.git/gh-stack` regardless of
`gh repo set-default`. Its PR lookups then fail (`Could not resolve to a PullRequest with the number
of 1`) and `gh stack submit` attempts to create PRs on upstream — which is precisely the thing that
must not happen.

Workaround, and it works: open each PR with an explicit base

```bash
gh pr create -R nsillik/bonsai -H port/macos-phase1 -B port/macos
```

then group them into a native Stack **by hand in the GitHub web UI**. Confirm with:

```bash
gh api graphql -f query='{ repository(owner:"nsillik", name:"bonsai") {
  pullRequest(number:2) { stackEntry { position stack { number } } } } }'
```

Also: `branch.master.remote` was left pointing at `upstream` by the fork, so every `gh` command needs
`-R nsillik/bonsai` (or a one-time `gh repo set-default nsillik/bonsai`). `.git/gh-stack` was removed
so the CLI cannot be run accidentally.

### 11. mise's clang package ships no `clang++`

Worth knowing before Phase 5 or any CI change. On Linux, mise installs clang from the conda package,
whose `bin/` contains `clang`, `clang-18`, `clang-cl`, `clang-cpp` — **but not `clang++`**, and
`.mise-bins` does not add one. `make.sh` hardcodes `COMPILER="clang++"`.

It works on `ubuntu-22.04` runners only because the runner image supplies a `clang++` earlier in
`PATH`; `jdx/mise-action` prepends `.mise-bins` and shims, and the fallthrough finds the image's.
That is fragile. If Linux CI ever resolves the wrong compiler, this is why. Phase 5 should either
pin a `clang++` symlink explicitly or switch `make.sh` to `${CC:-clang++}`.

### 12. `run_tests.sh` prints an empty suite count (pre-existing, **not fixed**)

Two bugs in `run_tests.sh`, both upstream, both left alone — but they will mislead a reader of the
test output, so they are recorded here.

```bash
for test_executable in $(find $exe_search_string); do
  if $test_executable $COLORFLAG == 0; then     # <-- $COLORFLAG is undefined
    TESTS_PASSED=$((TESTS_PASSED+1))
...
echo "All Tests ($TESTES_PASSED) Passed"          # <-- typo: TESTES_PASSED
```

- `$TESTES_PASSED` on the final `echo` is a typo for `$TESTS_PASSED`, so the count always prints
  empty. That is why the output reads `All Tests () Passed`.
- `$COLORFLAG` is never set (the real variable is `POOF_COLOR_FLAG`), and `== 0` is passed to the
  test binary as literal argv. Harmless — the binaries ignore it and the `if` uses the **exit
  status** — but it is not the intended invocation.

**The exit code is correct**: `0` iff every suite exits `0`, non-zero counting failures. So
`./make.sh RunTests`'s exit status is a valid gate and Phase 1's 10/10 claim rests on
`ls bin/tests` (10 executables) plus an aggregate exit 0, not on the printed count.

Not fixed because it is shared harness behaviour outside this phase's scope, and `$COLORFLAG` may be
wired up elsewhere. Worth a one-char fix (`TESTES` → `TESTS`) whenever someone is in the file next.

### 13. Phase 2 could not reach its own gate — it needed Phase 3's items 1–5, 7, 8

The plan treats Phase 2 (window + input) and Phase 3 (renderer) as separable, with the gate for
Phase 2 being "`game_loader` opens a window". They are not separable: the renderer fails to
**initialize** on a 4.1 core context, and window creation and renderer init are the same call
(`InitializeBonsaiStdlib` → `OpenAndInitializeWindow` → `InitializeOpenglFunctions` →
`GraphicsInit`). There is no state in which the window exists and the renderer has not run.

Measured chain, from running `./bin/game_loader` at each step:

| # | Blocker | Evidence | Plan owner |
|---|---|---|---|
| 1 | `glMultiDrawArraysIndirect` absent → `Error` → SIGTRAP | `! Error - Couldn't load Opengl function (glMultiDrawArraysIndirect)` | P3 item 7 |
| 2 | `#version 460` unsupported → `Error` → SIGTRAP | `Error - Compiling Shader` in `GraphicsInit` | P3 item 1 |
| 3 | Profiler assert on a coarse cycle counter | `Assert(EndingCycle > StartingCycle)` at `debug.cpp:321` | unplanned, third repo |
| 4 | Empty `case` body segfaults Apple's GLSL linker | SIGSEGV in `glpLLVMCGSwitchStatement` | unplanned |

So Phase 2 landed Phase 3's items 1–5, 7 and 8. What that leaves of Phase 3 is in
[Next session](progress.md#next-session): item 6's world-edit path is converted but untested, item 9 is still
dead code, and the gate (`terrain_gen` + `blank_project` visual parity) is untouched.

Recorded rather than merged quietly because it changes what Phase 3 is. The alternative — landing
Phase 2 with the window gate unmet — was rejected: it would have shipped input mapping that nothing
could exercise, and a "window opens" claim with no window.

### 14. An empty `case` body segfaults Apple's core-profile GLSL compiler

The single largest surprise of Phase 2, and the only blocker that was not in any plan.

`shaders/composite.fragmentshader` had `case 3: {} break;` in a `switch` over a uniform int. Compiling
and linking that shader pair at `#version 410 core` kills `glLinkProgram` with SIGSEGV inside Apple's
own GLSL compiler:

```
libGLProgrammability.dylib  glpLLVMCGSwitchStatement
libGLProgrammability.dylib  glpLLVMCGNode -> glpLLVMCGBlock -> glpLLVMCGNode
libGLProgrammability.dylib  glpLLVMCGFunctionDefinition -> glpLLVMCGTopLevel -> glpLinkProgram
```

Reproduced standalone, deterministically (12/12), with the fragment shader alone — no engine code, no
buffers, no state. Narrowed by stubbing each top-level function in turn: removing any one of the four
segfaulted identically, which is what pointed at a shared construct rather than one function. `switch`
was the shared construct; the delta debugger reduced the 272-line shader to 223 lines with exactly one
`switch` left, the `agxLook` one, with `case 3: {}`.

Facts, all measured:

- **Deterministic.** 12/12 runs.
- **Not version-specific.** Crashes at `#version 330 core` and `#version 410 core` alike.
- **Core-profile only.** The same shader links fine on a legacy 2.1 context.
- **Empty means empty after comments.** `world_edit.fragmentshader` had no syntactically empty case
  bodies at all, and still crashed. Its three triggers were bodies containing only `/* ... */`
  comments: `case 0`, `case 2` and `case 4` of the `BlendType` switch. The comment-stripping scanner
  that found them reported nothing on the first pass because it was retracting braces incorrectly —
  worth knowing before trusting a scanner over a compiler.
- **A statement fixes it.** `Blended = Blended;` / `ColorValue = ColorValue;` / restating the defaults
  in `case 3` keep the behaviour identical and link cleanly. `case 3: break;` (no braces) is *not*
  verified — the control harness for that test was itself broken, so it was dropped rather than
  reported.

Fixed in `shaders/composite.fragmentshader` (1 case) and `shaders/terrain/world_edit.fragmentshader`
(2 empty + 3 comment-only). Total: 6 sites, 2 files. `shaders/composite.fragmentshader:112`,
`shaders/terrain/world_edit.fragmentshader:446,451,521,553,573`.

**There is no compiler diagnostic for this, and the failure mode is a SIGSEGV**, so the guard is
inspection: do not leave a `case` body without a statement on macOS. Noted in both shaders where it
was found, and in [Next session](progress.md#next-session) as gotcha #1.

### 15. Verifying input by driving the user's machine is not acceptable

To confirm the key/mouse mapping, the first attempt built a small tool that posted synthetic events
with `CGEventPost(kCGHIDEventTap, …)` at the user's desktop. It worked — it produced real keycodes
and real mouse events, and the mapping was confirmed with it. It was also wrong, and the user stopped
it.

The engine's input path can be verified by asking the user to press keys and reading the engine's own
log, which is what was done instead and gave strictly better evidence: the log reports the receiving
field's name and its byte offset, so each press is checked against `offsetof()` rather than against a
screenshot. No synthetic input tool, and no Accessibility permission, is needed.

Recorded as a deviation rather than a footnote because the constraint is not obvious from the plan,
and because "verify interactive behaviour" is exactly the kind of instruction that invites it. See
[Next session](progress.md#next-session) gotcha #9.

### 16. The profiler's cycle-counter assertion cannot hold under Rosetta

`debug_timed_function`'s destructor asserted `EndingCycle > StartingCycle`. `GetCycleCount()` is
`__rdtsc()` on this path, and under Rosetta 2 **RDTSC is not a core cycle counter** — it is the 24MHz
system timer. Measured on an M4 Max:

```
rdtsc consecutive-read delta: min_nonzero=41 max=6625 zero_deltas=1747651   (of 2,000,000)
1e6-iter loop: rdtsc delta=3046417  mach_absolute_time delta=3046084
rdtsc/mach ratio = 1.000
```

1.75M of 2M consecutive read *pairs* return identical values, and the smallest non-zero delta is 41
ticks. So any timed region shorter than one tick reads the same value twice, and the assertion fires
on healthy code — it traps in `~debug_timed_function` as soon as the render thread starts doing work.
arm64's `cntvct_el0` (Phase 4) ticks at the same rate, so Phase 4 would hit it too, natively.

→ `bonsai_debug` `b6ceecb` relaxes it to `>=`, which still catches a counter that rewinds — the thing
the assertion was actually protecting — and no longer fires on a counter that is merely coarse.

This lands in a **third fork**, `nsillik/bonsai_debug`, which the plan did not anticipate.

**It is deliberately not a PR.** The diff is one character plus a three-line comment; a third
repository in the review surface is the wrong trade for that, and `AGENTS.md` already says what to do
with a change like this — leave it on a fork branch and say so. `external/bonsai_debug` pins
`nsillik/bonsai_debug` `port/macos-phase2` (`b6ceecb`, based on `master`), and both the fork and the
branch have to keep existing or `git submodule update --init --recursive` breaks for a fresh clone.

The change itself is not optional: without it the engine traps in `~debug_timed_function` as soon as
the render thread starts doing work, on macOS *and* on Phase 4's native arm64.

### 17. `MaxFragShaderTexUnits` is queried and then never used

Not introduced by this work, but adjacent to it, and it matters for the TBO change. `render_loop.cpp`
does `GL->GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &MaxFragShaderTexUnits)` and the value is
unused. `gl.h` does not declare `GL_MAX_TEXTURE_IMAGE_UNITS_ARB`, so that `pname` resolves through
whatever the platform's GL headers happen to define — it works, but it is not a value this tree
controls. Worth either using it to bound the texture-unit budget or deleting the query.

### 18. The brush assets on `master` no longer deserialize, and that is not new

`./bin/game_loader` prints ~55 `Reading Object Delim Failed` / `Deserializing v3i BasisOffset on
layer_settings_2` errors at startup, one per `.brush` in `brushes/`.

**Pre-existing, and not in the port's path.** Established by construction rather than by running the
old code, because Phase 1 traps at the GL loader before reaching any asset:

- `brushes/` is byte-identical between `port/macos-phase1` and `port/macos-phase2`.
- `src/engine/editor.h` (which holds `world_edit_op`, `layer_settings`, `brush_layer`) is byte-identical.
- `generated/serdes_vector_v3i.h` and the other deserializers are byte-identical.
- **12 commits on `master`** changed `src/engine/editor.h` *after* the `.brush` files were last
  written (`e29a657f`, "Finish up porting shape params to SSBO"), and none of them rewrote the assets.

So the serialized layout drifted from the struct on `master`, before this branch existed. Each failed
brush leaves `*CurrentBrush = {}` (`editor.cpp:29`), so the engine runs with empty brushes — which is
why the world editor does not demonstrate anything yet. A regeneration pass over `brushes/` is the
fix; out of scope here, and worth doing before the world-edit path is called verified.

**Correction from the first Phase 3 session, which changes this:** the assets are not the broken
part. See Deviations #21 — the version-shim *reader* is, and it means these files are repairable
rather than needing regeneration, which is not actually available without a working editor anyway.

### 19. `SetVSync` is dead code, and Phase 3 deliberately does not touch it

Confirmed twice more. `SetVSync` (`gl.cpp:36-57`, declared at `gl.h:650`) has no live caller. Both
referencing sites are commented out and were left commented: `initialize.cpp:72` and
`linux_platform.cpp:89`. The Windows side has no call site at all — the repo-wide grep finds
`SetVSync` only in `gl.cpp`, `gl.h`, `initialize.cpp` and `linux_platform.cpp`.

Decision: **leave it.** Three reasons, in order of weight:

1. The macOS swap interval is already set where the context is created
   (`macos_platform.cpp:198-199`, `NSOpenGLCPSwapInterval`, from the same `VSyncFrames` argument
   `initialize.cpp:67` passes). Adding a macOS branch to `SetVSync` would duplicate a value that is
   already honoured, and the plan's Phase 2 conclusion (Deviations #6) stands.
2. Enabling the other two arms would change Linux and Windows frame pacing. That is unverifiable
   here (CI has no display; this machine has no Linux GPU) and it is a behaviour change to platforms
   this port does not own. Upstream's own note at `gl.cpp:41` says vsync was *not* achievable on the
   machine it was tested on, so enabling it is not even known to help.
3. Deleting it would remove two upstream `TODO(Jesse)` markers with stable ids (`gl.cpp:41` id 151,
   `gl.cpp:54` id 368). Nothing else in the tree references those ids, but `AGENTS.md` says
   upstream's markers are upstream's — leave them alone.

Phase 6 replaces the whole function with `VK_PRESENT_MODE_FIFO_KHR`, so its lifetime is bounded and
the upstream TODOs survive it. If `SetVSync` is ever wired up for real, note first that
`SetVSync` calls `AssertNoGlErrors`, which expands to `GetGL()->GetError()` (`gl.h:6-11`) — the
loaded function table, which is zeroed until `InitializeOpenglFunctions()` runs. The commented site
at `linux_platform.cpp:89` sits *before* that, so un-commenting it as-is would dereference a null
function pointer. `initialize.cpp:72` sits after it and is the only correct place.

### 20. `terrain_gen` renders on macOS, and does not match Linux — the open gate

Established with a temporary `glReadPixels` dump of the engine's own back buffer (recipe in the
Phase 3 progress section), not a screenshot. Two frames per platform, taken at the same `FrameIndex`:

| | macOS, frame 400 (3440×1378) | Linux, frame 400 (4096×2160) |
|---|---|---|
| UI | all four debug windows, correct text and layout | same, correct |
| terrain | **streaky slabs at scattered depths, background visible through them** | coherent green plain with yellow foliage, filling the frame |

`terrain_gen` runs without crashing and draws correct UI text, so the 2D path is sound. The 3D scene
is not. The macOS frame at frame 400 shows the same defect as at frame 200, so it is not
initialisation noise.

Two candidates, neither confirmed. The plan's own risk table lists the second one:

1. **The per-draw transform index is wrong on macOS.** If `DrawIndex` does not line up with the
   transform it should read from the TBO, every chunk is drawn with another chunk's matrix — which
   is exactly what streaky misplaced geometry looks like. The plan itself names this risk:
   *"Descriptor-binding model changes (SSBO → TBO) silently drop the transform/edit-op data."*
   Cheapest discriminating experiment: make `MultiDrawIndirect` set `DrawIndexUniform` to a constant
   `0` for every draw. If the image collapses to every chunk drawn with chunk 0's transform in the
   same place as the broken image, the uniform is not reaching the shader on macOS.
2. **The 3-byte vertex fetch** (`v3_u8`, `stride = 0`, `gpu_mapped_buffer.cpp:459-463`). Apple's
   Metal-backed GL may not fetch tightly-packed 3-byte attributes correctly; Linux/Mesa does. This
   is spike 6.0b, previously optional — and note that the crash fixed in Deviations #21 lived in
   `gldRenderVertexArray`, the same code that would do this fetch, so the two symptoms may share a
   cause. Distinguishing test: pad to a 4-byte stride and re-compare. That is a real change (see the
   couplings table under Phase 6 → Blocker 1), so it is not a five-minute experiment.

Neither was confirmed before the session ended. **Do not assume either.** Run experiment 1 first;
it is a one-line edit to `render.cpp` and takes minutes. If it exonerates the transform path, move
to the stride, which is also the Phase 6 prerequisite.

### 21. `glDrawArraysIndirect` crashes Apple's driver — fixed by drawing directly

`terrain_gen` crashed on macOS with SIGSEGV, exit 139, inside Apple's own driver:

```
AppleMetalOpenGLRenderer  GLRResourceList::addResource(GLRResource*)      <- null resource
AppleMetalOpenGLRenderer  GLDContextRec::prepareResourceForGPUAccess(...)
AppleMetalOpenGLRenderer  gldRenderVertexArray(...)
GLEngine                  glDrawArraysIndirect_GL3Exec
terrain_gen_loadable      MultiDrawIndirect(...)                          render.cpp
terrain_gen_loadable      RenderDrawList / DrainHiRenderQueue
game_loader               RenderThread_Main
```

Measured properties:

- **Intermittent, not deterministic.** Always on the *first* draw of a batch, after 1000–1600
  commands have already gone through; 12 crashes in 13 runs of the pre-fix code. Not draw-count
  driven (crashes seen at batch sizes 989 and 1611) and not frame-count driven (crashed at frame 14
  in two runs, survived to frame 176 in another).
- **Reached only via the indirect entry point.** The identical draw issued as `glDrawArrays` never
  crashed, in 8/8 runs.
- **Not buffer orphaning.** Sizing the indirect buffer once and feeding it with
  `glBufferSubData` still crashed (3 of 5).
- **Not the per-draw uniform.** Dropping the `glUniform1i` from the loop still crashed.
- **Not the TBO.** Skipping `BindTextureBuffer` entirely still crashed.
- **Not synchronisation.** A `glFinish` after the loop did not prevent it.

`crashreporter` state at the fault (`$r14 == 0` in `GLRResourceList::addResource`) says the driver
was adding a null resource while preparing vertex-array state for the draw. Root cause inside
Apple's driver was **not** established; what was established is that the indirect path is the only
one that reaches it. `gldRenderVertexArray` is also where the rendering mismatch of Deviations #20
shows up, so the two symptoms may share a root.

Fix: draw directly. `BufferIndirectDrawCommand` (`render.cpp:958`) always writes `InstanceCount = 1`
and nothing reads `BaseInstance` — the shader takes its transform from the `DrawIndex` uniform, the
same thing this loop already sets per draw — so each command is a plain non-instanced draw, and the
indirect buffer was written every frame and read by nothing else. `MultiDrawIndirect` now issues
`glDrawArrays(GL_TRIANGLES, First, Count)` per command. The loop, the per-draw uniform and the
ordering are unchanged; the dead indirect buffer, its `glBufferData` and the now-pointless
4-byte-alignment `static_assert` are deleted, and `gl.cpp:193-198`'s comment updated to match.

Phase 6 restores the single-call form under Vulkan, where `gl_DrawIndex` is 1:1 with the original
`gl_DrawID`.

### 22. Two readback traps, found while building the frame-dump probe

Both are engine state, not probe bugs, and both will bite anyone verifying rendering this way.

1. **`CheckNoiseReadbackJobs` leaves `GL_PIXEL_PACK_BUFFER` bound.** After mapping a readback job it
   never unbinds (`render_loop.cpp:879` binds, `:881` maps, nothing unbinds). The next
   `glReadPixels` with a client-side pointer is then rejected with `GL_INVALID_OPERATION` (0x502)
   and writes nothing — the pixel sums came back zero and identical between frames, which looks
   exactly like "the renderer is drawing nothing". Unbinding explicitly before reading fixes it.
   Worth fixing properly at some point: `UnbindBuffer(GL_PIXEL_PACK_BUFFER)` after `MapBuffer`.
2. **The render pass leaves its own framebuffer bound**, so a readback must `glBindFramebuffer(0)`
   first. Combined with the pack buffer this produced `ReadErr 0x502` on some frames and clean
   reads on others — intermittent-looking, actually state-dependent.

Also: `WriteBitmapToDisk` writes rows as given, and `glReadPixels` returns them bottom-up, so a
correct-looking dump is upside down unless the rows are reversed first.

### 23. Mounting the repo into Docker clobbers the host's `bin/`

`docker run -v "$PWD":/src … ./make.sh` writes into `$PWD/bin`, which is the host's directory. The
first Linux verification run therefore overwrote every macOS binary in `bin/` with ELF objects
(`bin/game_loader` became an ELF x86-64 file) and left `*.so` next to `*.dylib` in `bin/game_libs/`.
Not noticed until a macOS run picked up the wrong binary — the engines are distinguished only by
extension, so both sets coexist and it is easy to run the wrong one.

→ Build Linux in a **copied** tree, not a bind mount of the working repo:

```bash
tar --exclude=.git --exclude=bin --exclude='*.dSYM' -cf - . | (cd /tmp/linuxsrc && tar -xf -)
docker run --rm --platform linux/amd64 -v /tmp/linuxsrc:/src …
```

That keeps `$PWD/bin` untouched and gives the Linux build its own `bin/` as well.

### 23a. The frame dump must **not** reverse its rows (correction to #22)

#22's last paragraph says a correct-looking dump is upside down unless the rows are reversed first.
**It is the other way round.** `WriteBitmapToDisk` emits rows in file order, and BMP stores rows
bottom-up, which is exactly the order `glReadPixels` returns; writing them as given is correct.
Reversing them produces the upside-down file, which is why the first Phase 3 session's frames looked
mirrored and were read as such. Measured, both ways, against a frame whose orientation is known.

### 24. The macOS↔Linux mismatch is Apple fetching 3-byte vertices at a 4-byte stride — FIXED

This is Deviations #20's open gate, resolved. **#20's candidate 1 (large `first` offsets) was
wrong**, and the two isolating experiments it proposed never had to be run: `LoadTransform(0)` and
`first = 0` both draw a *different chunk's mesh*, so those images were never comparable to the real
ones, which is why "forced 0 looks coherent" appeared to mean something it did not.

Established with two standalone offscreen GL 4.1 programs, kept in
`examples/tools/macos_gl_probes/` with a README saying how to build and run them (no window, no
pixels, nothing in `make.sh` — the vertex buffer holds a known pattern, and the vertices the driver
actually fetched are read back through transform feedback):

| idiom | result |
|---|---|
| `GL_BYTE` x3, stride 4, 4-byte-padded data | **0 of 57 (first x count) combinations wrong** |
| `GL_BYTE` x3, stride 0 — tightly packed 3 bytes | **57 of 57 wrong**, from the second vertex on |
| float x3 stride 12 | 0 of 57 |
| engine layout + `matl` via `VertexAttribIPointer` + separate normal buffer | 0 of 57 |
| large `first` (0 … 67,108,863) | innocent |
| per-draw `glUniform1i(DrawIndex)` + `texelFetch(samplerBuffer)` over 1700 draws x 60 frames | innocent, 0 mismatches in 226,936 checks |

With `stride = 0` the driver reads at a 4-byte stride, so every vertex after the first is assembled
from the wrong bytes — `got (0,68,62)` where the data says `(68,56,62)`, a one-byte shift. That is
what turned every chunk mesh into the slivers of #20's table, and why the packed layout renders
correctly on Mesa and incorrectly on Apple.

**Fix** (in `bonsai_stdlib`, three files): `v3_u8` is `alignas(4)` so `sizeof(v3_u8) == 4`, the
element-size table is declared as `sizeof(v3_u8)`, and the two attribute pointers use
`sizeof(v3_u8)` as their stride instead of `0`. Every other `sizeof(v3_u8)` in the tree then means
"4 bytes on the GPU" and needed no edit. Cost is the +20% vertex memory spike 6.0b predicted; the
fourth byte is padding that nothing reads and the meshers do not write.

Two engine-side checks, both of which passed *after* the fix and neither of which was the cause:
the transform TBO is byte-identical to the CPU copy (0 of 1728 matrices differ), and the VAO reports
`size=3 type=GL_BYTE stride=4` for position and normal.

### 25. `terrain_gen` cannot be compared across runs; `macos_smoketest` exists for that

Two things that cost time in this session and will cost it again:

1. **`terrain_gen` is unusable for a frame comparison.** Its chunks stream in on worker threads, so
   how much of the world exists at a given `FrameIndex` depends on how fast the machine is: two runs
   of the *same* binary at frame 400 differed in **45% of the frame's bytes**. Its day/night cycle
   also advances with accumulated wall-clock `dt` (`api.cpp:515`), which changes the lighting between
   runs even at a fixed frame. Dumping "at frame N" is not a pin.
2. **`examples/macos_smoketest/` exists for this.** It defines the world by hand — a
   `chunk_completion_callback` overwrites the engine's noise buffer before voxels are finalized
   (`ChunkCompletionCallbacks`, invoked at `api.cpp:736-750`; bit 31 of each `u32` means "filled",
   the low bits are the voxel's material) — so the scene is a pure function of voxel position. With
   the camera aimed at the pattern, the day/night cycle off and `tDay` pinned, two macOS runs are
   **pixel-identical except a 15-row strip at the top of the frame**, which is the `EngineDebug`
   HUD's live FPS/dT numbers. `tDay` is not free to choose: `UpdateKeyLight` derives the sun's
   direction and colour from it, and most values put the sun below the horizon. Sweeping it and
   measuring the frame's mean luminance gives `0 -> 0.0, 1 -> 14.6, 3 -> 5.3, 5 -> 26.5, 6 -> 7.9`;
   the example pins `5`.

Gate result with that example, macOS (3440x1378) vs Linux (4096x2160, llvmpipe under Xvfb), both
rescaled to a common grid after cropping to a common aspect (`ScreenDim` is the display-clamped
window backing on macOS and the window/back-buffer size on Linux, so the raw grids differ):

| metric | macOS | Linux |
|---|---|---|
| geometry pixels | 12793 (3.9%) | 12558 (3.8%) |
| mean luma of geometry | 106.9 | 106.1 |
| mean RGB of lit geometry | (143, 93, 152) | (141, 92, 150) |
| pixels differing by more than 32 luma | 89 (0.03%) | |

The scene — a ground slab, a three-step staircase, a column and a ridge — renders the same on both.

### 26. The renderer is verified; what is still wrong is the **voxel data source**

**Corrected by #27 — read that first.** The defect was in the terrain-shaping *render target*, not in
the voxel data, and the engine-noise control this entry rests on cannot discriminate (gotcha #18).
The stage table and the readback portability note below are still accurate and were used to find it;
the conclusion and all four candidates are superseded.

The gate's remaining half, and it is not the draw path. Two runs of the *same* example, same camera,
same renderer, differing only in where the voxels come from:

| voxel source | result |
|---|---|
| hand-written in `examples/macos_smoketest/` | the full scene — ground slab, three-step staircase, column, ridge; measured identical to Linux (#25) |
| the engine's own GPU noise (`SMOKETEST_ENGINE_NOISE=1`) | **almost nothing** — a handful of sliver-like fragments in an empty world |

So `terrain_gen`'s landscape of long horizontal shelves with sky between them is a *data* defect, not
a rendering one: the geometry is drawn correctly from voxels that are not what the shading shaders
wrote. It also explains the wrong colours in that screenshot — the material bits are wrong too.

The path, in order, none of it verified today:

| stage | where |
|---|---|
| terrain-shaping shaders render into a ping-pong `RenderToTexture` pair (`RGB32F`) | `render_loop.cpp:655-715` |
| "Terrain Finalize" pass renders into `TerrainFinalizeRC.FBO`, a 2D `GL_R32UI` texture of `TextureDim = 64 x (66*66)` — the chunk volume packed into rows | `render_loop.cpp:723-739`, `render_init.cpp:683,769-770` |
| `ReadPixels(0, 0, TextureDim, GL_RED_INTEGER, GL_UNSIGNED_INT, 0)` into a `GL_PIXEL_PACK_BUFFER`, then a fence, pushed as a `finalize_noise_values` job | `render_loop.cpp:748-768` |
| `CheckNoiseReadbackJobs` maps the PBO | `render_loop.cpp:859-890` |
| voxels finalized from it — **bit 31 = filled**, low bits = material | `FinalizeOccupancyMasksFromNoiseValues`, `world_chunk.cpp:4458` |

Candidates, none confirmed:

1. **The `R32UI` render target and integer readback on Apple's GL.** Everything measured in #24 was
   vertex fetch, attributes, uniforms and texture-buffer fetches; integer-texture rendering and
   `GL_RED_INTEGER`/`GL_UNSIGNED_INT` readback are untouched by it.
2. **The shaping shaders themselves** — 410-core since Phase 2, and they are the only other place a
   texture buffer plus many texture units are used (`Assert(TexUnit <= SHADER_TEXTURE_BUFFER_UNIT)`
   at `render_loop.cpp:695` shows how close that gets to a collision).
3. **Row packing** — probably innocent: rows are 64 texels of 4 bytes = 256 B, already aligned for
   the default `GL_PACK_ALIGNMENT` of 4.
4. **The pack-buffer state leak in #22** (`CheckNoiseReadbackJobs` maps the pack buffer and never
   unbinds it). The readback binds its own PBO first (`render_loop.cpp:761`), so this is probably not
   it — but `glReadPixels(..., 0)` *is* "offset into the bound pack buffer" semantics, so it is worth
   ruling out explicitly rather than by argument.

**Portability note from the same measurements:** on this driver `glReadPixels` reads from
`GL_READ_BUFFER`, which defaults to `COLOR_ATTACHMENT0`; reading any *other* attachment needs an
explicit `glReadBuffer` (hit while reading the gBuffer's `gNormal` and `gPosition`). The noise
readback relies on the default being attachment 0, which holds today.

**Next step:** a third standalone probe, same shape as the two in #24 — render a known pattern into an
`R32UI` attachment the way `render_init.cpp:769` does, read it back the way `render_loop.cpp:762`
does, and compare every texel. That names the stage in one run. If it comes back clean, the next
question is whether `terrain_gen` renders coherently on Linux: if it does, Apple's readback diverges;
if it does not, world generation is broken on both platforms and macOS merely makes it visible.

### 27. The near-empty `terrain_gen` world was a draw-buffer **alias**, not the voxel data — FIXED

The gate's remaining half, found and fixed. **Jesse's hint that `terrain_gen` is the only consumer of
the modern GL features is what framed this, but the cause was neither the GLSL 4.10 downgrade nor the
SSBO/TBO or indirect-draw work: it was a framebuffer-state bug in `render_init.cpp` that any draw
into that target would have hit.**

`terrain_gen` now renders a coherent landscape on macOS — green ground plane with yellow foliage
filling the frame, no sky, no streaks, no slivers. Same content, in kind, as the Linux reference in
#20's table. Dumped from the engine's own back buffer with the #25 pin (both worker queues and the
noise-readback queue empty for 60 consecutive frames, `tDay` fixed at 5).

**Which stage was losing the data.** An in-engine probe (`TEMP(INPROBE)`, since removed) read each
stage of the terrain chain back on the CPU for the first three chunks, reporting exact counts rather
than ranges:

| stage | target | measured |
|---|---|---|
| shaping (`shaders/terrain/shaping/default.fragmentshader`) | 68×4624 `RGBA32F` | **all 314,432 texels written**, alpha 147,968 positive / 166,464 negative, min −2288.0 max 2003.5 — the shader's `1000 − 32z` for `z ∈ [0,67]`, `ChunkResolution = 32`. Byte-identical across runs. |
| derivs (`derivs.fragmentshader`) | 66×4356 `RGB32F` | all 287,496 texels non-zero |
| decoration (`decoration/default.fragmentshader`) | 66×4356 `RGBA32F` | **64 of 287,496 texels written** (252 and 508 on other runs) — everything else still held the clear value exactly |
| finalize (`TerrainFinalize.fragmentshader`) | 66×4356 `R32UI` | bit-31 count **exactly equalled** the decoration's positive-alpha count (64/64, 252/252, 508/508) — so it covered, and it read what the decoration wrote |
| PBO readback → `FinalizeOccupancyMasksFromNoiseValues` | — | the worker's `NoiseSum` matched the same numbers |

So the chain was internally consistent from the finalize pass onward and the defect was one pass
upstream of it: **the decoration quad drew into a target that already held the clear value
afterwards.** Note this also retires #26's candidate 1 — the `R32UI` render, the
`GL_RED_INTEGER`/`GL_UNSIGNED_INT` readback and the PBO path are all correct on this driver.

**What it was not.** Measured, in this order:

| Excluded | Evidence |
|---|---|
| the depth test against a depth-attachment-less FBO | the quad sits at NDC z = 1.0 and `GL_DEPTH_TEST` is enabled at that point, but `glDisable(GL_DEPTH_TEST)` before the draw changed nothing (texels still held the clear value) |
| lost state, or draw ordering | re-stating FBO + `SetViewport` + program + uniform and drawing a *second* time also failed to cover (a different 2.4% of the target) |
| the vertex data | the quad VBO mapped back byte-identical to `g_quad_vertex_buffer_data`, 0 of 18 floats differing |
| viewport, scissor, blend, colour mask, draw buffer 0 | viewport (0,0,66,4356) = the target size, scissor test off, blend off, colour mask all on, `GL_DRAW_BUFFER0` = `COLOR_ATTACHMENT0` |
| any GL error | none, at any point; and the framebuffer reports `GL_FRAMEBUFFER_COMPLETE` |

**What it was.** One line of the draw-buffer list. `FramebufferTexture` (`framebuffer.cpp`) does
`u32 Attachment = FBO->Attachments++;` — it attaches to the *next* free slot. For the decoration pass
the target is `WorldEditRC->Framebuffers[0]`, whose texture `InitializeRenderToTextureFramebuffer`
had already attached to attachment 0; `render_init.cpp` then attached the same texture *again*, to
attachment 1, and `SetDrawBuffers` — which enables `FBO->Attachments` buffers — turned both on. The
same image was therefore bound to two draw buffers.

Measured with the engine's own quad and the tile's real dimensions
(`examples/tools/macos_gl_probes/gl_drawbuffer_alias_probe.cpp`, offscreen, no window):

| configuration | texels written of 287,496 | framebuffer status | GL error |
|---|---|---|---|
| one attachment, 1 draw buffer | 287,496 (100%) | complete | none |
| same texture on `A0` and `A1`, 2 draw buffers — the engine's state | **0 (0.00%)** | complete | none |
| same texture on `A0` and `A1`, 1 draw buffer — the fix | 287,496 (100%) | complete | none |
| two *different* textures on `A0`/`A1`, 2 draw buffers | 287,496 (100%) | complete | none |

Identical on x86_64 under Rosetta 2 and on arm64, so it is Apple's driver rather than the GL client.
Mesa tolerates the alias, which is why Linux rendered correctly and this only ever showed up on
macOS. There is no GL error, no incomplete-framebuffer warning and no shader diagnostic — the same
shape as #14, and now [Next session](progress.md#next-session) gotcha #16.

**Fix:** `TerrainDecorationRC->DestFBO->Attachments = 0;` before the re-attach in
`src/engine/render/render_init.cpp` (Terrain Decoration), which is the idiom `render.cpp`'s RTT group
already uses — it resets the counter for exactly this reason. Engine-side only; no `bonsai_stdlib`
change, so this commit is one repository. The comment carries the failure mode.

**What #26 got wrong, so it is not repeated:** the "engine noise renders almost nothing" control was
read as evidence of a voxel-data defect. After the fix that control *still* shows nothing, because
the engine's default shaping puts the surface at `z ≈ 1000` and the origin chunk comes out
**completely** solid (measured `NoiseSum` 278,784 = 100% of the 64×66×66 interior), with the
smoketest camera inside it — so it cannot discriminate and never could (gotcha #18). `terrain_gen`
was the right place to look and #26's stage table is what made the probe cheap to aim.

**Verified on the committed state**, macOS 26.5.2 / M4 Max / Apple clang 21, x86_64 under Rosetta 2,
with every probe stripped (`git diff` contains no `INPROBE`):

| Check | Result |
|---|---|
| `./make.sh` | exit 0, 0 errors |
| `./make.sh RunTests` | exit 0, 10 test executables |
| `terrain_gen` | 15 s, no trap, no assert, clean SIGTERM |
| `blank_project` | 15 s, no trap, no assert, clean SIGTERM |
| `macos_smoketest` | 10 s, no trap, no assert, clean SIGTERM |
| `gl_drawbuffer_alias_probe` | as above, on both arches |

Not yet done: the Linux run of this commit. The fix is in shared code, and on Mesa the aliased state
was benign, so the expectation is that Linux is unchanged — which is a measurement, not an
assumption, and is the first item in [Next session](progress.md#next-session).

---
