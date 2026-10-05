# Large Pyramid Benchmark Report

Simulates a gravity-supported 16,206-cube square stepped pyramid on a floor, holds it for two measured seconds, then launches four medium spheres into its front base at 60 Hz

## Evidence

**Repeats: 245 passed, 7 failed, 408 skipped, 0 unassessed**

Pyramid: safety and unforced shape, limit 10 cm

| Field                  | Value                                                                                                                                                                                                      |
| :--------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Selected run id        | `2026-10-05_0940_ddr5-4800_threads-1-2-3-4-5-6-8-10-12-14-16-24_r5`                                                                                                                                        |
| Result folder          | `results/large-pyramid-16206/intel-core-i7-13700k/2026-10-05_0940_ddr5-4800_threads-1-2-3-4-5-6-8-10-12-14-16-24_r5/`                                                                                      |
| Case id                | `large_pyramid_16206`                                                                                                                                                                                      |
| Benchmark mode         | `headless_api`                                                                                                                                                                                             |
| Thread counts          | `1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 24`                                                                                                                                                                  |
| Measured steps         | `600`                                                                                                                                                                                                      |
| Warmup steps           | `0`                                                                                                                                                                                                        |
| Bodies                 | `16211`                                                                                                                                                                                                    |
| Shapes                 | `16211`                                                                                                                                                                                                    |
| Queries                | `0`                                                                                                                                                                                                        |
| Constraints            | `0`                                                                                                                                                                                                        |
| Timestep               | `60 Hz`                                                                                                                                                                                                    |
| Host                   | Intel Core i7-13700K (8P+8E / 24T); max 3.40 GHz; 64 GB DDR5 4800 MHz; Micro-Star International Co., Ltd. PRO Z790-A WIFI (MS-7E07) BIOS A.E0; Microsoft Windows 11 Pro for Workstations 64-bit 10.0.26200 |
| Build settings         | Entasis: odin_dev_2026_09_windows_amd64_v3                                                                                                                                                                 |
|                        | BEPUphysics: dotnet10_release_no_profiling_self_contained                                                                                                                                                  |
|                        | NVIDIA PhysX 5: vs2022_msbuild_release                                                                                                                                                                     |
|                        | Vite's PhysX 3.4: windows_clang_cl_release                                                                                                                                                                 |
|                        | Box3D: windows_clang_cl_release                                                                                                                                                                            |
|                        | NVIDIA PhysX: vs2026_msbuild_release                                                                                                                                                                       |
|                        | Rapier3D: rust_1_89_msvc_cargo_release                                                                                                                                                                     |
|                        | Unity DOTS Physics: unity_player                                                                                                                                                                           |
|                        | Unreal Engine Chaos: unreal_chaos_ubt_win64_shipping                                                                                                                                                       |
|                        | Jolt Physics: windows_clang_cl_distribution                                                                                                                                                                |
|                        | Avian3D: rust_1_95_msvc_cargo_release                                                                                                                                                                      |
| Data source            | `summary.csv` generated from `normalized.csv`                                                                                                                                                              |
| Chart                  | `summary.svg`                                                                                                                                                                                              |
| Work-unit timing data  | `step-timing.csv`                                                                                                                                                                                          |
| Work-unit timing chart | `step-timing.svg`                                                                                                                                                                                          |
| Case observation data  | `observations.csv`                                                                                                                                                                                         |
| Run route              | windows                                                                                                                                                                                                    |

## Physics settings

| Engine | Settings |
| --- | --- |
| Box3D | ccd=disabled, sleep=disabled, substeps=4, workers=threads - 1 |
| Jolt Physics | angular\_damping=0.05, ccd=disabled, collision\_steps=1, linear\_damping=0.05, position\_iterations=1, sleep=disabled, velocity\_iterations=4, workers=threads - 1 |
| BEPUphysics | angular\_damping=0, ccd=disabled, deterministic=yes, linear\_damping=0, sleep=disabled, substeps=4, velocity\_iterations=1, workers=threads |
| Rapier3D | ccd=disabled, sleep=disabled, solver\_iterations=4, workers=threads |
| Avian3D | ccd=disabled, sleep=disabled, solver\_defaults=avian, substeps=4, workers=threads |
| Unity DOTS Physics | angular\_damping=0, linear\_damping=0, sleep=not\_applicable, solver\_iterations=2, solver\_type=iterative, stabilization=on, substeps=3, synchronize\_collision\_world=on, workers=threads - 1 |
| Vite's PhysX 3.4 | ccd=disabled, position\_iterations=5, sleep=disabled, velocity\_iterations=1, workers=threads |
| NVIDIA PhysX | ccd=disabled, position\_iterations=5, sleep=disabled, velocity\_iterations=1, workers=threads |
| NVIDIA PhysX 5 | ccd=disabled, position\_iterations=5, sleep=disabled, velocity\_iterations=1, workers=threads |
| Unreal Engine Chaos | ccd=disabled, position\_iterations=3, projection\_iterations=1, sleep=disabled, unit\_scale=100\_chaos\_units\_per\_meter, velocity\_iterations=1, workers=threads |
| Entasis | angular\_damping=0, ccd=discrete, friction=0.5, linear\_damping=0, restitution=0, sleep=disabled, substeps=4, velocity\_iterations=1, workers=threads - 1 |

## Result Summary

Main metric: median Physics ms/step, lower is better. Rows are grouped by thread count and sorted best to worst within each thread group

| Engine              | Threads | Median median_ms_per_step (ms) |     Minimum |     Maximum | Repeats | Bodies | Shapes | Queries | Constraints |
| :------------------ | ------: | -----------------------------: | ----------: | ----------: | ------: | -----: | -----: | ------: | ----------: |
| Entasis             |       1 |                         34.164 |      33.597 |      34.769 |       5 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       1 |                         46.400 |      45.125 |      47.429 |       5 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       1 |                         67.577 |      67.577 |      67.577 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       1 |                         68.259 |      68.259 |      68.259 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |       1 |                         69.055 |      69.055 |      69.055 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       1 |                         70.207 |      70.207 |      70.207 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       1 |                         78.154 |      76.744 |      81.709 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       1 |                         81.943 |      81.943 |      81.943 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       1 |                         93.653 |      91.931 |      95.190 |       5 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       1 |                        130.668 |     130.668 |     130.668 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       1 |                        168.364 |     165.121 |     169.213 |       5 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Box3D               |       2 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       2 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       2 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       2 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       2 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       2 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Entasis             |       2 |                         19.423 |      19.351 |      20.141 |       5 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       2 |                         24.926 |      24.926 |      24.926 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       2 |                         39.150 |      36.980 |      41.294 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       2 |                         64.923 |      63.492 |      66.685 |       5 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       2 |                         99.601 |      99.579 |     100.603 |       5 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Box3D               |       3 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       3 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       3 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       3 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       3 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       3 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       3 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Entasis             |       3 |                         14.661 |      14.540 |      15.211 |       5 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       3 |                         29.889 |      29.348 |      32.112 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       3 |                         58.571 |      58.097 |      61.318 |       5 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       3 |                         76.306 |      75.597 |      76.421 |       5 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Box3D               |       4 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       4 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       4 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       4 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       4 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       4 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       4 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Entasis             |       4 |                         12.133 |      12.076 |      12.463 |       5 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       4 |                         25.391 |      24.618 |      26.419 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       4 |                         55.739 |      54.123 |      56.417 |       5 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       4 |                         62.617 |      62.329 |      63.078 |       5 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Box3D               |       5 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       5 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       5 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       5 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       5 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       5 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       5 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Entasis             |       5 |                         10.487 |      10.387 |      10.761 |       5 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       5 |                         23.415 |      22.112 |      24.279 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       5 |                         53.939 |      52.142 |      54.500 |       5 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       5 |                         54.908 |      54.642 |      55.766 |       5 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Box3D               |       6 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       6 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       6 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       6 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       6 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       6 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       6 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Entasis             |       6 |                          9.158 |       9.076 |       9.348 |       5 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       6 |                         20.807 |      20.562 |      21.219 |       5 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       6 |                         49.184 |      48.683 |      49.552 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       6 |                         51.335 |      50.916 |      53.317 |       5 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Box3D               |       8 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       8 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       8 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       8 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       8 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       8 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       8 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Entasis             |       8 |                          7.632 |       7.607 |       7.735 |       5 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       8 |                         18.174 |      17.709 |      18.649 |       5 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       8 |                         42.364 |      41.792 |      42.729 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       8 |                         50.891 |      50.098 |      51.684 |       5 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Box3D               |      10 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |      10 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |      10 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |      10 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |      10 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |      10 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |      10 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Entasis             |      10 |                          8.782 |       8.619 |       8.848 |       5 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |      10 |                         17.512 |      16.868 |      17.825 |       5 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |      10 |                         46.949 |      46.523 |      47.497 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |      10 |                         51.731 |      49.792 |      52.185 |       5 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Box3D               |      12 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |      12 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |      12 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |      12 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |      12 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |      12 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |      12 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Entasis             |      12 |                          8.799 |       8.767 |       8.844 |       5 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |      12 |                         16.967 |      16.812 |      17.204 |       5 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |      12 |                         43.622 |      43.419 |      44.102 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |      12 |                         49.945 |      49.450 |      51.501 |       5 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Box3D               |      14 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |      14 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |      14 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |      14 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |      14 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |      14 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |      14 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Entasis             |      14 |                          8.560 |       8.535 |       8.566 |       5 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |      14 |                         16.515 |      16.126 |      16.698 |       5 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |      14 |                         42.091 |      41.932 |      42.135 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |      14 |                         50.161 |      49.457 |      50.754 |       5 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Box3D               |      16 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |      16 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |      16 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |      16 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |      16 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |      16 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |      16 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Entasis             |      16 |                          8.282 |       8.241 |       8.303 |       5 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |      16 |                         16.084 |      15.982 |      16.364 |       5 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |      16 |                         40.793 |      40.694 |      41.500 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |      16 |                         49.972 |      48.206 |      50.122 |       5 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Box3D               |      24 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |      24 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |      24 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |      24 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |      24 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |      24 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |      24 |                    unavailable | unavailable | unavailable |       0 | 16,211 | 16,211 |       0 |           0 |
| Entasis             |      24 |                          7.387 |       7.374 |       7.400 |       5 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |      24 |                         15.264 |      15.194 |      15.619 |       5 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |      24 |                         39.708 |      39.537 |      40.049 |       5 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |      24 |                         46.257 |      46.094 |      46.706 |       5 | 16,211 | 16,211 |       0 |           0 |

## Assessment diagnostics

<details>
<summary>Show saved assessment details</summary>

Physical rows identify the determining saved repeat for each engine/thread configuration. In diagnostic identities, t is the thread count, r is the zero-based repeat index, and s is the capture segment. Execution diagnostics are in [manifest.json](manifest.json), all recorded physical metrics are in [stability.csv](stability.csv)

| Engine | Threads | Diagnostic |
| --- | ---: | --- |
| Entasis | 1 | Entasis t1 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0703 m (7.03 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| BEPUphysics | 1 | BEPUphysics t1 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0707 m (7.07 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| NVIDIA PhysX 5 | 1 | NVIDIA PhysX 5 t1 r0: unforced slot-envelope excess, body 13818 at s0 measured 9, assessed s0 construction 0 to s0 measured 161, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.2489 m (24.89 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 17, before possible impact |
| Vite's PhysX 3.4 | 1 | Vite's PhysX 3.4 t1 r0: unforced slot-envelope excess, body 13444 at s0 measured 9, assessed s0 construction 0 to s0 measured 161, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.2460 m (24.60 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 17, before possible impact |
| Box3D | 1 | Box3D t1 r0: unforced slot-envelope excess, body 16203 at s0 measured 76, assessed s0 construction 0 to s0 measured 161, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.1804 m (18.04 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 161, before possible impact |
| NVIDIA PhysX | 1 | NVIDIA PhysX t1 r0: unforced slot-envelope excess, body 13115 at s0 measured 9, assessed s0 construction 0 to s0 measured 161, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.2421 m (24.21 cm), limit 0.10 m (10 cm), body 16204 at s0 measured 17, before possible impact |
| Rapier3D | 1 | Rapier3D t1 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Unity DOTS Physics | 1 | Unity DOTS Physics t1 r0: unforced slot-envelope excess, body 14495 at s0 measured 9, assessed s0 construction 0 to s0 measured 160, excluded possible impact interval ending s0 measured 161 (16.667 ms samples), shape excess 0.8898 m (88.98 cm), limit 0.10 m (10 cm), body 14430 at s0 measured 160, before possible impact |
| Unreal Engine Chaos | 1 | Unreal Engine Chaos t1 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| Jolt Physics | 1 | Jolt Physics t1 r0: unforced slot-envelope excess, body 13136 at s0 measured 9, assessed s0 construction 0 to s0 measured 161, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.2231 m (22.31 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 16, before possible impact |
| Avian3D | 1 | Avian3D t1 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| Box3D | 2 | Box3D t2 r0: unassessed (saved trajectory evidence unavailable) |
| Jolt Physics | 2 | Jolt Physics t2 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX | 2 | NVIDIA PhysX t2 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX 5 | 2 | NVIDIA PhysX 5 t2 r0: unassessed (saved trajectory evidence unavailable) |
| Vite's PhysX 3.4 | 2 | Vite's PhysX 3.4 t2 r0: unassessed (saved trajectory evidence unavailable) |
| Unity DOTS Physics | 2 | Unity DOTS Physics t2 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 2 | Entasis t2 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0703 m (7.03 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| BEPUphysics | 2 | BEPUphysics t2 r0: unforced slot-envelope excess, body 16205 at s0 measured 56, assessed s0 construction 0 to s0 measured 161, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.1325 m (13.25 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 92, before possible impact |
| Rapier3D | 2 | Rapier3D t2 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Unreal Engine Chaos | 2 | Unreal Engine Chaos t2 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| Avian3D | 2 | Avian3D t2 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| BEPUphysics | 3 | BEPUphysics t3 r0: unassessed (saved trajectory evidence unavailable) |
| Box3D | 3 | Box3D t3 r0: unassessed (saved trajectory evidence unavailable) |
| Jolt Physics | 3 | Jolt Physics t3 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX | 3 | NVIDIA PhysX t3 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX 5 | 3 | NVIDIA PhysX 5 t3 r0: unassessed (saved trajectory evidence unavailable) |
| Vite's PhysX 3.4 | 3 | Vite's PhysX 3.4 t3 r0: unassessed (saved trajectory evidence unavailable) |
| Unity DOTS Physics | 3 | Unity DOTS Physics t3 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 3 | Entasis t3 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0704 m (7.04 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| Rapier3D | 3 | Rapier3D t3 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Unreal Engine Chaos | 3 | Unreal Engine Chaos t3 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| Avian3D | 3 | Avian3D t3 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| BEPUphysics | 4 | BEPUphysics t4 r0: unassessed (saved trajectory evidence unavailable) |
| Box3D | 4 | Box3D t4 r0: unassessed (saved trajectory evidence unavailable) |
| Jolt Physics | 4 | Jolt Physics t4 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX | 4 | NVIDIA PhysX t4 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX 5 | 4 | NVIDIA PhysX 5 t4 r0: unassessed (saved trajectory evidence unavailable) |
| Vite's PhysX 3.4 | 4 | Vite's PhysX 3.4 t4 r0: unassessed (saved trajectory evidence unavailable) |
| Unity DOTS Physics | 4 | Unity DOTS Physics t4 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 4 | Entasis t4 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0702 m (7.02 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| Rapier3D | 4 | Rapier3D t4 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Unreal Engine Chaos | 4 | Unreal Engine Chaos t4 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| Avian3D | 4 | Avian3D t4 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| BEPUphysics | 5 | BEPUphysics t5 r0: unassessed (saved trajectory evidence unavailable) |
| Box3D | 5 | Box3D t5 r0: unassessed (saved trajectory evidence unavailable) |
| Jolt Physics | 5 | Jolt Physics t5 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX | 5 | NVIDIA PhysX t5 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX 5 | 5 | NVIDIA PhysX 5 t5 r0: unassessed (saved trajectory evidence unavailable) |
| Vite's PhysX 3.4 | 5 | Vite's PhysX 3.4 t5 r0: unassessed (saved trajectory evidence unavailable) |
| Unity DOTS Physics | 5 | Unity DOTS Physics t5 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 5 | Entasis t5 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0703 m (7.03 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| Rapier3D | 5 | Rapier3D t5 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Unreal Engine Chaos | 5 | Unreal Engine Chaos t5 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| Avian3D | 5 | Avian3D t5 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| BEPUphysics | 6 | BEPUphysics t6 r0: unassessed (saved trajectory evidence unavailable) |
| Box3D | 6 | Box3D t6 r0: unassessed (saved trajectory evidence unavailable) |
| Jolt Physics | 6 | Jolt Physics t6 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX | 6 | NVIDIA PhysX t6 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX 5 | 6 | NVIDIA PhysX 5 t6 r0: unassessed (saved trajectory evidence unavailable) |
| Vite's PhysX 3.4 | 6 | Vite's PhysX 3.4 t6 r0: unassessed (saved trajectory evidence unavailable) |
| Unity DOTS Physics | 6 | Unity DOTS Physics t6 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 6 | Entasis t6 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0704 m (7.04 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| Rapier3D | 6 | Rapier3D t6 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Avian3D | 6 | Avian3D t6 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| Unreal Engine Chaos | 6 | Unreal Engine Chaos t6 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| BEPUphysics | 8 | BEPUphysics t8 r0: unassessed (saved trajectory evidence unavailable) |
| Box3D | 8 | Box3D t8 r0: unassessed (saved trajectory evidence unavailable) |
| Jolt Physics | 8 | Jolt Physics t8 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX | 8 | NVIDIA PhysX t8 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX 5 | 8 | NVIDIA PhysX 5 t8 r0: unassessed (saved trajectory evidence unavailable) |
| Vite's PhysX 3.4 | 8 | Vite's PhysX 3.4 t8 r0: unassessed (saved trajectory evidence unavailable) |
| Unity DOTS Physics | 8 | Unity DOTS Physics t8 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 8 | Entasis t8 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0699 m (6.99 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| Rapier3D | 8 | Rapier3D t8 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Avian3D | 8 | Avian3D t8 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| Unreal Engine Chaos | 8 | Unreal Engine Chaos t8 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| BEPUphysics | 10 | BEPUphysics t10 r0: unassessed (saved trajectory evidence unavailable) |
| Box3D | 10 | Box3D t10 r0: unassessed (saved trajectory evidence unavailable) |
| Jolt Physics | 10 | Jolt Physics t10 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX | 10 | NVIDIA PhysX t10 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX 5 | 10 | NVIDIA PhysX 5 t10 r0: unassessed (saved trajectory evidence unavailable) |
| Vite's PhysX 3.4 | 10 | Vite's PhysX 3.4 t10 r0: unassessed (saved trajectory evidence unavailable) |
| Unity DOTS Physics | 10 | Unity DOTS Physics t10 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 10 | Entasis t10 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0700 m (7.00 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| Rapier3D | 10 | Rapier3D t10 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Avian3D | 10 | Avian3D t10 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| Unreal Engine Chaos | 10 | Unreal Engine Chaos t10 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| BEPUphysics | 12 | BEPUphysics t12 r0: unassessed (saved trajectory evidence unavailable) |
| Box3D | 12 | Box3D t12 r0: unassessed (saved trajectory evidence unavailable) |
| Jolt Physics | 12 | Jolt Physics t12 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX | 12 | NVIDIA PhysX t12 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX 5 | 12 | NVIDIA PhysX 5 t12 r0: unassessed (saved trajectory evidence unavailable) |
| Vite's PhysX 3.4 | 12 | Vite's PhysX 3.4 t12 r0: unassessed (saved trajectory evidence unavailable) |
| Unity DOTS Physics | 12 | Unity DOTS Physics t12 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 12 | Entasis t12 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0700 m (7.00 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| Rapier3D | 12 | Rapier3D t12 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Avian3D | 12 | Avian3D t12 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| Unreal Engine Chaos | 12 | Unreal Engine Chaos t12 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| BEPUphysics | 14 | BEPUphysics t14 r0: unassessed (saved trajectory evidence unavailable) |
| Box3D | 14 | Box3D t14 r0: unassessed (saved trajectory evidence unavailable) |
| Jolt Physics | 14 | Jolt Physics t14 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX | 14 | NVIDIA PhysX t14 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX 5 | 14 | NVIDIA PhysX 5 t14 r0: unassessed (saved trajectory evidence unavailable) |
| Vite's PhysX 3.4 | 14 | Vite's PhysX 3.4 t14 r0: unassessed (saved trajectory evidence unavailable) |
| Unity DOTS Physics | 14 | Unity DOTS Physics t14 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 14 | Entasis t14 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0703 m (7.03 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| Rapier3D | 14 | Rapier3D t14 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Avian3D | 14 | Avian3D t14 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| Unreal Engine Chaos | 14 | Unreal Engine Chaos t14 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| BEPUphysics | 16 | BEPUphysics t16 r0: unassessed (saved trajectory evidence unavailable) |
| Box3D | 16 | Box3D t16 r0: unassessed (saved trajectory evidence unavailable) |
| Jolt Physics | 16 | Jolt Physics t16 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX | 16 | NVIDIA PhysX t16 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX 5 | 16 | NVIDIA PhysX 5 t16 r0: unassessed (saved trajectory evidence unavailable) |
| Vite's PhysX 3.4 | 16 | Vite's PhysX 3.4 t16 r0: unassessed (saved trajectory evidence unavailable) |
| Unity DOTS Physics | 16 | Unity DOTS Physics t16 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 16 | Entasis t16 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0705 m (7.05 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| Rapier3D | 16 | Rapier3D t16 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Avian3D | 16 | Avian3D t16 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| Unreal Engine Chaos | 16 | Unreal Engine Chaos t16 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| BEPUphysics | 24 | BEPUphysics t24 r0: unassessed (saved trajectory evidence unavailable) |
| Box3D | 24 | Box3D t24 r0: unassessed (saved trajectory evidence unavailable) |
| Jolt Physics | 24 | Jolt Physics t24 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX | 24 | NVIDIA PhysX t24 r0: unassessed (saved trajectory evidence unavailable) |
| NVIDIA PhysX 5 | 24 | NVIDIA PhysX 5 t24 r0: unassessed (saved trajectory evidence unavailable) |
| Vite's PhysX 3.4 | 24 | Vite's PhysX 3.4 t24 r0: unassessed (saved trajectory evidence unavailable) |
| Unity DOTS Physics | 24 | Unity DOTS Physics t24 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 24 | Entasis t24 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0699 m (6.99 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 8, before possible impact |
| Rapier3D | 24 | Rapier3D t24 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0639 m (6.39 cm), limit 0.10 m (10 cm), body 16205 at s0 measured 42, before possible impact |
| Avian3D | 24 | Avian3D t24 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0766 m (7.66 cm), limit 0.10 m (10 cm), body 12674 at s0 measured 160, before possible impact |
| Unreal Engine Chaos | 24 | Unreal Engine Chaos t24 r0: safety and shape passed, excluded possible impact interval ending s0 measured 162 (16.667 ms samples), shape excess 0.0575 m (5.75 cm), limit 0.10 m (10 cm), body 16158 at s0 measured 8, before possible impact |
| NVIDIA PhysX 5 | 1 | NVIDIA PhysX 5 t1: 1/5 measured repeats, 4 skipped. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 1 | Vite's PhysX 3.4 t1: 1/5 measured repeats, 4 skipped. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 1 | Box3D t1: 1/5 measured repeats, 4 skipped. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 1 | NVIDIA PhysX t1: 1/5 measured repeats, 4 skipped. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 1 | Unity DOTS Physics t1: 1/5 measured repeats, 4 skipped. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 1 | Jolt Physics t1: 1/5 measured repeats, 4 skipped. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 2 | Box3D t2: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 2 | Jolt Physics t2: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 2 | NVIDIA PhysX t2: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX 5 | 2 | NVIDIA PhysX 5 t2: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 2 | Vite's PhysX 3.4 t2: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 2 | Unity DOTS Physics t2: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| BEPUphysics | 2 | BEPUphysics t2: 1/5 measured repeats, 4 skipped. Skipped after failed repeat: engine=bepuphysics2 thread=2 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| BEPUphysics | 3 | BEPUphysics t3: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=bepuphysics2 thread=2 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 3 | Box3D t3: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 3 | Jolt Physics t3: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 3 | NVIDIA PhysX t3: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX 5 | 3 | NVIDIA PhysX 5 t3: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 3 | Vite's PhysX 3.4 t3: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 3 | Unity DOTS Physics t3: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| BEPUphysics | 4 | BEPUphysics t4: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=bepuphysics2 thread=2 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 4 | Box3D t4: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 4 | Jolt Physics t4: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 4 | NVIDIA PhysX t4: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX 5 | 4 | NVIDIA PhysX 5 t4: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 4 | Vite's PhysX 3.4 t4: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 4 | Unity DOTS Physics t4: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| BEPUphysics | 5 | BEPUphysics t5: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=bepuphysics2 thread=2 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 5 | Box3D t5: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 5 | Jolt Physics t5: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 5 | NVIDIA PhysX t5: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX 5 | 5 | NVIDIA PhysX 5 t5: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 5 | Vite's PhysX 3.4 t5: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 5 | Unity DOTS Physics t5: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| BEPUphysics | 6 | BEPUphysics t6: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=bepuphysics2 thread=2 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 6 | Box3D t6: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 6 | Jolt Physics t6: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 6 | NVIDIA PhysX t6: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX 5 | 6 | NVIDIA PhysX 5 t6: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 6 | Vite's PhysX 3.4 t6: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 6 | Unity DOTS Physics t6: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| BEPUphysics | 8 | BEPUphysics t8: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=bepuphysics2 thread=2 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 8 | Box3D t8: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 8 | Jolt Physics t8: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 8 | NVIDIA PhysX t8: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX 5 | 8 | NVIDIA PhysX 5 t8: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 8 | Vite's PhysX 3.4 t8: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 8 | Unity DOTS Physics t8: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| BEPUphysics | 10 | BEPUphysics t10: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=bepuphysics2 thread=2 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 10 | Box3D t10: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 10 | Jolt Physics t10: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 10 | NVIDIA PhysX t10: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX 5 | 10 | NVIDIA PhysX 5 t10: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 10 | Vite's PhysX 3.4 t10: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 10 | Unity DOTS Physics t10: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| BEPUphysics | 12 | BEPUphysics t12: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=bepuphysics2 thread=2 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 12 | Box3D t12: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 12 | Jolt Physics t12: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 12 | NVIDIA PhysX t12: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX 5 | 12 | NVIDIA PhysX 5 t12: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 12 | Vite's PhysX 3.4 t12: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 12 | Unity DOTS Physics t12: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| BEPUphysics | 14 | BEPUphysics t14: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=bepuphysics2 thread=2 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 14 | Box3D t14: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 14 | Jolt Physics t14: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 14 | NVIDIA PhysX t14: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX 5 | 14 | NVIDIA PhysX 5 t14: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 14 | Vite's PhysX 3.4 t14: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 14 | Unity DOTS Physics t14: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| BEPUphysics | 16 | BEPUphysics t16: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=bepuphysics2 thread=2 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 16 | Box3D t16: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 16 | Jolt Physics t16: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 16 | NVIDIA PhysX t16: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX 5 | 16 | NVIDIA PhysX 5 t16: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 16 | Vite's PhysX 3.4 t16: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 16 | Unity DOTS Physics t16: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| BEPUphysics | 24 | BEPUphysics t24: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=bepuphysics2 thread=2 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Box3D | 24 | Box3D t24: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=box3d thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 24 | Jolt Physics t24: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX | 24 | NVIDIA PhysX t24: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| NVIDIA PhysX 5 | 24 | NVIDIA PhysX 5 t24: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=nvidia&#95;physx5 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Vite's PhysX 3.4 | 24 | Vite's PhysX 3.4 t24: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=physx34 thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Unity DOTS Physics | 24 | Unity DOTS Physics t24: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=unity&#95;physics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |

</details>

## Execution outcomes

Completed with failures. Costs describe completed measurements only, unavailable measurements are not zeroes. Original tuple identities and diagnostics are retained in [manifest.json](manifest.json)

| Engine | Threads | Repeat | Stage | Outcome | Reason | Exit code |
| --- | ---: | ---: | --- | --- | --- | ---: |
| box3d | 1 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 1 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 1 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 1 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 2 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 2 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 2 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 2 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 2 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 3 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 3 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 3 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 3 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 3 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 4 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 4 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 4 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 4 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 4 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 5 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 5 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 5 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 5 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 5 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 6 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 6 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 6 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 6 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 6 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 8 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 8 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 8 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 8 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 8 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 10 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 10 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 10 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 10 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 10 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 12 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 12 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 12 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 12 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 12 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 14 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 14 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 14 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 14 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 14 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 16 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 16 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 16 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 16 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 16 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 24 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 24 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 24 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 24 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| box3d | 24 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 1 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 1 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 1 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 1 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 2 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 2 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 2 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 2 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 2 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 3 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 3 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 3 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 3 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 3 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 4 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 4 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 4 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 4 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 4 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 5 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 5 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 5 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 5 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 5 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 6 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 6 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 6 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 6 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 6 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 8 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 8 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 8 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 8 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 8 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 10 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 10 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 10 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 10 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 10 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 12 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 12 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 12 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 12 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 12 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 14 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 14 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 14 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 14 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 14 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 16 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 16 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 16 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 16 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 16 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 24 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 24 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 24 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 24 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| joltphysics | 24 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 2 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 2 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 2 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 2 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 3 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 3 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 3 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 3 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 3 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 4 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 4 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 4 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 4 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 4 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 5 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 5 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 5 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 5 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 5 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 6 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 6 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 6 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 6 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 6 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 8 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 8 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 8 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 8 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 8 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 10 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 10 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 10 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 10 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 10 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 12 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 12 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 12 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 12 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 12 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 14 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 14 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 14 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 14 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 14 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 16 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 16 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 16 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 16 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 16 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 24 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 24 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 24 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 24 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| bepuphysics2 | 24 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 1 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 1 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 1 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 1 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 2 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 2 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 2 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 2 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 2 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 3 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 3 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 3 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 3 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 3 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 4 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 4 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 4 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 4 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 4 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 5 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 5 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 5 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 5 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 5 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 6 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 6 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 6 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 6 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 6 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 8 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 8 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 8 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 8 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 8 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 10 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 10 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 10 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 10 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 10 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 12 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 12 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 12 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 12 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 12 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 14 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 14 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 14 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 14 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 14 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 16 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 16 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 16 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 16 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 16 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 24 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 24 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 24 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 24 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| unity_physics | 24 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 1 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 1 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 1 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 1 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 2 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 2 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 2 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 2 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 2 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 3 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 3 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 3 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 3 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 3 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 4 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 4 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 4 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 4 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 4 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 5 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 5 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 5 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 5 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 5 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 6 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 6 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 6 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 6 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 6 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 8 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 8 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 8 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 8 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 8 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 10 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 10 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 10 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 10 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 10 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 12 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 12 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 12 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 12 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 12 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 14 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 14 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 14 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 14 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 14 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 16 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 16 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 16 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 16 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 16 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 24 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 24 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 24 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 24 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| physx34 | 24 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 1 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 1 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 1 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 1 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 2 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 2 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 2 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 2 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 2 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 3 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 3 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 3 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 3 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 3 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 4 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 4 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 4 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 4 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 4 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 5 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 5 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 5 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 5 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 5 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 6 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 6 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 6 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 6 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 6 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 8 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 8 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 8 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 8 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 8 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 10 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 10 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 10 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 10 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 10 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 12 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 12 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 12 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 12 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 12 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 14 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 14 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 14 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 14 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 14 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 16 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 16 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 16 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 16 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 16 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 24 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 24 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 24 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 24 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx34 | 24 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 1 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 1 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 1 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 1 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 2 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 2 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 2 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 2 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 2 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 3 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 3 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 3 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 3 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 3 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 4 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 4 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 4 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 4 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 4 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 5 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 5 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 5 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 5 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 5 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 6 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 6 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 6 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 6 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 6 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 8 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 8 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 8 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 8 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 8 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 10 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 10 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 10 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 10 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 10 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 12 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 12 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 12 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 12 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 12 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 14 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 14 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 14 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 14 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 14 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 16 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 16 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 16 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 16 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 16 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 24 | 1 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 24 | 2 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 24 | 3 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 24 | 4 | benchmark | not_run | previous_repeat_failed | unavailable |
| nvidia_physx5 | 24 | 5 | benchmark | not_run | previous_repeat_failed | unavailable |

## Route Notes

The Result Summary table comes from `summary.csv`, aggregated from `normalized.csv`. Open `PhysicsArena.exe report <result-dir>` and choose **Regenerate report** to refresh it
