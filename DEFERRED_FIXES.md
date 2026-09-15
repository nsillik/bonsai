# Deferred Fixes

Found while acting on the review of `port/macos-take-two` against `origin/master`.  Everything
else the review raised is fixed on this branch; these four are not, and each is here because it
is a behaviour change or needs a tool this tree does not have, not because it was missed.

## 1. The shadow-map draw list never issues a draw

**Not introduced here, but load-bearing and now documented.**  `DispatchOctreeDrawJobs` pushes a
chunk into `ShadowMapDrawList` under the same condition it pushes into `MainDrawList`
(`src/engine/world.cpp:1036-1046`), and `src/engine/api.cpp:532` queues a
`draw_world_chunk_draw_list` for it -- but passes `Camera = 0`.  Every `DrawCount++` in
`RenderDrawList` is inside `if (Camera)` (`src/engine/render.cpp:1850-1880`), so the list
accumulates nothing, `SubmitDrawList` is skipped, and the shadow map is never drawn into.
`Settings->UseShadowMapping` is likewise off: its only assignment is commented out
(`src/engine/render/render_init.cpp:446`).

Two things stop this being a one-line fix:

* Handing the shadow pass a camera reaches `SubmitDrawList` with the `DepthRTT` program bound
  (`src/engine/render/shadow_map.h`), which declares neither `TransformBuffer` nor `DrawIndex`,
  so both of that function's asserts fire.  The shadow pass needs either its own path or a
  `DrawIndex`-aware depth shader.
* `shadow_map_shader`'s `ModelMatrix` is only ever bound by the gBuffer path
  (`@janky_model_matrix_bs`, `src/engine/render/shadow_map.h`), so even a drawn shadow pass
  would use one stale matrix for every chunk.  That has to be resolved first.

`src/engine/render.cpp` now records the invariant where it is relied on, so this is a rendering
feature to build rather than a trap to fall into.

## 2. `generated/gen_common_vector_v3.h` has a stale callsite comment

Its header says `// external/bonsai_stdlib/src/vector.h:805:0`; at the `bonsai_stdlib` pin this
branch moves to, the call is at line 816 (the submodule's `1caf36a` added comments above it).
`gen_common_vector_v4.h` was corrected by hand to 819 for the same reason.

Not fixed here because it is comment-only in a generated file, and the fix that is actually
correct is the one that rewrites both: `./make.sh RunPoof` with `poof` installed.  Hand-editing a
second generated file's line numbers makes the tree diverge further from what poof emits, for no
functional gain.

## 3. The frame-capture harness is not committed

`docs/macos_port.md` §Verification reports rendering results measured with a harness that binds
framebuffer 0 just before `BonsaiSwapBuffers`, reads the viewport rect with `glReadPixels` and
writes a BMP.  None of that is in the tree, so the results in that section (and the "45% of the
frame's bytes" variance figure for `terrain_gen`) cannot be reproduced from this branch without
rewriting it.  The section now says so.

Committing it is a judgement call, not a mechanical one: it is throwaway measurement tooling that
has to sit somewhere, and `docs/macos_port.md`'s Known gaps already concedes the macOS/Linux
comparison was never completed.  Whoever picks that up should decide whether the harness becomes a
committed tool (and where) or the results section is dropped.

## 4. `ShaderLanguageSetting_410core` is inert

The editor UI now offers it (`generated/do_editor_ui_for_enum_shader_language_setting.h`) and the
string/value tables carry it, but nothing reads it: `Settings.Graphics.ShaderLanguage`'s only
consumer is commented out in `src/engine/resources.cpp`, and the language actually used comes from
`CompileShaderPair`, which picks `410core` on macOS and `ShaderLanguageSetting_default` (460core)
everywhere else.  `settings.init` says as much where the setting is listed.

Wiring it up is a behaviour change, not a fix: it would let the editor select a shader language per
platform, which means every shader in the tree has to be verified to compile at 410 core on Linux
too -- currently only the macOS path is exercised at that version.  Until then the member exists so
that the enum and the platform default have a name in common, and the UI button is there so the
enum's tables and its editor are not half-wired relative to each other.
