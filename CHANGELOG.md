# Changelog

## Unreleased

Changes since `46b2b469` (2026-07-13)

### Ports and cleanup

- Ported benchmark scheduling, configuration loading, CSV processing and Markdown/SVG report generation from Python to the C++ core in `src/physics_arena`
- Removed the root `bench.sh`, `tools/bench.py` and `tools/bench/` implementation
- Ported rendering from SDL/bgfx to raylib 6.0 and OpenGL 3.3, with rlImGui for the interface and ImPlot for charts
- Removed the standalone shared renderer, live visual transport and per-engine `shared_visual_mode` implementations. Recorded playback now lives in the application
- Removed duplicated source revisions, checkout paths and build-tool lists from the runtime engine catalog. Source details remain in each engine's `engine.json`

### Application

- Added the Windows x64 `PhysicsArena.exe` with **Run**, **Results** and **Replay** workspaces
- Added case, engine, thread and repeat selection, editable fixture and solver settings, and saved configuration presets. Shape stays locked for every GUI case
- Added progress, cancellation and error reporting. Opening the application shows Run without starting a benchmark
- Added result browsing, engine comparisons, thread scaling, timing plots, repeat inspection and case explanations

### Engine settings and quality

- Moved material, Sleep, CCD and Ragdoll damping into each engine's physics/solver profile, preserving native benchmark defaults across reset, presets, queue, results and replay comparison
- Added repeated `--physics engine:field=value` assignments and configurable Unity contact solver stabilization, default On. Corrected unavailable Unity Sleep and fixed-zero Chaos Ragdoll damping admission
- Repaired scene, contact-pair and body participation for PhysX CCD and restored native Wall Sleep On thresholds
- Removed runnable Sphere, Capsule and Convex Hull Contact Islands variants. Saved variants remain readable
- Added fixed 10 cm occupied-shape qualification for both pyramids, before possible projectile impact for Large Pyramid and throughout the unforced Wall run. Historical fall passes require full-trace reassessment for current shape acceptance

### Benchmarks

Added six case families alongside Box Container Pile:

| Case                 | Workload                                                                                                  |
| :------------------- | :-------------------------------------------------------------------------------------------------------- |
| Contact Islands      | 50 independent piles of 200 bodies                                                                        |
| Spatial Query Trace  | Rays, sphere casts and overlap queries against 10,000 static bodies                                       |
| Ragdoll Stair Tumble | 512 articulated ragdolls falling down stairs, with joint and body quality measurements instead of timings |
| Large Pyramid        | A 16,206-cube square pyramid struck by four sphere projectiles                                            |
| Pyramid Wall         | A triangular stack one cube deep, with settling, energy and penetration measurements                      |
| Heavy Ray Tracing    | Native CPU ray queries through 65,536 primitives and 1,048,576 mesh triangles across six camera views     |

Added Sphere, Capsule and Convex Hull variants for Container, Spatial Query and Ragdoll. The current catalog has 16 selections, with Box3D covering 16, Entasis 12 and each remaining engine 15

Expanded `config/cases.json` to define geometry, materials, schedules and expected observations. The application passes those settings to the engine adapters

### Engines

- Added the Entasis adapter in Odin, supporting every case family except Ragdoll
- Included Unreal Chaos in the default selection, bringing it to 11 engines
- Updated Rapier from 0.34.0 to 0.35.3 and NVIDIA PhysX 5 from 5.6.1 to 5.10.0. Updated the pinned Box3D, Jolt, BEPUphysics v2 and Avian sources and packages
- Added explicit case support to the engine catalog. Pyramid Wall supports all 11 engines, while Heavy Ray Tracing supports Box3D and Entasis

Exact revisions and package details are listed in [SOURCES.md](SOURCES.md#engine-sources)

### Recordings and reports

- Added optional transform recordings with lossless Zstd compression, seeking and playback without running an engine. Capture can be limited to selected thread counts
- Connected each result's engine, thread count and repeat directly to its recording. Added camera controls, storage estimates, recording deletion and unfinished-file cleanup
- Added saved ray-hit images with shaded, depth, normals and error views
- Added per-step timing files, physical observations and visible quality failures beside performance results. Heavy Ray Tracing reports include phase timings, capability outcomes and memory measurements
- Saved effective fixture and solver settings with each result. Added interrupted-run recovery and recording finalization without repeating completed physics

### Release results

- Added the 2026-10-05 Release runs for Box Container Pile 10k, Box Contact Islands 10k and Large Pyramid, with 11 engines, 12 thread counts and five requested repeats per engine/thread pair
- Linked the reports and charts in the [README](README.md#latest-release-results), with measured timing ranges, physical outcomes and skipped-repeat counts. Container used verification Off. Islands and Pyramid used verification On and retain their physical failures
