# The macOS Port

This documents the parts of the macOS port that are not obvious from a diff: what the platform
backend is, which macOS constraints forced code changes, and the failure modes that produce no
diagnostic.

Build and run instructions are in [01_build_process.md](01_build_process.md).

Two readings of "the platform" run through the document:

* **4.1 core** -- `NSOpenGLProfileVersion4_1Core` is the highest GL macOS offers.  It has no
  shader storage buffers (4.3), no `glMultiDrawArraysIndirect` (4.3) and no `gl_DrawID`
  (GLSL 4.60).
* **Apple's GL is a Metal translation layer**, and its divergences from Mesa are often silent
  -- `GL_FRAMEBUFFER_COMPLETE`, no GL error, wrong pixels.  §Silent failures is the list.

## What the backend is

`external/bonsai_stdlib/src/platform/macos/macos_platform.{h,cpp}`, reached from the platform
dispatch in `external/bonsai_stdlib/src/platform.{h,cpp}` -- the same shape as the Linux
backend, and the only new backend file.  It owns:

| Piece | Note |
|---|---|
| `os` | `NSWindow *`, `NSView *`, `NSOpenGLContext *`, and `ContinueRunning` |
| `OpenAndInitializeWindow` | 4.1 core pixel format, view, context, swap interval |
| `ProcessOsMessages` | one drain of the AppKit event queue per frame; keys, mouse buttons and drags, wheel, live resize |
| `BonsaiSwapBuffers` | `flushBuffer` under `CGLLockContext`, scoped to the flush |
| `PlatformGetGlFunction` | `dlsym(RTLD_DEFAULT, ...)`, warning rather than trapping on a miss |

Two details that are easy to get wrong:

* **`ScreenDim` is the framebuffer size, not the window size.**  AppKit measures the window in
  points and the surface in backing pixels, and they differ by the backing scale factor (2 on
  Retina).  `ScreenDim` drives `SetViewport` and both projection matrices, so deriving it from
  the point size renders at half resolution *and looks correct while doing it*.
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

## Silent failures

None of these produce a GL error, a compiler diagnostic, or an incomplete-framebuffer warning.

### An empty `case` body kills the GLSL linker

`case N: {} break;` -- or a body of only comments, which is the same thing to the compiler --
segfaults Apple's core-profile GLSL compiler in `glpLLVMCGSwitchStatement`.  It is the
*linker*, so the only symptom is a dead process with no output.  Six sites across two shaders
express "this case does nothing" as a bare `break`, or omit the case where the pre-switch
values already say so.

### One image on two draw buffers discards every fragment

`FramebufferTexture` appends to the next free attachment slot, so attaching an image that is
*already* attached binds it twice and `SetDrawBuffers` turns both slots on.  Apple's GL then
discards every fragment of every draw into that framebuffer -- no GL error,
`GL_FRAMEBUFFER_COMPLETE` -- and Mesa renders it correctly.

In `render_init.cpp`'s Terrain Decoration block this was pure redundancy: the framebuffer came
out of `InitializeRenderToTextureFramebuffer` already carrying that texture on draw buffer 0,
and the redundant attach is deleted.  `render.cpp`'s RTT group is re-pointed at a *different*
image on every call, so it must reset the attachment counter.

### A 3-byte vertex is fetched on a 4-byte stride

Apple's GL only fetches vertex attributes on a 4-byte stride, so the tightly packed 3-byte
`v3_u8` layout reads each vertex from 4 bytes past the last and chunk meshes render as scattered
slivers.  `v3_u8` is therefore `alignas(4)` with `sizeof(v3_u8) == 4`, the fourth byte being
padding that nothing reads and the meshers do not write; that is why `gpu_mapped_buffer.cpp`
spells the attribute stride as `sizeof(v3_u8)` rather than `0`.

`v3_u8` is also used as a colour type, so this resizes a shared general-purpose type to serve
one GPU path.  A dedicated 4-byte vertex type avoids that but touches every
`Cast(v3_u8*, …)` pointer walk in `mesh.h` and `world_chunk.cpp`; padding is the smaller change.

### `glDrawArraysIndirect` crashes Apple's driver

SIGSEGV inside Apple's own driver, reached *only* through `glDrawArraysIndirect_GL3Exec`; the
identical draw issued as `glDrawArrays` never crashed.  The draw list was already not really
indirect -- `InstanceCount` was always 1 and `BaseInstance` was written and never read, because
the shader takes its transform from the `DrawIndex` uniform -- so the draws are issued directly,
which costs one call per entry and removes the buffer.  This is the one place the port's change
is not merely a portability fix: `DrawArraysIndirectCommand` became `draw_arrays_command`
(`First`, `Count`) and `MultiDrawIndirect` became `SubmitDrawList`.

### Two readback traps

1. `CheckNoiseReadbackJobs` maps a pixel-pack buffer and left it bound.  Any later
   *client-pointer* `glReadPixels` is then a silent `GL_INVALID_OPERATION` that writes
   nothing, which reads as "the renderer drew nothing".  It unbinds after mapping; unbinding
   does not unmap.
2. `RuntimeBreak` did not flush `log.txt` before trapping.  `stdout` is unbuffered but the
   `log.txt` mirror is a buffered `stdio` stream, so a trap loses it entirely.

## Deliberate deviations from upstream

* **`link_weak` needs `-Wl,-U` per symbol on Darwin.**  `link_weak` is
  `extern "C" __attribute__((weak))` -- the ELF rule for "may be undefined; bind to 0".  ld64
  has no equivalent for a main executable and fails the link instead.  The symbols named in
  `setup_for_cxx.sh` **must stay undefined**, so the dynamic linker can bind them to whatever
  the loaded game dylib defines; a weak *definition* would pin them to the engine's own copy
  and silently disable the hook.  Deliberately not `-Wl,-undefined,dynamic_lookup`, which
  disables undefined-symbol checking for the whole link.
* **`Fract` collides with the SDK.**  `<MacTypes.h>` declares `typedef SInt32 Fract;`, a hard
  clash with `maff.h`'s `f32 Fract(f32)`: C++ does not let a typedef and a function share a
  name.  `macos_platform.h` renames the SDK's typedef for the duration of the imports, and the
  rename has to span every import that can reach `MacTypes.h` first -- the header is
  include-guarded, so it only gets one chance.
* **`gl.cpp` no longer loads 4.3/4.5 entry points nothing calls.**  The nine a 4.1 context
  lacks are gone -- loader, typedef and `opengl` member -- rather than left loaded and
  ungated, because a member that is never assigned compiles and dereferences null.
* **`PlatformInitializeAudio` and `PlatformPinCurrentThreadToCore` exist for posix.**  Shared
  code calls both unconditionally and only win32 defined them, so Linux and macOS both failed
  to link.
* **`ConnectToServer` is not ported.**  It is a copy of the Linux one under
  `BONSAI_NETWORK_IMPLEMENTATION`, which nothing defines -- and which could not compile anyway,
  since `bonsai_net/network.h` does not exist in either tree.
* **`SetVSync` is untouched.**  No live callers; the swap interval is set where the context is
  created, which is the only place the value is available.

## Upstream state this depends on

* `bonsai_stdlib`'s `1caf36a` added `poof(gen_common_vector(v4))` to `vector.h` but did not
  commit the output, which is written into **bonsai**'s `generated/` -- so `bonsai` master and
  `bonsai_stdlib` master do not build together.  `generated/gen_common_vector_v4.h` here is
  that output.
* `generated/string_and_value_tables_shader_language_setting.h` is likewise poof output, hand
  extended for the new `ShaderLanguageSetting_410core` member because poof is not installed
  here.
* `scripts` in `.gitmodules` pointed at a `bonsai_build_scripts` repository that is not a
  submodule and whose path is a real tracked directory, which makes
  `git submodule update --init --recursive` fail.  The stanza is dropped.
* The profiler's cycle-counter assertion is `>=` rather than `>`, in `nsillik/bonsai_debug`.
  Under Rosetta `__rdtsc` is the system timer rather than a cycle counter, so a short region
  reads the same value twice and `>` traps on healthy code -- on macOS, and on any native arm64
  build, where `cntvct_el0` ticks at the same rate.  `>=` still catches a counter that rewinds,
  which is what the assert was protecting against.

## Verification

macOS 27.0 / Xcode 27 / Apple clang 21, Apple M4 Max, `4.1 Metal - 91.7`, x86_64
cross-targeted and run under Rosetta 2.

| Check | Result |
|---|---|
| `./make.sh` from a clean tree | exit 0, 19 targets |
| `./make.sh RunTests` | exit 0, 10 test executables, all suites pass |
| `game_loader` | opens its window, loads a game lib and renders; runs until the window is closed |
| `terrain_gen` / `blank_project` / `macos_smoketest` | run clean, no trap, no assert |

Rendering was checked by reading frames back from the engine's own back buffer (bind framebuffer
0, `glReadPixels`, write a BMP) rather than from screenshots.  That harness is not checked in,
and a like-for-like Linux frame has not been diffed (see Known gaps).

* `terrain_gen` renders a coherent landscape: the surface is unbroken -- no streaks, slivers or
  holes -- with `Graphics->SkyColor` above its horizon.  Geometry detached from the surface is
  the shaping rule's output rather than a renderer artifact: the rules write a density into
  `Output.a` that is not monotonic in z, so a column can be filled in more than one separated z
  span (see `shaders/terrain/shaping/7_steep_ravines.fragmentshader`).
* `examples/macos_smoketest/` renders its hand-written scene coherently -- ground slab,
  staircase, column, ridge -- with no holes or torn edges.  It defines the world by hand through
  `ChunkCompletionCallbacks` and pins the lighting, so its frame is a pure function of voxel
  position, where `terrain_gen` is not.

## Known gaps

1. **The macOS↔Linux frame comparison is thin.**  The macOS side is reproduced and checked for
   coherence; a like-for-like Linux frame has not been diffed.
2. **The world-edit path has never executed a brush against terrain.**  It is blocked upstream
   of the port: the `.brush` assets no longer deserialize against `master`'s `editor.h` (the
   loader prints `Reading Object Delim Failed` errors at startup and runs with empty brushes).
   The shaders compile and the op buffer is uploaded, but the mapping in `LoadOp` is verified by
   construction and by the two layout asserts, not by a rendered brush.
3. **`MaxFragShaderTexUnits` is queried and never used** (`render_loop.cpp`), adjacent to the
   world-edit op upload.  Dead upstream; left alone rather than deleted in a port diff.
4. **`ErrnoToString` is deliberately not ported.**  Zero callers, and macOS defines only a
   subset of the `errno` constants the Linux version switches over, so a copy would silently
   drop cases.  Use `strerror(3)` if ever needed.
5. **Native arm64 is untouched.**  This cross-targets x86_64 and the SIMD layer is
   SSE/AVX-only; dropping `-target` and the four `-m*` flags is a separate job.
   `src/tests/allocation.cpp`'s `__ss.__rip` is x86_64-only for the same reason.
6. **`sem_init` is `ENOSYS` on macOS**, so `CreateSemaphore` returns an unusable semaphore and
   `ThreadSleep`/`WakeThread` do nothing.  Harmless today -- nothing calls them; the worker
   loop polls with `SleepMs(1)` by design (`bugs.md` records the semaphore flow control being
   removed) -- but they are dead API that *looks* live.
