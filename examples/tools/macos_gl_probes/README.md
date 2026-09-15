# macOS GL probes

Two standalone offscreen GL 4.1 programs, written to isolate the macOS rendering mismatch of
PLAN.md Deviations #24. Neither is part of `make.sh`; neither is engine code. Build and run them
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

Both render nothing (`GL_RASTERIZER_DISCARD`) and read results back numerically, so they need no
window, no screenshots and no interpretation. **Keep that shape for the next one:** the question that
is still open (PLAN.md Deviations #26) is the same kind — render a known pattern into an `R32UI`
attachment the way `render_init.cpp:769` does, read it back the way `render_loop.cpp:762` does, and
compare every texel.

Build both ways to tell an Apple driver bug from a Rosetta GL-client bug:

```bash
clang++ -std=c++17 -O1 -target x86_64-apple-macos11 -o /tmp/probe_x86 gl_first_probe.cpp -framework OpenGL
```
