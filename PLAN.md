# macOS Port — status

Bonsai builds, opens a window, takes input and renders on macOS. Phases 0–3 are landed on one
branch per repo; Phase 4 (native arm64) has not started.

| | |
|---|---|
| `bonsai` | `port/macos-phases-0-3`, PR [nsillik/bonsai#5](https://github.com/nsillik/bonsai/pull/5) → `master` |
| `bonsai_stdlib` | `port/macos-phases-0-3`, PR [nsillik/bonsai_stdlib#5](https://github.com/nsillik/bonsai_stdlib/pull/5) → `master` |
| Submodule pins | `external/bonsai_stdlib` → the same branch; `external/bonsai_debug` → `nsillik/bonsai_debug` `port/macos-phase2` (`b6ceecb`, deliberately not a PR) |

These two PRs are the whole port through Phase 3, and supersede the per-phase stack
(`port/macos` #1, `port/macos-phase1` #2, `port/macos-phase2` #4 here; #1–#3 in the stdlib), which is
untouched and can be closed once these land. Nothing is pushed to `scallyw4g/*`.

## Documents

| | |
|---|---|
| [docs/macos_port/plan.md](docs/macos_port/plan.md) | The plan: decisions, the measured research it rests on, phases 0–6 with their gates, risks |
| [docs/macos_port/progress.md](docs/macos_port/progress.md) | What was built and measured, per session, plus the verification recipes (frame dump, Linux reference build) and the gotcha list |
| [docs/macos_port/deviations.md](docs/macos_port/deviations.md) | Every place implementation differed from the plan, #1–#27, referenced as `#<n>` from the code |

## What is left in Phase 3

1. **Linux run of the renderer commits.** `render_init.cpp` (the draw-buffer alias fix) and the FBO
   attachment change are shared code; on Mesa the aliased state was benign, so the expectation is
   "unchanged on Linux". That is a measurement, not an assumption, and it is the last thing the
   Phase 3 gate needs. Recipe: [progress.md](docs/macos_port/progress.md).
2. **`blank_project` with a frame.** Runs clean; never looked at with a frame since the vertex-fetch
   fix. It does not use the terrain-shaping chain, so the recent fixes should not touch it.
3. **Item 6, the world-edit path.** Never executed a brush against terrain. Blocked on brush assets,
   and the root cause is a reader bug in the version-shim structs, not stale files (`#18`, `#21`) —
   the `.brush` files are repairable.
4. **Item 9, `SetVSync`.** Decision recorded, no code change (`#19`); Phase 6 replaces it.
5. **Spike 6.0b, measurement half only.** The `v3_u8` padding it asked for is landed and load-bearing
   (`#24`); frame time and heap bytes per unit world at 1920×1080 have not been compared.

## What the cleanup pass changed

Landed on the two branches after the port itself, in response to review:

- **`FramebufferTexture` cannot alias an image across two draw buffers any more** (`framebuffer.h`).
  `framebuffer` records which image occupies each slot, so re-attaching one is a no-op instead of a
  second attachment. This was the #27 failure mode — Apple's GL silently discards every draw into
  such a target, with no GL error — and it is now unreachable rather than avoided at one call site.
  `render_init.cpp`'s local reset and the "next free slot" counter idiom are gone.
- **The draw list carries only what is drawn.** `DrawArraysIndirectCommand` is replaced by
  `draw_arrays_command` (`First`, `Count`); `InstanceCount` and `BaseInstance` were written and never
  read. `MultiDrawIndirect` is now `SubmitDrawList`, matching what it does.
- **`CheckNoiseReadbackJobs` unbinds `GL_PIXEL_PACK_BUFFER`** after mapping, so a later
  client-pointer `glReadPixels` is not a silent `GL_INVALID_OPERATION` (`#22`).
- **`RuntimeBreak` flushes stdout and `log.txt` before it traps** (`FlushStdout`). Measured: without
  it a trap leaves `log.txt` **empty** — not merely truncated — because the log file is a buffered
  `stdio` stream (`#22`, gotcha #11).
- Deleted: the unused `MaxFragShaderTexUnits` query (`#17`), the duplicated `FramebufferTexture`
  declaration, and the smoketest's `SMOKETEST_ENGINE_NOISE=1` control, which could not discriminate
  (`#26`, `#27`).
- Fixed `run_tests.sh`: the suite count printed empty (`$TESTES_PASSED`) and the color flag was the
  wrong variable (`$COLORFLAG`), with `== 0` passed as literal argv.
- Corrected the platform claims in `readme.md` and `docs/00_getting_started.md`; dropped the
  `.gitmodules` stanza for `scripts`, which has no gitlink in the tree.
- Split the 2000-line `PLAN.md` into this status file plus the three documents above.

## Gotchas

The full list, with the measurements behind each, is in
[progress.md](docs/macos_port/progress.md#next-session). The ones that cost the most and are easiest
to re-hit:

- **Apple's core-profile GLSL compiler segfaults on an empty `case` body** — a comment-only body
  counts as empty, and there is no diagnostic. `#14`.
- **A framebuffer state bug that is silent on macOS, benign on Mesa, and has no GL error** — the
  aliasing case above. `FramebufferTexture` now makes it unrepresentable; the general lesson is not
  to trust a `GL_FRAMEBUFFER_COMPLETE` as evidence that a draw reached the target. `#27`.
- **A clean macOS build is not evidence that a shared-header change is portable.** Any edit to
  `posix_platform.{h,cpp}` or a platform header must be built on Linux before it is pushed — and
  Linux must be built in a **copied** tree, or the container's `./bin` overwrites the host's macOS
  binaries (`#8`, `#23`).
- **`terrain_gen` cannot be compared across runs**; `examples/macos_smoketest/` exists for that, and a
  frame dump needs the state pin, not a frame index (`#25`).
- **Never write `Jesse` in a comment you author, and never push or PR to `scallyw4g/*`.** See
  `AGENTS.md`.

## Verifying

```bash
git submodule update --init --recursive
./make.sh && ./make.sh RunTests
./bin/game_loader ./bin/game_libs/terrain_gen_loadable.dylib
```

Green as of the cleanup commit: `./make.sh` exit 0 with 0 errors, `./make.sh RunTests` exit 0 with
10 test executables passing, `terrain_gen` rendering a coherent landscape (verified by back-buffer
dump, byte-identical to the pre-cleanup frame outside the HUD strip).
