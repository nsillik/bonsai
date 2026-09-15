# macOS GL probes

Three standalone offscreen GL 4.1 programs, written to isolate the macOS rendering mismatches of
PLAN.md Deviations #24 and #27. None is part of `make.sh`; none is engine code. Build and run them
directly:

```bash
clang++ -std=c++17 -O1 -DGL_SILENCE_DEPRECATION -o /tmp/probe gl_first_probe.cpp -framework OpenGL
/tmp/probe            # or: /tmp/probe "v2 byte"  -- substring-selects a variant
```

- `gl_first_probe.cpp` — does `glDrawArrays(GL_TRIANGLES, first, count)` fetch the vertices that live
  at `first`? The vertex buffer is filled with a known pattern, the shader writes the fetched position
  to a transform-feedback varying, and the captured values are compared against the pattern computed
  on the CPU. Six variants cover the engine's attribute layout, and it sweeps `first` from 0 to
  67,108,863. Found: `GL_BYTE` x3 with `stride = 0` is fetched at a 4-byte stride (57/57 wrong);
  `stride = 4` over 4-byte-padded data is correct (0/57).

- `gl_uniform_probe.cpp` — does a per-draw `glUniform1i` plus `texelFetch(samplerBuffer)` loop deliver
  the right value to each draw? Reproduces the engine's shape (texture buffer on unit 15, re-uploaded
  per frame, ~1700 draws x 60 frames) and captures both the uniform and the fetched texel per vertex.
  Found: correct, 0 mismatches in 226,936 checks.

- `gl_drawbuffer_alias_probe.cpp` — does a draw into a framebuffer whose two draw buffers alias the
  *same* image write anything? Four framebuffer configurations, each cleared to a sentinel and drawn
  into with the engine's own quad, counting the texels that still hold the sentinel. Found: with one
  texture on `COLOR_ATTACHMENT0` **and** `COLOR_ATTACHMENT1` and two enabled draw buffers, **0 of
  287,496 texels are written**, the framebuffer still reports `GL_FRAMEBUFFER_COMPLETE` and no GL
  error is raised. Same texture with one draw buffer: 287,496/287,496. Two *different* textures on
  the two attachments with two draw buffers: 287,496/287,496, so it is the alias and not the count.
  Identical on x86_64 under Rosetta and on arm64, so it is the driver, not the GL client. This is the
  bug that left `terrain_gen` with a near-empty world; the fix is to reset `FBO->Attachments` before
  re-attaching (`render_init.cpp`, Terrain Decoration).

All three render nothing (`GL_RASTERIZER_DISCARD` in the first two; the third reads back a sentinel)
and produce numbers rather than screenshots, so they need no window and no interpretation. **Keep
that shape for the next one.**

Build both ways to tell an Apple driver bug from a Rosetta GL-client bug:

```bash
clang++ -std=c++17 -O1 -target x86_64-apple-macos11 -o /tmp/probe_x86 gl_first_probe.cpp -framework OpenGL
```
