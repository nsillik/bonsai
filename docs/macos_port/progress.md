# macOS Port: Progress, Evidence and Verification

What was built, what was measured, and how to reproduce the measurements.  The plan itself is in
[plan.md](plan.md); the failure catalogue is in [deviations.md](deviations.md); the current status
and remaining work are in `PLAN.md` at the repo root.

---
## Next session

**Phase 3 — renderer runs: in progress.** Items 1–5, 7 and 8 landed in Phase 2 (Deviations #13).
The first Phase 3 session (2026-09-14) landed a renderer-crash fix, set up both `port/macos-phase3`
branches, and established the Linux reference the gate needs. What is left is in
[Phase 3 progress](plan.md#phase-3--renderer-runs-in-progress); the items below, minus what that session
already closed.

What is left of Phase 3, in priority order:

1. **The Linux half of the gate for `terrain_gen`.** macOS now renders a coherent landscape
   (Deviations #27), which was the gate's open half. What has not been done is the *comparison*:
   the macOS frame exists and is described in #27, a Linux frame of the same build does not. The
   fix changes shared code (`render_init.cpp`), so it is also the first thing to re-check on Linux —
   on Mesa the alias was benign, so the expectation is "no change", and that expectation is what
   needs measuring rather than assuming. Use the recipe under
   [Phase 3 progress](plan.md#phase-3--renderer-runs-in-progress), and the same state pin: both worker
   queues and the noise-readback queue drained for ~60 frames, `tDay` fixed, top ~15 rows ignored.
2. **`blank_project` with a frame.** Runs clean, and it does not use the terrain-shaping chain at
   all, so #27 does not change what it does — but it has never been looked at since #24 and it is
   the cheapest remaining item.
3. Item 6 — the world-edit path. Blocked on brush assets, but the root cause is a **reader bug, not
   stale assets** (#18/#21): the version-shim structs are wrong, so the files are repairable, and
   regeneration is not actually possible the way #18 assumed.
4. Item 9 — `SetVSync`, dead code with no live callers. Decision made, recorded as #19.
5. Optional, and now cheap: **spike 6.0b** has only its measurement half left (frame time and heap
   bytes per unit of world, `terrain_gen` at 1920x1080). The padding itself is landed and load-bearing
   (#24), so the "win or only a cost?" question is now about the 20% vertex memory, not about whether
   to do it at all.

Branches already exist locally, created per the original recipe. **Do not re-run it**, and do not
expect a PR yet — none exists (see [Branches and PRs](#branches-and-prs)):

```bash
cd external/bonsai_stdlib && git fetch origin && git checkout -b port/macos-phase3 origin/port/macos-phase2
cd ../..                   && git fetch origin && git checkout -b port/macos-phase3 origin/port/macos-phase2
```

`bonsai_debug` is not in the stack and has no PR — only branch it if Phase 3 turns out to need a
change there, and leave that on the fork branch too (Deviations #16).

Gotchas that cost time in Phase 1, 2 and the first Phase 3 session, in order of how expensive they
were:

1. **Apple's core-profile GLSL compiler segfaults on an empty `case` body.** `case 3: {} break;`
   inside a `switch` in a `#version 410 core` shader kills `glLinkProgram` with SIGSEGV in
   `glpLLVMCGSwitchStatement`. A comment-only body counts as empty. This is the single largest
   surprise of Phase 2 and it cost more time than everything else combined — see Deviations #14.
   Before adding any `switch` to a shader, look at the existing ones in
   `shaders/composite.fragmentshader` and `shaders/terrain/world_edit.fragmentshader`.
2. **`glDrawArraysIndirect` crashes Apple's driver.** Intermittent SIGSEGV in
   `gldRenderVertexArray` under Rosetta, reached only through that entry point. Fixed in Phase 3
   by drawing directly — Deviations #21 has the isolation table.
3. **Build on Linux before pushing anything that touches a shared header.** `uintptr_t` compiled
   cleanly on macOS and broke Linux CI with 18 errors. Docker recipe is under
   [Phase 1 progress](#phase-1--makesh-completes-done) — `--platform linux/amd64` is required.
   **And build Linux in a copied tree**: mounting the repo with `-v $PWD` points the container's
   `./bin` at the host's, so a Linux build silently overwrites the macOS binaries (Deviations #23).
4. **A GLSL version bump is a hard stop, not a warning.** `CheckShaderCompilationStatus` calls
   `Error`, which traps the process. So any shader that fails to compile takes the engine down at
   startup rather than degrading.
5. **Never write `Jesse` in a comment you author** — that is the upstream maintainer. Use
   `NOTE(nsillik)` / `TODO(nsillik)`. See `AGENTS.md`.
6. **Never push or PR to `scallyw4g/*`.** Verify with
   `gh pr list -R scallyw4g/bonsai --author=@me --state=open`.
7. **Do not run `gh stack`** — it resolves to the parent repo and will try to open upstream PRs.
8. **Pass `-R nsillik/bonsai`** to `gh` commands; `branch.master.remote` points at `upstream`.
9. macOS builds are x86_64 and run under Rosetta 2. `ARCH` is the **target** arch, not the host arch.
10. **Driving the machine to verify input is a user-visible act.** Do not post synthetic events to
    the user's desktop; ask them to press keys and read the engine's log.
11. **Engine log output is buffered and is lost on a trap.** `PrintToStdout` uses `fwrite` into a
    `FILE*`, and `log.txt` likewise, so a SIGTRAP/SIGSEGV loses the tail. Any per-frame probe must
    write to stderr and `fflush` — an `Info()` tail that looks like it says where the crash was is
    just wherever the buffer happened to flush. This wasted a round of crash localisation.
12. **Do not compare frames across runs of `terrain_gen`** — chunk meshing is async, so frame 400 is
    a different scene on a fast machine than on a slow one (measured: 45% of the bytes differ between
    two runs of the same binary), and its day/night cycle advances with wall-clock `dt`. Use
    `examples/macos_smoketest/` for parity work, and remember that `tDay` must be *measured* for
    brightness, not guessed (Deviations #25).
13. **A frame dump needs a *state* pin, not a frame index, and the probe is not in the tree.**
    `terrain_gen` looks different on every run at frame 400; dump only when both work queues have been
    drained for ~60 frames and `tDay` is fixed. Re-add the dump block per the verification method
    below rather than assuming it is still there (Deviations #25).
14. **`docker run` in a tool call dies with the call, and the Linux run script's `cp` never fires.**
    Start the container detached, and `docker cp` the frame out while it is still running — the engine
    does not exit on its own. Cache the apt layer in an image; a repeat Linux run is then build-only.
15. **`screencapture` of the whole desktop captures the user's private content.** The engine can
    dump its own back buffer instead: `glReadPixels` + `WriteBitmapToDisk` (`bitmap.cpp:203`), read
    back on the render thread just before `BonsaiSwapBuffers`. It needs `glBindFramebuffer(0)` and
    an explicit `glBindBuffer(GL_PIXEL_PACK_BUFFER, 0)` first — see Deviations #22 for both traps.
16. **A framebuffer's `Attachments` counter is not idempotent.** `FramebufferTexture` attaches to the
    *next* free slot and increments it, so calling it twice on the same FBO attaches the same texture
    to two attachment points, and `SetDrawBuffers` then enables both. Apple's GL drops the draw
    entirely, with no GL error and a `FRAMEBUFFER_COMPLETE` status; Mesa does not. Reset
    `FBO->Attachments = 0` before re-attaching, as `render.cpp`'s RTT group does. Full failure mode in
    Deviations #27 — this is gotcha #1's shape: silent, and only on macOS.
17. **A probe that leaves a GL error in the queue trips the engine's `AssertNoGlErrors`**, which calls
    `Error`, which `RuntimeBreak`s — killing the render thread before any frame is dumped. My own
    readbacks did this. Pop the whole queue (`while (GetError()) {}`) after each probe call, and
    expect that the engine only ever looks at the first error.
18. **The `SMOKETEST_ENGINE_NOISE=1` control is not a control.** The engine's default shaping shader
    puts the surface at `z ≈ 1000` with the origin chunk *entirely* solid (measured: `NoiseSum`
    278,784 of the 64x66x66 interior), and the smoketest camera is inside it, so nothing is visible
    whether or not rendering works. Deviations #26 read "almost nothing" from this and drew a
    conclusion it cannot support.

---


## Progress

### Repository layout

All work happens in forks. **Nothing is pushed to `scallyw4g/*`** — verified: zero branches matching
`port/` on either upstream, and no *open* PR authored there.

One correction to that attestation, found when the phase 3 branches were pushed: there **is** a
closed PR on `scallyw4g/bonsai` — [#8](https://github.com/scallyw4g/bonsai/pull/8), "Add posix stubs
for `PlatformInitializeAudio` and `PlatformPinCurrentThreadToCore`", from `nsillik:port/macos`, opened
and closed on 2026-09-14 before this session. It is closed, nothing is open against upstream, and
`AGENTS.md` says not to touch anything owned by someone else, so it is recorded here and left alone.
`scallyw4g/bonsai_stdlib` has no PRs at all.

| Remote | `bonsai` | `external/bonsai_stdlib` |
|---|---|---|
| `origin` | `nsillik/bonsai` | `nsillik/bonsai_stdlib` |
| `upstream` | `scallyw4g/bonsai` | `scallyw4g/bonsai_stdlib` |

`.gitmodules` uses relative URLs (`../bonsai_stdlib.git`), which resolve against the **fork**, so
`git submodule update --init --recursive` requires fork copies of `bonsai_stdlib` *and*
`bonsai_debug`. Both exist. The stale `scripts` entry has no gitlink in the tree (it is a plain
directory), so `git submodule update` ignores it.

### Branches and PRs

Stacked bottom-up, one branch per phase, in the two repos that hold reviewable changes:

```
master
 └─ port/macos               bonsai#1  stdlib#1   Phase 0
     └─ port/macos-phase1    bonsai#2  stdlib#2   Phase 1
         └─ port/macos-phase2  bonsai#4  stdlib#3  Phase 2
```

| Repo | Branch | Head | PR |
|---|---|---|---|
| `bonsai` | `port/macos` | `b7ef48c7` | [#1](https://github.com/nsillik/bonsai/pull/1) |
| `bonsai` | `port/macos-phase1` | `922c1dcf` | [#2](https://github.com/nsillik/bonsai/pull/2) |
| `bonsai` | `port/macos-phase2` | `11e2502f` | [#4](https://github.com/nsillik/bonsai/pull/4) |
| `bonsai_stdlib` | `port/macos` | `6b80224` | [#1](https://github.com/nsillik/bonsai_stdlib/pull/1) |
| `bonsai_stdlib` | `port/macos-phase1` | `82974dc` | [#2](https://github.com/nsillik/bonsai_stdlib/pull/2) |
| `bonsai_stdlib` | `port/macos-phase2` | `a7d2dd3` | [#3](https://github.com/nsillik/bonsai_stdlib/pull/3) |
| `bonsai` | `port/macos-phase3` | `582814b7` | **pushed, no PR yet** |
| `bonsai_stdlib` | `port/macos-phase3` | `930f51e` | **pushed, no PR yet** |
| `bonsai_debug` | `port/macos-phase2` | `b6ceecb` | **none — deliberate** |

**`bonsai_debug` is a third fork, and is deliberately not a PR.** Phase 2 needed one change there
(Deviations #16) — a one-character assertion relaxation. That does not warrant a third repository in
the review surface, so per `AGENTS.md` ("if a change belongs upstream, leave it on a fork branch and
say so") it stays on the fork branch. `external/bonsai_debug` is pinned at
`nsillik/bonsai_debug` `port/macos-phase2`, and **`nsillik/bonsai_debug` must exist** for
`git submodule update --init --recursive` to resolve. The branch must stay too: the gitlink points at
it, so deleting it breaks a fresh clone.

If that commit is ever wanted upstream, it is `b6ceecb` on `port/macos-phase2`, based on `master`.

Linked as GitHub native Stack **#3** (done by hand in the web UI — see Deviations). Merge
bottom-up; `external/bonsai_stdlib` is pinned at the fork's `port/macos-phase2` (`a7d2dd3`) and must
be re-pointed at upstream `master` if these PRs are ever accepted upstream.

### Phase 0 — green base: done

`bonsai` `b7ef48c7`, `bonsai_stdlib` `6b80224`. Same fixes as specified above, plus:

- `.github/workflows/build.yml` — `pull_request.branches` was `["develop"]`, so a PR against the
  default branch (`master`) started **no jobs at all**. This is not in the plan but is required to
  observe Phase 0's gate from a PR. Later removed entirely (see Phase 1).
- `src/engine/engine_resources.h:182` — uncommented the `Input` binding. One line fixes all 13
  Windows sites.

CI [`34856272505`](https://github.com/nsillik/bonsai/actions/runs/34856272505): `build-ubuntu-22`
and `build-and-release-windows` both **success**. First green CI since 2025-12-17.

### Phase 1 — `./make.sh` completes: done

`bonsai` `922c1dcf`, `bonsai_stdlib` `82974dc`. Gate **exceeded**: the plan asked that
`RunTests` merely *run*, with failures deferred to Phase 2. It passes 10/10 suites on both
platforms.

Everything in the Phase 1 section below was implemented as written, except that the macOS platform
backend landed here rather than in Phase 2 — see Deviations.

CI [`34860403419`](https://github.com/nsillik/bonsai/actions/runs/34860403419), all three jobs green:

```
build-ubuntu-22            success   (Tar/Release skipped: tag-gated)
build-and-release-windows  success
build-macos                success   (new)
```

Local, from a clean tree (`rm -rf bin`):

| Platform | Build | Tests | Products |
|---|---|---|---|
| macOS 26.5.2 / M4 Max, Apple clang 21 | exit 0 | exit 0, 10/10 suites | x86_64 Mach-O, run under Rosetta 2 |
| Linux x86_64, clang 18 (Ubuntu 24.04) | exit 0, 18/18 targets | exit 0 | x86_64 ELF |

Also verified on macOS: `mise run build` and `mise run test` exit 0; `mise ls clang` is empty and
`mise bootstrap packages apply --dry-run` reports no packages, confirming the `os = ["linux"]` filter
is honoured.

**Reproducing the Linux check.** `docker run --platform linux/amd64 ubuntu:24.04`, then
`apt install clang-18 libx11-dev freeglut3-dev` and `PATH=/usr/lib/llvm-18/bin:$PATH`. The
`--platform linux/amd64` is load-bearing on an arm64 host: the default pull gets an
`aarch64-conda-linux-gnu` clang, which hard-errors on `-mssse3 -mavx -mavx2 -mfma` for reasons
unrelated to the code.

### Phase 2 — window opens, tests pass: done

`bonsai` `11e2502f`, `bonsai_stdlib` `a7d2dd3`, `bonsai_debug` `b6ceecb`. Gate **met**: `./bin/game_loader`
opens a window, renders terrain and UI, and exits 0.

Local, macOS 26.5.2 / M4 Max, Apple clang 21, x86_64 under Rosetta 2:

| Check | Result |
|---|---|
| `./make.sh` from a clean tree | exit 0, 0 errors |
| `./make.sh RunTests` | exit 0, 10/10 suites |
| `./bin/game_loader` | window opens, terrain + UI render, exit 0 on close |
| Linux `./make.sh`, Docker `--platform linux/amd64`, clang 18 (Ubuntu 24.04) | exit 0 |

Input was verified by reading what the engine actually received, not by inspection. `offsetof()`
against the real `input` struct gives the expected byte offset of every field, and each key that was
pressed landed on exactly the right one:

| Key | keyCode | Field | Offset | | Key | keyCode | Field | Offset |
|---|---|---|---|---|---|---|---|---|
| `W` | 13 | `W` | 660 | | `.` | 47 | `Dot` | 228 |
| `A` | 0 | `A` | 396 | | `/` | 44 | `FSlash` | 252 |
| `Z` | 6 | `Z` | 696 | | `return` | 36 | `Enter` | 0 |
| `5` | 23 | `N5` | 336 | | `esc` | 53 | `Escape` | 12 |

Shift, Control and Option each set only their own field (`raw=0x20102`→shift, `0x40101`→ctrl,
`0x80120`→alt) and cleared on release. LMB, RMB and MMB down/up all reached their fields. Scroll read
`scrollingDeltaY` and scaled a line-based wheel by 120, matching Linux/win32. A resize drag produced a
continuous stream of `ScreenDim` updates (982×564 → 891×465) with the render thread still drawing.
`esc` and the close button both exited 0.

Two notes on the verification method: the input checks were done with temporary `Warn` logging in
`macos_platform.cpp`, since removed (the committed diff contains no `INPROBE`); and the first
attempt at it posted synthetic CGEvents to the user's desktop, which is not acceptable — see
Deviations #15.

**Not verified in Phase 2**, and the reason Phase 3 is still open: the world-edit path (a brush
actually editing terrain) has never run, and `terrain_gen`/`blank_project` have not been launched.

---

### Phase 3 progress: third 2026-09-15 session

`bonsai` `582814b7`; `bonsai_stdlib` unchanged at `930f51e` (the fix is engine-side only).
**Pushed; no PRs.**

This session found and fixed the near-empty `terrain_gen` world, which was the open half of the gate.
The cause was not the GLSL 4.10 downgrade, the SSBO→TBO conversion or the indirect-draw replacement:
the terrain decoration render target had one texture bound to two draw buffers, and Apple's GL
**silently discards every draw into it** — no GL error, framebuffer complete (Deviations #27).

| Check | Result |
|---|---|
| `./make.sh` | exit 0, 0 errors |
| `./make.sh RunTests` | exit 0, 10 test executables |
| `terrain_gen`, frame dumped with the #25 pin | coherent green ground plane with yellow foliage, filling the frame — no sky, no streaks, none of #20's slivers |
| `terrain_gen` / `blank_project` (15 s) / `macos_smoketest` (10 s) | no trap, no assert, clean SIGTERM |
| `examples/tools/macos_gl_probes/gl_drawbuffer_alias_probe.cpp`, x86_64 + arm64 | 0 of 287,496 texels written with the alias; 287,496 of 287,496 with one draw buffer or with two distinct textures |

The stage-by-stage readback that localised it — shaping, derivs, decoration, finalize and the PBO
path, each read back on the CPU — is in #27's tables, and the four exclusions (depth test, lost
state, vertex data, all the ordinary state) are there too.

**Not done, and the first item next session:** the Linux run of this commit. The fix touches shared
code, and on Mesa the aliased state was benign, so the expectation is "unchanged on Linux" — which
has to be measured, not assumed.

### Phase 3 progress: second 2026-09-14 session

`bonsai` `05eb39b5`, `c4832943`; `bonsai_stdlib` `930f51e`. **Neither pushed; no PRs.**

This session found and fixed the rendering mismatch that was the gate (#24), built the deterministic
scene the comparison needs (#25), and established that what is *left* of the mismatch is the voxel
data source rather than the renderer (#26).

| Check | Result |
|---|---|
| `./make.sh` | exit 0, 0 errors |
| `./make.sh RunTests` | exit 0, 20 suites |
| `terrain_gen`, `blank_project`, `macos_smoketest` | each runs to a clean SIGTERM, no trap, no GL errors |
| two macOS smoketest runs | pixel-identical apart from a 15-row strip at the top (the HUD's FPS/dT text) |
| macOS vs Linux, smoketest | geometry 12793 vs 12558 px, mean luma 106.9 vs 106.1, mean RGB (143,93,152) vs (141,92,150), 0.03% of pixels differing by >32 luma |

Adding `examples/macos_smoketest/` to `make.sh`'s `BUNDLED_EXAMPLES` is what makes it build with the
rest; it has no `assets/` directory and needs none.

### Phase 3 progress: first 2026-09-14 session

Branches `port/macos-phase3` created off `port/macos-phase2` in both repos and committed:
`bonsai` `bcae541c`, `bonsai_stdlib` `1d3144d`. **Neither pushed; no PRs.** Nothing else is pending
in either tree.

Verified on the committed state, macOS 26.5.2 / M4 Max / Apple clang 21, x86_64 under Rosetta 2:

| Check | Result |
|---|---|
| `./make.sh` | exit 0, 0 errors, 18 targets |
| `./make.sh RunTests` | exit 0, 10/10 suites |
| `./bin/game_loader ./bin/game_libs/terrain_gen_loadable.dylib` | runs without crashing, 10/10 attempts to a 20 s timeout |
| `./bin/game_loader ./bin/game_libs/blank_project_loadable.dylib` | runs without crashing, 3/3 to a 40 s timeout |
| `./bin/game_loader ./bin/game_libs/project_and_level_picker_loadable.dylib` | runs without crashing, 2/2 to a 40 s timeout |
| Linux `./make.sh`, Docker `--platform linux/amd64`, clang 18 | exit 0, all targets |

The crash that was fixed is Deviations #21. What remains is the gate, and the gate currently
**fails**: macOS and Linux render differently. That is the open work, not a regression introduced
here — it was invisible until this session, because `terrain_gen` crashed before it could render.

### Verification method (what the new session needs to reproduce)

**The dump probe is not in the tree** — it was removed before committing, so it has to be re-added
for any frame comparison. It also has to be *pinned*: dumping at a frame index alone is not a pin
(#25). What worked:

- dump only when `Plat->HighPriority` and `Plat->LowPriority` have had `EnqueueIndex == DequeueIndex`
  for ~60 consecutive frames, so the async mesh work is done;
- pin `Graphics->Settings.Lighting.tDay`, because the day/night cycle advances with wall-clock `dt`
  and changes the lighting between runs;
- normalise `ScreenDim` before comparing platforms (macOS reports the display-clamped window backing,
  Linux the window/back-buffer size);
- ignore the top ~15 rows, which are the `EngineDebug` HUD's live numbers.

The engine can dump its own composed back buffer. Add a temporary block on the render thread in
`RenderThread_Main`, immediately before `BonsaiSwapBuffers` (`render_loop.cpp:990`):

```c
// TEMP(INPROBE): dump the composed back buffer, for visual comparison.
v2i Dim = V2i(Plat->ScreenDim);
u32 PixelCount = u32(Dim.x) * u32(Dim.y);
u32 *Pixels = Allocate(u32, GetTranArena(), PixelCount);

// CheckNoiseReadbackJobs leaves GL_PIXEL_PACK_BUFFER bound (render_loop.cpp:879, never
// unbound). With a pack buffer bound, glReadPixels treats the pointer as an offset into it
// and rejects it with GL_INVALID_OPERATION, writing nothing.
GetGL()->BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
GetGL()->BindFramebuffer(GL_FRAMEBUFFER, 0);
GetGL()->ReadPixels(0, 0, Dim.x, Dim.y, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);

bitmap Bitmap = {};
Bitmap.Dim = Dim;
Bitmap.Pixels = U32Cursor(Pixels, Pixels + PixelCount);
WriteBitmapToDisk(&Bitmap, "/tmp/INPROBE_frame.bmp");
```

Convert with `sips -s format png …` and read the PNG. **Write the rows as given** —
`WriteBitmapToDisk` emits them in file order and BMP stores rows bottom-up, which is the order
`glReadPixels` returns; reversing them produces the upside-down file (measured both ways, Deviations
#23a, which corrects an earlier claim in this section). Log `GetError()` before and after — a stale
error from earlier in the frame is not from this call.

Two corrections to the snippet above, both hit while using it:

- `Allocate(u32, GetTranArena(), PixelCount)` **cannot work**: a thread's temp arena is 1 MB
  (`DefaultThreadLocalState`, `thread.cpp`) against a 19 MB frame, and `Allocate` will `Error`. Use a
  scratch arena with the size asked for: `Allocate(u32, AllocateArena(Megabytes(64)), PixelCount)`.
- The probe must not leave a GL error in the queue — an engine `AssertNoGlErrors` after it calls
  `Error` and `RuntimeBreak`s, killing the render thread before the frame is written. Pop the whole
  queue after each probe call (gotcha #17).

Two things this method surfaced, both worth knowing before the next attempt:

- The probe must `fflush` stderr, not `Info()`. Engine log output goes through `fwrite` into a
  buffered `FILE*` (`file.cpp:359-380`), and a trap loses it — see gotcha #11 in the Next session.
- macOS dumps at the window's *backing* size (3440×1378 on the host, i.e. a 1720×689 window at 2×
  Retina). Linux dumps at `settings.init`'s resolution. Parity therefore means "same content",
  not "same pixel grid"; the resolution mismatch is not a bug.

For the Linux reference, build in a **copied** tree and run under Xvfb with the GL version forced:

```bash
tar --exclude=.git --exclude=bin --exclude='*.dSYM' -cf - . | (cd /tmp/linuxsrc && tar -xf -)
docker run --rm --platform linux/amd64 -v /tmp/linuxsrc:/src -v /tmp/linuxbuild:/scripts \
  -v /tmp/linuxout:/out ubuntu:24.04 bash /scripts/run.sh
# inside: apt install clang-18 xvfb libgl1-mesa-dri libglx-mesa0 libx11-6 libglu1-mesa
#          mesa-utils bsdextrautils
#   apt install the same in the build container (see Phase 1 recipe for the compiler bits)
#   PATH=/usr/lib/llvm-18/bin:$PATH ./make.sh
#   LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=4.6 MESA_GLSL_VERSION_OVERRIDE=460 \
#     Xvfb :99 -screen 0 1920x1080x24 &   DISPLAY=:99 ./bin/game_loader …
```

Practicalities that cost time in the second session, none of them obvious:

- **The run script's `cp` never executes.** The engine does not exit on its own, so anything after the
  `game_loader` invocation waits for the script's `timeout` to fire. `docker cp bonsai-linux:/tmp/INPROBE_frame.bmp /tmp/linuxout/`
  while it is still running is the way to get the frame.
- **Cache the apt layer.** A `Dockerfile` from `ubuntu:24.04` that installs the packages once, tagged
  and reused, turns a repeat Linux run from ~10 minutes into a build-only one. The tree is bind
  mounted, so the built `bin/` survives between containers and the build can be skipped entirely.
- **Start the container detached** (`docker run -d --name …`). A foreground `docker run` from a tool
  call dies with the call.
- **It is slow.** Software rendering under emulated `linux/amd64`: ~180 frames took about four minutes
  in the second session's run, versus a second on the host.

Without `MESA_GL_VERSION_OVERRIDE=4.6` **and** `MESA_GLSL_VERSION_OVERRIDE=460`, llvmpipe reports
GL 4.5, `#version 460` fails to compile, and `DepthRTT.vertexshader|DepthRTT.fragmentshader` dies
at `Linking shader pair` → `Assert` in `InitializeShadowRenderGroup` (`shadow_map.cpp:30`) → SIGTRAP
with an empty log. Both overrides are required; `MESA_GL_VERSION_OVERRIDE` alone is not enough.
That failure is an artefact of software rendering, not a code problem — the plan's Linux target is
real hardware, and this recipe is the closest proxy available on this machine.

### What remains

| # | Change | Note |
|---|---|---|
| — | **Linux run of the #27 fix** | The gate's other half, and the first thing to do: the fix is shared code, so re-measure `terrain_gen` on Linux with the same pin. Expectation is "unchanged", and on Mesa the aliased state rendered correctly, so a regression here would be surprising — which is exactly why it needs measuring |
| — | `blank_project` visuals | Runs clean; does not use the terrain-shaping chain, so #27 does not change it. Never looked at *with a frame* since #24 |
| 6 | World-edit path | Blocked on brush assets; root cause is the version-shim reader, not staleness (#18/#21) |
| 9 | `SetVSync` | Decision made, no code change (#19). Phase 6 replaces it |
| — | Engine log output lost on a trap, and `GL_PIXEL_PACK_BUFFER` left bound | Both hit while building the frame-dump probe (#22) |
| — | `FBO->Attachments` is a counter, not a set | The #27 trap. Only one other re-attach site exists (`render.cpp`, which resets it) — worth a grep before any new framebuffer setup |
| — | Spike 6.0b | Only its measurement half is left — the padding itself is landed and load-bearing (#24). Frame time and heap bytes per unit of world, `terrain_gen` at 1920x1080 |

### Gate

`terrain_gen` and `blank_project` render, visually matching Linux. **Both halves are now met on
macOS**: the renderer (#25 — the smoketest scene is identical across platforms, and #24 fixed the
vertex fetch), and `terrain_gen` itself, which renders a coherent landscape from the engine's own
noise (#27 — that was the draw-buffer alias, not the voxel data as #26 concluded). What is left is
the other side of the comparison: a Linux run of this commit, which is the first item in
[Next session](#next-session). `blank_project` runs clean but has not been looked at with a frame
since #24. Shader hot-reload still works (Phase 2 verified it for the game lib; the terrain-shader
picker window is live in `terrain_gen` and reloads on click).

Optional, and the cheapest moment to do it: **spike 6.0b** (vertex-format stride padding). It needs
nothing from Phase 6 and both strides are legal in GL, so this is the earliest point at which the
"is it a win or only a cost?" question can be answered with real numbers — and `terrain_gen` now
renders, which is the precondition it was waiting on. Spec is under Phase 6 → Blocker 1.

---
