# Entasis adapter

[bench](bench) owns arguments, lifecycle, CSV output and recording. [common](common) owns the typed case decoder, native shape and pose conversion, and wire codecs. [cases](cases) implements six families:

| Family                                         | Shape presets                          |
| ---------------------------------------------- | -------------------------------------- |
| Container, Spatial Query                       | Authored, Sphere, Capsule, Convex Hull |
| Contact Islands                                | Authored                               |
| Large Pyramid, Pyramid Wall, Heavy Ray Tracing | Authored                               |

Ragdoll is unsupported. Run this adapter through [Physics Arena](../../README.md), which supplies the validated case contract

## Shapes and workers

Simulation shapes use native boxes, spheres, capsules and the canonical 24-point cooked convex hull. Mass follows density and solid volume. Creation applies capsule-axis rotation and cooked center-of-mass offsets. Saved poses reverse those adjustments

Each requested thread count includes the caller and N-1 native background workers. One thread uses the scalar path

Authored dynamics settings use one velocity iteration, four substeps, zero damping, discrete collision, disabled sleeping and fixture friction. Custom runs use their saved effective settings. Spatial Query and Heavy Ray Tracing do not step a dynamics solver

In custom runs, Sleep On enables native automatic sleeping and waking for every supported dynamics case, including Contact Islands. Sleep Off keeps all dynamic bodies active. Authored defaults remain Off

In custom runs, continuous collision detection (CCD) Off uses native discrete detection. CCD On uses native swept CCD with default refinement settings for all supported dynamics, including pyramid projectiles. Native speculative-margin defaults are preserved

Restitution accepts coefficients from 0 to 1. A positive coefficient becomes the world fallback for dynamic and static collidables and enables native contact restitution with the native 1 m/s approach-speed threshold and default combine policy. Zero keeps optional restitution disabled. Result text records the effective CCD mode, coefficient and enabled threshold

## Timing and lifecycle

[bench/session.odin](bench/session.odin) owns simulation warmup, reset and timing. The timer encloses `world_step` and worker completion. Large Pyramid also includes its projectile launch, replacing linear velocity while preserving angular velocity

| Authored case   | Warmup and measured state                                                                                                             |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| Container       | Recreates the measured world after warmup, retaining the caller pool                                                                  |
| Contact Islands | Retains 50 groups, 10,000 dynamic bodies and pools after 30 warmup steps for 300 measured steps. The 50 floors share one native shape |
| Large Pyramid   | Zero warmup, with projectiles launched after 120 completed measured steps                                                             |
| Pyramid Wall    | One warmup step and 300 measured steps in the same world                                                                              |
| Spatial Query   | Retains the warmed immutable world, dispatcher, pools and input/debug storage, resetting only measurement totals                      |

Spatial Query measures complete mixed batches: 50,000 rays, 25,000 radius-0.25 sphere casts and 25,000 overlaps against 10,000 static shapes. The timing span for each query family includes lane reset, synchronous dispatch, worker completion, native status checks and hit reduction. Workers use separate pools and collectors

The adapter checks expected hit totals outside timing after every batch and stops at the first mismatch. It also queries debug samples outside timing

Heavy Ray Tracing has its own [runner](bench/ray_tracing_run.odin), with ordinary and native-batch APIs, phase measurements and optional native-hit recordings. Its geometry, capability limits and measurement boundaries are documented in [SOURCES.md](../../SOURCES.md#pyramid-wall-and-heavy-ray-tracing)

Input preparation, body/shape setup, cooking, adapter storage allocation, observations, serialization and file I/O are outside measured work. Native allocations during measured work remain included

## Memory ownership

Simulation caller and worker pools use 65,536-byte blocks. [common/world.odin](common/world.odin) owns case-scaled initial capacities. These are allocation hints, not hard limits or evidence of allocation-free execution. Worlds and query borrows are destroyed before caller pools

Pinned source, toolchain and licenses are listed in [SOURCES.md](../../SOURCES.md#entasis). Historical results retain their original solver settings. Compilation and codec tests alone do not establish physical quality, performance or interactive behavior
