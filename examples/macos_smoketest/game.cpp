// NOTE(nsillik): A deliberately tiny, fully deterministic scene, for comparing the macOS renderer
// against the Linux one (PLAN.md Phase 3).
//
// Why it exists: `terrain_gen` is useless for a pixel comparison.  Its voxels come from a GPU noise
// shader, the chunks stream in on worker threads, and its day/night cycle advances with accumulated
// wall-clock time -- measured, two runs of the *same* binary at the same FrameIndex differed in
// 45% of the frame's bytes.  With no fixed reference there is no way to tell a rendering bug from a
// scene that just happened to be in a different state.
//
// So this example defines the world by hand instead.  The engine finalizes chunk voxels from a
// 66^3 buffer of u32 "noise" values, where bit 31 means "this voxel is filled" and the low 31 bits
// are the voxel's material data (`FinalizeOccupancyMasksFromNoiseValues`, world_chunk.cpp:4458).
// `ChunkCompletionCallbacks` are invoked on that buffer *before* it is finalized
// (api.cpp:736-750), which is the hook used here: every value written below comes from
// `VoxelAt(worldVoxelP)`, a pure function of the voxel's world position.
//
// Consequences, all of them wanted:
//   - identical voxels on macOS and Linux, with no dependence on noise, GPU float behaviour, seeds
//     or timing;
//   - the scene is a function of the *world position*, so neighbouring chunks agree and the chunk
//     seams are exercised (which is where the mesh boundaries are);
//   - lighting is pinned (day/night cycle off, tDay fixed), so the frames differ only by rendering.
//
// The scene itself is chosen to be readable at a glance, in one screenshot:
//
//      z ^                              one 4-voxel-tall red column at (24,24), 2x2 in plan
//        |   . . . . . . . . .          a 3-step staircase of blue blocks along +x at y=40
//        |   ............               a green ground slab, 8 voxels thick, over the whole world
//        |   ............               a 2-voxel-tall yellow ridge along z=56
//        +------------------------> x
//
// Rendering it is the test: the ground plane must be a flat unbroken quad grid, the staircase must
// read as three separate steps, and the column must be a clean vertical box.  Anything streaky,
// half-missing or z-fighting is a renderer bug, and the frame is reproducible, so it can be diffed
// against the Linux one directly.

#define BONSAI_DEBUG_SYSTEM_API 1

#include <bonsai_types.h>
#include <game_types.h>

// NOTE(nsillik): A 1-chunk visible region, so the whole world is 8 chunks and settles immediately.
// The pattern spans several chunks regardless, because it is keyed off world position.
#define SMOKETEST_WORLD_SIZE VisibleRegionSize_1

// Ground slab: z in [0, SMOKETEST_GROUND_THICKNESS).
#define SMOKETEST_GROUND_THICKNESS 8

link_internal b32
SmokeTestVoxelFilled(v3i P)
{
  // Ground plane.
  if (P.z >= 0 && P.z < SMOKETEST_GROUND_THICKNESS) { return True; }

  // A red column, 8x8 in plan, 16 voxels tall, at (16,16).
  if (P.x >= 16 && P.x < 24 && P.y >= 16 && P.y < 24 && P.z >= 0 && P.z < 16) { return True; }

  // A 3-step staircase along +x at y in [32,48).  Steps are 8 wide (x), 16 deep (y) and 8 tall.
  if (P.y >= 32 && P.y < 48)
  {
    if (P.x >= 32 && P.x < 40 && P.z >= 8 && P.z < 16) { return True; }
    if (P.x >= 40 && P.x < 48 && P.z >= 8 && P.z < 24) { return True; }
    if (P.x >= 48 && P.x < 56 && P.z >= 8 && P.z < 32) { return True; }
  }

  // A yellow ridge, 8 voxels tall, running along x at y in [56,62).
  if (P.z >= 8 && P.z < 16 && P.y >= 56 && P.y < 62) { return True; }

  return False;
}

link_internal v3
SmokeTestVoxelColor(v3i P)
{
  if (P.x >= 16 && P.x < 24 && P.y >= 16 && P.y < 24 && P.z >= 0 && P.z < 16)
  {
    return V3(1.f, 0.f, 0.f);              // The column: red.
  }

  if (P.y >= 32 && P.y < 48 && P.z >= 8)
  {
    return V3(0.f, 0.f, 1.f);              // The staircase: blue.
  }

  if (P.z >= 8 && P.z < 16 && P.y >= 56 && P.y < 62)
  {
    return V3(1.f, 1.f, 0.f);              // The ridge: yellow.
  }

  // The ground: banded by z so the top surface is a visible gradient rather than a flat wash.
  r32 t = SafeDivide0(Cast(r32, P.z), Cast(r32, SMOKETEST_GROUND_THICKNESS));
  return Lerp(t, V3(0.05f, 0.35f, 0.08f), V3(0.5f, 0.9f, 0.2f));
}

// NOTE(nsillik): The engine calls this for every chunk before it finalizes the chunk's voxels from
// `NoiseValues`, so whatever is written here *is* the world.
link_internal b32
SmokeTestChunkCompletion(engine_resources *Engine, v3i NoiseDim, u32 *NoiseValues, octree_node *Node)
{
  world_chunk *Chunk = Node->Chunk;
  Assert(Chunk);
  Assert(NoiseDim == V3i(66, 66, 66));

  // Voxel coordinate of the corner of this chunk.  Noise coord (nx, ny, nz) corresponds to the gen
  // chunk's voxel (nx-1, ny, nz) and to the drawn chunk's (nx-1, ny-1, nz-1), which is what makes
  // the apron voxels of one chunk agree with the interior voxels of its neighbour.
  v3i ChunkMinVoxelP = Chunk->WorldP * V3i(64);

  RangeIterator_t(s32, nz, NoiseDim.z)
  RangeIterator_t(s32, ny, NoiseDim.y)
  RangeIterator_t(s32, nx, NoiseDim.x)
  {
    v3i WorldP = ChunkMinVoxelP + V3i(nx - 1, ny - 1, nz - 1);
    s32 NoiseIndex = GetIndex(V3i(nx, ny, nz), NoiseDim);

    if (SmokeTestVoxelFilled(WorldP))
    {
      voxel V = {};
      V.PackedHSV = RGBtoPackedHSV(SmokeTestVoxelColor(WorldP));
      V.Normal = PackV3_15b(V3(0.f, 0.f, 1.f));
      NoiseValues[NoiseIndex] = V.Data | (1u << 31);
    }
    else
    {
      NoiseValues[NoiseIndex] = 0;
    }
  }

  return True;
}

// NOTE(nsillik): Optional callback, called for each worker thread at engine startup.
BONSAI_API_WORKER_THREAD_INIT_CALLBACK()
{
}

// NOTE(nsillik): Optional.  Returning False anywhere here means "engine, you handle this job".
BONSAI_API_WORKER_THREAD_CALLBACK()
{
  switch (Entry->Type)
  {
    InvalidCase(type_work_queue_entry_noop);
    InvalidCase(type_work_queue_entry__align_to_cache_line_helper);
    InvalidCase(type_work_queue_entry__bonsai_render_command);

    case type_work_queue_entry_build_chunk_mesh:
    case type_work_queue_entry_finalize_noise_values:
    case type_work_queue_entry_async_function_call:
    case type_work_queue_entry_rebuild_mesh:
    case type_work_queue_entry_init_asset:
    case type_work_queue_entry_init_world_chunk:
    case type_work_queue_entry_copy_buffer_ref:
    case type_work_queue_entry_copy_buffer_set:
    case type_work_queue_entry_sim_particle_system: {} break;
  }

  return False;
}

BONSAI_API_MAIN_THREAD_INIT_CALLBACK()
{
  UNPACK_ENGINE_RESOURCES(Resources);

  Global_AssetPrefixPath = CSz("examples/macos_smoketest/assets");

  GameState = Allocate(game_state, Resources->GameMemory, 1);
  *GameState = {};

  world_position WorldCenter = World_Position(0, 0, 0);
  AllocateWorld(World, WorldCenter, SMOKETEST_WORLD_SIZE);

  chunk_completion_callback CompletionCallback = SmokeTestChunkCompletion;
  Push(&Resources->ChunkCompletionCallbacks, &CompletionCallback);

  // NOTE(nsillik): Pin the lighting.  Left on, the sun moves with accumulated wall-clock time and
  // two runs of the same binary produce different frames.
  Graphics->Settings.Lighting.AutoDayNightCycle = False;
  // NOTE(nsillik): 5 is measured, not guessed.  UpdateKeyLight derives the sun's direction and colour
  // from tDay, and most values park it below the horizon: sweeping tDay and measuring the mean
  // luminance of the rendered frame gives 0 -> 0.0, 1 -> 14.6, 3 -> 5.3, 5 -> 26.5, 6 -> 7.9.  5 is
  // the brightest by a wide margin.  Pinning it means the frame depends only on the voxels.
  Graphics->Settings.Lighting.tDay = 5.f;

  // NOTE(nsillik): No world-edit brush, no terrain shaping, no debug overlays that would change the
  // image: this example is only interested in the voxels it writes itself.
  Graphics->Settings.DrawMajorGrid = False;
  Graphics->Settings.DrawMinorGrid = False;

  StandardCamera(Graphics->Camera, 10000.f, 1000.f, 1.f);
  SnapCameraToCenterOfWorld(Resources, SMOKETEST_WORLD_SIZE);

  // Look down at the scene from far enough that the whole pattern is in frame.  SnapCameraToCenterOfWorld
  // puts the camera ghost (which api.cpp:180 makes the camera target) at the centre of the visible
  // region, which for a 1-chunk region is chunk (0,0,0) -- exactly where the pattern is.
  Camera->DistanceFromTarget = 150.f;
  Camera->TargetDistanceFromTarget = 150.f;

  // SnapCameraToCenterOfWorld aims the camera ghost (the camera's target) at the *corner* of the
  // visible region, i.e. voxel (0,0,0).  Aim it at the middle of the pattern instead.
  {
    entity *Ghost = GetEntity(EntityTable, Graphics->Camera->GhostId);
    Assert(Ghost);
    Ghost->P = Canonical_Position(V3(32.f, 32.f, 8.f), World_Position(0, 0, 0));
  }

  return GameState;
}

BONSAI_API_MAIN_THREAD_CALLBACK()
{
  Assert(ThreadLocal_ThreadIndex == 0);
  TIMED_FUNCTION();

  UNPACK_ENGINE_RESOURCES(Resources);

  // NOTE(nsillik): Static scene; nothing to update.  Input is deliberately not handled so the camera
  // cannot be moved and the dump stays comparable across runs.
}
