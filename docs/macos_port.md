# The macOS Port

Bonsai builds and runs on macOS from `master`.  This is the reference for the parts of the
port that are not obvious from a diff: what the platform backend is, which macOS
constraints forced code changes, and the failure modes that produce no diagnostic.

Build and run instructions are in [01_build_process.md](01_build_process.md).  This is the
second attempt at the port: the first is `port/macos-phases-0-3` (PRs nsillik/bonsai#5 and
nsillik/bonsai_stdlib#5), which reached the same behaviour with more code and, in three
places, by working around a symptom rather than the cause.  Where that matters it is noted
below.

Two readings of "the platform" run through the whole document:

* **4.1 core** -- `NSOpenGLProfileVersion4_1Core` is the highest GL macOS offers.  It has no
  shader storage buffers (4.3), no `glMultiDrawArraysIndirect` (4.3) and no
  `gl_DrawID` (GLSL 4.60).
* **Apple's GL is a Metal translation layer**, and several of its divergences from Mesa are
  silent -- `GL_FRAMEBUFFER_COMPLETE`, no GL error, wrong pixels.  §Silent failures is the
  list; it is the most useful part of this file.

## What the backend is

`external/bonsai_stdlib/src/platform/macos/macos_platform.{h,cpp}`, reached from the platform
dispatch in `external/bonsai_stdlib/src/platform.{h,cpp}` -- the same shape as the Linux
backend, and the only new backend file.  It owns:

| Piece | Note |
|---|---|
| `os` | `NSWindow *`, `NSView *`, `NSOpenGLContext *`, and `ContinueRunning` |
| `OpenAndInitializeWindow` | 4.1 core pixel format, view, context, swap interval.  The window is ordered front and the app activated explicitly, or a binary launched from a terminal never takes focus |
| `ProcessOsMessages` | one drain of the AppKit event queue per frame; keys, mouse buttons and drags, wheel, live resize |
| `BonsaiSwapBuffers` | `flushBuffer` under `CGLLockContext`, scoped to the flush |
| `PlatformGetGlFunction` | `dlsym(RTLD_DEFAULT, ...)`, warning rather than trapping on a miss |

Two details that are easy to get wrong and are load-bearing:

* **`ScreenDim` is the framebuffer size, not the window size.**  AppKit measures the window in
  points and the surface in backing pixels, and they differ by the backing scale factor (2 on
  Retina).  `ScreenDim` drives `SetViewport` and both projection matrices, so deriving it from
  the point size renders at half resolution *and looks correct while doing it*.  It is
  recomputed in the window delegate's resize and backing-change callbacks, which is where
  AppKit delivers them -- it runs its own tracking loop on the main thread for the duration of
  a resize drag, so the frame loop is not running while they fire.
* **Input is one table, not two switches.**  `keyCode` is a physical, layout-independent
  HIToolbox code, the same property the X11 keysym switch relies on.  The table selects the
  `input` field and the state is applied once afterwards, so keydown and keyup cannot drift
  apart.

## What the 4.1 core context costs

Six code changes, none of which is a workaround for a bug -- they replace 4.3+ machinery with
equivalents available at 4.10.  Each site carries a `NOTE(nsillik)(macos)`.

| Was | Is | Where |
|---|---|---|
| `layout(std430) readonly buffer TransformBuffer` | `uniform samplerBuffer TransformBuffer` + `LoadTransform`, reassembling the struct from 8 `GL_RGBA32F` texels | `shaders/gBuffer.vertexshader` |
| `gl_DrawID` in that shader | `uniform int DrawIndex`, uploaded per draw | same |
| `layout(std430) readonly buffer WorldEditOpBuffer` | the same treatment, 17 texels (272 bytes) | `shaders/terrain/world_edit.fragmentshader` |
| `glMultiDrawArraysIndirect` over a draw list | `SubmitDrawList`: one `glDrawArrays` + one `glUniform1i` per entry | `src/engine/render.cpp` |
| `#version 460 core` | `#version 410 core` on macOS only | `external/bonsai_stdlib/src/shader.cpp` (`CompileShaderPair`) |
| `glGenerateTextureMipmap` | `glGenerateMipmap` on the target already bound (identical semantics; the DSA form only names the texture) | `external/bonsai_stdlib/src/ui/ui.cpp` |

A texture buffer is the right substitute for a std430 block because it imposes no layout at
all: texel N is bytes `[16N, 16N+16)`.  Structs whose members are already padded to 16 bytes
map onto it one field per fixed texel-and-component, with nothing straddling a texel.
`world_edit_op`'s `Pad` members are exactly that padding, and `editor.h` asserts the size and
`offsetof(Pad9)` so that an edit which moves a field fails at compile time.

The draw list also stopped *being* indirect, which is the one place the port's change is not
merely a portability fix.  See §Silent failures.

## Silent failures

None of these produce a GL error, a compiler diagnostic, or an incomplete-framebuffer
warning.  Each is measured; each was reached in the first attempt after losing time to the
symptom instead of the cause.

### Apple's core-profile GLSL compiler segfaults on an empty compound statement

A `case` body that is `{}` -- or that contains only comments, which is the same thing to the
compiler -- kills `glLinkProgram` with SIGSEGV inside `glpLLVMCGSwitchStatement`.  Because it
is the *linker*, the only symptom is a dead process with no output.

Measured with a standalone offscreen probe, `shaders/composite.fragmentshader` at
`#version 410 core`, Apple M4 Max / "4.1 Metal - 91.7":

| body | runs |
|---|---|
| `case 3: {} break;` | SIGSEGV 6 of 6 |
| `case 3: break;` | links 6 of 6 |
| case removed entirely | links 6 of 6 |
| `case 3: { slope = vec3(1.0); } break;` | links 6 of 6 |

So the *empty block* is the trigger, not the case.  The fix is therefore deletion: six sites
across two shaders now express "this case does nothing" as a bare `break` (or an absent case
where the pre-switch values already say so), rather than by adding an inert statement inside
the block.

### One image on two draw buffers discards every fragment

`FramebufferTexture` appends to the next free attachment slot, so attaching an image that is
*already* attached binds it twice, and `SetDrawBuffers` turns both slots on.  Apple's GL then
discards every fragment of every draw into that framebuffer -- no GL error,
`GL_FRAMEBUFFER_COMPLETE` -- and Mesa renders it correctly, so it is invisible on Linux.

In `render_init.cpp`'s Terrain Decoration block this was pure redundancy: the framebuffer had
come out of `InitializeRenderToTextureFramebuffer` already carrying that texture on draw
buffer 0.  It was what left `terrain_gen` with a nearly-empty world.  The first attempt made
the aliasing unrepresentable by teaching `framebuffer` to record which image occupies each
slot; this one deletes the redundant call, which is smaller and fixes the actual defect rather
than a class of it.  The one place that legitimately resets the counter -- `render.cpp`'s RTT
group, which points its framebuffer at a *different* image each call -- carries a note saying
so, so it is not "tidied away" later.

### 3-byte vertices are fetched on a 4-byte stride

Apple's GL only fetches vertex attributes on a 4-byte stride.  Given the tightly packed
3-byte `v3_u8` layout it reads each vertex from 4 bytes past the last, so every vertex after
the first is assembled from the wrong bytes and chunk meshes render as scattered slivers.

Measured with a standalone probe over all 57 `(first, count)` combinations, reading back the
vertices the driver actually fetched:

| idiom | wrong |
|---|---|
| `GL_BYTE` x3, stride 0 (element size, 3) | 57 of 57 |
| `GL_BYTE` x3, stride 4 | 0 of 57 |
| float x3, stride 12 | 0 of 57 |

So `v3_u8` is `alignas(4)` and `sizeof(v3_u8) == 4`; the fourth byte is padding that nothing
reads and the meshers do not write.  This is why `gpu_mapped_buffer.cpp` spells the attribute
stride as `sizeof(v3_u8)` rather than `0`.

`v3_u8` is also used as a colour type, so this resizes a shared general-purpose type to serve
one GPU path.  The alternative -- a dedicated 4-byte vertex type -- avoids that, but touches
every `Cast(v3_u8*, …)` pointer walk in `mesh.h` and `world_chunk.cpp`.  Padding is the
smaller change; it is flagged here because it is a judgement call, not a forced move.

### `glDrawArraysIndirect` crashes Apple's driver

SIGSEGV inside Apple's own driver -- `GLRResourceList::addResource` receiving a null resource,
from `gldRenderVertexArray`, reached *only* through `glDrawArraysIndirect_GL3Exec`.  The
identical draw issued as `glDrawArrays` never crashed.

It is intermittent: always on the first draw of a batch, after 1000-1600 commands have already
gone through, and 12 crashes in 13 runs of the pre-fix code.  Eliminated as causes: buffer
orphaning, the per-draw uniform, the texture buffer, and synchronisation.

The draw list was already not really indirect -- `InstanceCount` was always 1 and
`BaseInstance` was written and never read, because the shader takes its transform from the
`DrawIndex` uniform -- so issuing the draws directly costs one call per entry and removes the
buffer entirely.  `DrawArraysIndirectCommand` became `draw_arrays_command` (`First`, `Count`)
and `MultiDrawIndirect` became `SubmitDrawList`.

### Two readback traps, which is why `FlushStdout` exists

1. `CheckNoiseReadbackJobs` maps a pixel-pack buffer and left it bound.  Any later
   *client-pointer* `glReadPixels` is then a silent `GL_INVALID_OPERATION` that writes
   nothing, which reads as "the renderer drew nothing".  No caller in the tree does that --
   the only other `glReadPixels` binds its own pack buffer first -- but the frame-capture
   harness used to verify the port does, and it is what made that harness's first comparison
   show zero pixels.  It now unbinds after mapping; unbinding does not unmap.
2. `RuntimeBreak` did not flush `log.txt` before trapping.  `stdout` is unbuffered but the
   `log.txt` mirror is a buffered `stdio` stream, so a trap loses it: measured, `log.txt` is
   **empty** after a trap without the flush, not merely truncated.  The first attempt lost
   most of its evidence to this.

## Deliberate deviations from upstream

* **`link_weak` needs `-Wl,-U` per symbol on Darwin.**  `link_weak` is
  `extern "C" __attribute__((weak))` -- the ELF rule for "may be undefined; bind to 0".  ld64
  has no equivalent for a main executable and fails the link instead, and `weak_import` does
  not help (it only relaxes a link against a dylib that *does* define the symbol).  The seven
  symbols named in `setup_for_cxx.sh` were derived by linking with an empty list and reading
  the linker's report.  They **must stay undefined**, so the dynamic linker can bind them to
  whatever the loaded game dylib defines; a weak *definition* would pin them to the engine's
  own copy and silently disable the hook.  Deliberately not
  `-Wl,-undefined,dynamic_lookup`, which disables undefined-symbol checking for the whole link.
* **`Fract` collides with the SDK.**  `<MacTypes.h>` declares `typedef SInt32 Fract;` and
  arrives through every Foundation header and through Carbon, which is a hard clash with
  `maff.h`'s `f32 Fract(f32)`: C++ does not let a typedef and a function share a name.
  `macos_platform.h` renames the SDK's typedef for the duration of the imports.  The rename
  has to span every import that can reach `MacTypes.h` first -- it only gets one chance, since
  the header is include-guarded.
* **`gl.cpp`'s loader no longer *requires* 4.3/4.5 entry points.**  Nine symbols
  (`glMultiDrawArraysIndirect`, `glBindTextures`, `glBufferStorage`, `glDebugMessageCallback`,
  the four `glGetQueryBufferObject{iv,uiv,i64v,ui64v}`, `glGenerateTextureMipmap`) are still
  loaded, but not ANDed into `Initialized`, because a 4.1 context lacks them and nothing calls
  them.  They are listed in one place at the top of `InitializeOpenglFunctions`.  This is
  smaller than deleting the typdefs, the struct fields and the load sites, and it does not
  remove API.
* **`PlatformInitializeAudio` and `PlatformPinCurrentThreadToCore` now exist for posix.**
  Shared code calls both unconditionally and only win32 defined them, so Linux and macOS both
  failed to link.  Not macOS-specific; it was the pre-existing CI breakage.
* **The macOS backend does not port `ConnectToServer`.**  It is a copy of the Linux one, under
  `BONSAI_NETWORK_IMPLEMENTATION`, which nothing defines -- and which could not compile
  anyway, since `bonsai_net/network.h` does not exist in either tree.
* **`SetVSync` is untouched.**  No live callers; the swap interval is set where the context is
  created, which is the only place the value is available.  Both of its call sites are
  commented out upstream and stay that way.

## Upstream state this branch depends on

* `bonsai_stdlib`'s `1caf36a` added `poof(gen_common_vector(v4))` to `vector.h` but did not
  commit the output, which is written into **bonsai**'s `generated/` -- so `bonsai` master and
  `bonsai_stdlib` master do not build together.  `generated/gen_common_vector_v4.h` here is
  that output, derived mechanically from the checked-in v3 and verified by deriving v3 from v2
  and comparing byte for byte.  Re-running poof should reproduce it exactly.
* `generated/string_and_value_tables_shader_language_setting.h` is likewise poof output, hand
  extended for the new `ShaderLanguageSetting_410core` member because poof is not installed
  here.
* `scripts` is listed in `.gitmodules` pointing at a `bonsai_build_scripts` repository that is
  not a submodule and whose path is a real tracked directory, which makes
  `git submodule update --init --recursive` fail.  The stanza is dropped.
* The profiler's cycle-counter assertion needs one character changed to `>=`, in
  `nsillik/bonsai_debug` branch `port/macos-take-two`.  Under Rosetta `__rdtsc` is not a cycle
  counter but the 24 MHz system timer: measured, 1,676,577 of 2,000,000 consecutive read pairs
  return an identical value, the smallest non-zero delta is 41 ticks, and the mean is 6.  So
  any region shorter than one tick reads the same value twice and
  `Assert(EndingCycle > StartingCycle)` traps on healthy code -- as soon as the render thread
  does any work, on macOS *and* on any future native arm64 build, where `cntvct_el0` ticks at
  the same rate.  `>=` still catches a counter that rewinds, which is what the assert was
  protecting against.  A third repository is the wrong trade for one character, so it is a
  fork branch, not a PR.

## Verification

macOS 27.0 / Xcode 27 / Apple clang 21, Apple M4 Max, `4.1 Metal - 91.7`, x86_64
cross-targeted and run under Rosetta 2.

| Check | Result |
|---|---|
| `./make.sh` from a clean tree | exit 0, 0 errors, 19 targets |
| `./make.sh RunTests` | exit 0, 10 test executables, all suites pass |
| `game_loader` | opens its window, loads a game lib and renders; runs until the window is closed |
| `terrain_gen` / `blank_project` / `macos_smoketest` | run clean, no trap, no assert |

Rendering is measured from the engine's own back buffer, not from screenshots.  A frame is
grabbed just before `BonsaiSwapBuffers` by binding framebuffer 0, reading the viewport rect
with `glReadPixels` and writing a BMP -- the pack-buffer and read-back-buffer notes in
§Silent failures apply.  That harness is not checked in, so the results below are not
reproducible from this branch without rewriting it; a like-for-like Linux frame has not been
diffed (see Known gaps).  With that:

* `terrain_gen` renders a coherent landscape: the surface fills the frame, no sky, no streaks,
  no slivers.
* `examples/macos_smoketest/` renders its hand-written scene coherently -- ground slab,
  staircase, column, ridge -- with ~3-4% of the frame as geometry and no holes or torn edges.
  That example exists precisely so this claim is checkable: it defines the world by hand
  through `ChunkCompletionCallbacks`, pins the lighting, and so is a pure function of voxel
  position, where `terrain_gen` differs by up to 45% of its frame between two runs of the same
  binary at the same frame index.

## Known gaps

Stated so they are not mistaken for done.

1. **The macOS↔Linux frame comparison is thin.**  The first attempt's gate was a rescaled
   comparison of the smoketest scene between a macOS frame and an llvmpipe frame; this branch
   reproduces the macOS side and checks it is coherent, but a like-for-like Linux frame has
   not been diffed here.
2. **The world-edit path has never executed a brush against terrain.**  It is blocked upstream
   of the port: the `.brush` assets no longer deserialize against `master`'s `editor.h` (the
   loader prints ~55 `Reading Object Delim Failed` errors at startup and runs with empty
   brushes).  The shaders compile and the op buffer is uploaded, but the mapping in `LoadOp`
   is verified by construction and by the two layout asserts, not by a rendered brush.
3. **`MaxFragShaderTexUnits` is queried and never used** (`render_loop.cpp`), adjacent to the
   world-edit op upload.  Dead upstream; left alone rather than deleted in a port diff.
4. **`ErrnoToString` is deliberately not ported.**  Zero callers, and macOS defines only 85 of
   the 122 `errno` constants the Linux version switches over, so a copy would silently drop 37
   cases.  Use `strerror(3)` if ever needed.
5. **Phase 4 (native arm64) is untouched.**  This branch cross-targets x86_64 and the SIMD
   layer is still SSE/AVX-only; dropping `-target` and the four `-m*` flags is that phase's
   job.  `src/tests/allocation.cpp`'s `__ss.__rip` is x86_64-only for the same reason.
6. **`sem_init` is `ENOSYS` on macOS**, so `CreateSemaphore` returns an unusable semaphore and
   `ThreadSleep`/`WakeThread` do nothing.  Harmless today -- nothing calls them; the worker
   loop polls with `SleepMs(1)` by design (`bugs.md` records the semaphore flow control being
   removed) -- but they are dead API that *looks* live.
