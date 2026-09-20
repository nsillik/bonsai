# Deferred Fixes

Found while acting on a review of the macOS port.  Everything else that review raised is fixed;
these four are here because each is a behaviour change or needs a tool this tree does not have.

## 1. The shadow-map draw list never issues a draw

`DispatchOctreeDrawJobs` pushes a chunk into `ShadowMapDrawList` under the same condition as
`MainDrawList` (`src/engine/world.cpp:1036-1046`), and `api.cpp:532` queues a
`draw_world_chunk_draw_list` for it -- with `Camera = 0`.  Every `DrawCount++` in `RenderDrawList`
is under `if (Camera)`, so the list accumulates nothing, `SubmitDrawList` is skipped, and the shadow
map is never drawn into.  `Settings->UseShadowMapping` is off as well: its only assignment is
commented out at `render_init.cpp:446`.

Handing the shadow pass a camera is not the whole fix.  That reaches `SubmitDrawList` with the
`DepthRTT` program bound (`src/engine/render/shadow_map.h`), which declares neither
`TransformBuffer` nor `DrawIndex`, so both of its asserts fire; and `shadow_map_shader`'s
`ModelMatrix` is only ever bound by the gBuffer path, so a drawn shadow pass would use one stale
matrix for every chunk.

`render.cpp` records the invariant where it is relied on, so this is a feature to build rather than
a trap to fall into.

## 2. `generated/gen_common_vector_v3.h` has a stale callsite comment

It says `vector.h:805:0`; at the `bonsai_stdlib` pin this port moves to, the call is at line 816.
`gen_common_vector_v4.h` was corrected by hand for the same reason.  The fix that is actually
correct rewrites both, which is `./make.sh RunPoof` with `poof` installed.

## 3. The frame-capture harness is not committed

`docs/macos_port.md` reports rendering results measured with a harness that binds framebuffer 0
before `BonsaiSwapBuffers`, reads the viewport with `glReadPixels` and writes a BMP.  None of that is
in the tree, so those results cannot be reproduced from it.  Committing it is a judgement call
rather than a mechanical one: it is throwaway measurement tooling, and Known gaps already concedes
that the macOS/Linux comparison was never completed.

## 4. `ShaderLanguageSetting_410core` is inert

The editor UI offers it and the string/value tables carry it, but nothing reads it (see
`settings.init`).  Wiring it up is a behaviour change, not a fix: it would let the editor select a
shader language per platform, which means every shader in the tree has to be verified to compile at
410 core on Linux too, where only the macOS path is exercised at that version today.
