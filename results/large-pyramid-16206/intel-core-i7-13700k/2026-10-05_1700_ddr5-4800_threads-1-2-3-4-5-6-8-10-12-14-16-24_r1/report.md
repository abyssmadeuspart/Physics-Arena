# Large Pyramid Benchmark Report

Simulates a gravity-supported 16,206-cube square stepped pyramid on a floor, holds it for two measured seconds, then launches four medium spheres into its front base at 60 Hz

## Evidence

**132 completed repeats, 0 execution failures, 0 skipped**

Verification Off: physical quality not checked

Physical observations not collected for stack, Wall and Ragdoll quality checks. Completed repeats are unverified

| Field                  | Value                                                                                                                                                                                                      |
| :--------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Selected run id        | `2026-10-05_1700_ddr5-4800_threads-1-2-3-4-5-6-8-10-12-14-16-24_r1`                                                                                                                                        |
| Result folder          | `results/large-pyramid-16206/intel-core-i7-13700k/2026-10-05_1700_ddr5-4800_threads-1-2-3-4-5-6-8-10-12-14-16-24_r1/`                                                                                      |
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

| Engine              | Threads | Median median_ms_per_step (ms) | Minimum | Maximum | Repeats | Bodies | Shapes | Queries | Constraints |
| :------------------ | ------: | -----------------------------: | ------: | ------: | ------: | -----: | -----: | ------: | ----------: |
| Entasis             |       1 |                         35.083 |  35.083 |  35.083 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       1 |                         46.608 |  46.608 |  46.608 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       1 |                         64.327 |  64.327 |  64.327 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       1 |                         64.553 |  64.553 |  64.553 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |       1 |                         66.459 |  66.459 |  66.459 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       1 |                         68.021 |  68.021 |  68.021 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       1 |                         75.383 |  75.383 |  75.383 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       1 |                         81.278 |  81.278 |  81.278 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       1 |                         93.447 |  93.447 |  93.447 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       1 |                        128.658 | 128.658 | 128.658 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       1 |                        164.572 | 164.572 | 164.572 |       1 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       2 |                         19.872 |  19.872 |  19.872 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       2 |                         25.572 |  25.572 |  25.572 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |       2 |                         35.260 |  35.260 |  35.260 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       2 |                         36.438 |  36.438 |  36.438 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       2 |                         36.815 |  36.815 |  36.815 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       2 |                         36.846 |  36.846 |  36.846 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       2 |                         39.896 |  39.896 |  39.896 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       2 |                         44.515 |  44.515 |  44.515 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       2 |                         66.157 |  66.157 |  66.157 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       2 |                         72.941 |  72.941 |  72.941 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       2 |                         99.874 |  99.874 |  99.874 |       1 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       3 |                         14.817 |  14.817 |  14.817 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       3 |                         19.096 |  19.096 |  19.096 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |       3 |                         25.954 |  25.954 |  25.954 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       3 |                         26.431 |  26.431 |  26.431 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       3 |                         28.544 |  28.544 |  28.544 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       3 |                         30.437 |  30.437 |  30.437 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       3 |                         31.780 |  31.780 |  31.780 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       3 |                         31.923 |  31.923 |  31.923 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       3 |                         53.196 |  53.196 |  53.196 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       3 |                         57.791 |  57.791 |  57.791 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       3 |                         75.725 |  75.725 |  75.725 |       1 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       4 |                         12.045 |  12.045 |  12.045 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       4 |                         15.360 |  15.360 |  15.360 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |       4 |                         20.576 |  20.576 |  20.576 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       4 |                         21.579 |  21.579 |  21.579 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       4 |                         24.002 |  24.002 |  24.002 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       4 |                         24.212 |  24.212 |  24.212 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       4 |                         26.789 |  26.789 |  26.789 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       4 |                         28.651 |  28.651 |  28.651 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       4 |                         43.452 |  43.452 |  43.452 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       4 |                         55.162 |  55.162 |  55.162 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       4 |                         62.946 |  62.946 |  62.946 |       1 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       5 |                         10.341 |  10.341 |  10.341 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       5 |                         13.260 |  13.260 |  13.260 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |       5 |                         17.225 |  17.225 |  17.225 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       5 |                         19.240 |  19.240 |  19.240 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       5 |                         19.794 |  19.794 |  19.794 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       5 |                         22.075 |  22.075 |  22.075 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       5 |                         24.833 |  24.833 |  24.833 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       5 |                         25.684 |  25.684 |  25.684 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       5 |                         38.213 |  38.213 |  38.213 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       5 |                         51.922 |  51.922 |  51.922 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       5 |                         56.254 |  56.254 |  56.254 |       1 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       6 |                          9.079 |   9.079 |   9.079 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       6 |                         11.640 |  11.640 |  11.640 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |       6 |                         14.932 |  14.932 |  14.932 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       6 |                         16.911 |  16.911 |  16.911 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       6 |                         17.257 |  17.257 |  17.257 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       6 |                         19.776 |  19.776 |  19.776 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       6 |                         23.389 |  23.389 |  23.389 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       6 |                         24.132 |  24.132 |  24.132 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       6 |                         34.337 |  34.337 |  34.337 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       6 |                         49.248 |  49.248 |  49.248 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       6 |                         51.974 |  51.974 |  51.974 |       1 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       8 |                          7.718 |   7.718 |   7.718 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |       8 |                          9.829 |   9.829 |   9.829 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |       8 |                         11.767 |  11.767 |  11.767 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |       8 |                         13.552 |  13.552 |  13.552 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |       8 |                         15.348 |  15.348 |  15.348 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |       8 |                         17.415 |  17.415 |  17.415 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |       8 |                         21.518 |  21.518 |  21.518 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |       8 |                         22.669 |  22.669 |  22.669 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |       8 |                         29.490 |  29.490 |  29.490 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |       8 |                         42.582 |  42.582 |  42.582 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |       8 |                         50.244 |  50.244 |  50.244 |       1 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |      10 |                          8.797 |   8.797 |   8.797 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |      10 |                         10.638 |  10.638 |  10.638 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |      10 |                         11.024 |  11.024 |  11.024 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |      10 |                         12.400 |  12.400 |  12.400 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |      10 |                         14.967 |  14.967 |  14.967 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |      10 |                         16.681 |  16.681 |  16.681 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |      10 |                         21.308 |  21.308 |  21.308 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |      10 |                         22.680 |  22.680 |  22.680 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |      10 |                         27.124 |  27.124 |  27.124 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |      10 |                         47.043 |  47.043 |  47.043 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |      10 |                         50.952 |  50.952 |  50.952 |       1 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |      12 |                          8.927 |   8.927 |   8.927 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |      12 |                         10.389 |  10.389 |  10.389 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |      12 |                         10.906 |  10.906 |  10.906 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |      12 |                         11.691 |  11.691 |  11.691 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |      12 |                         14.746 |  14.746 |  14.746 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |      12 |                         16.617 |  16.617 |  16.617 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |      12 |                         21.312 |  21.312 |  21.312 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |      12 |                         22.154 |  22.154 |  22.154 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |      12 |                         25.861 |  25.861 |  25.861 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |      12 |                         44.030 |  44.030 |  44.030 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |      12 |                         51.258 |  51.258 |  51.258 |       1 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |      14 |                          8.791 |   8.791 |   8.791 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |      14 |                          9.864 |   9.864 |   9.864 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |      14 |                         10.482 |  10.482 |  10.482 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |      14 |                         11.187 |  11.187 |  11.187 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |      14 |                         14.660 |  14.660 |  14.660 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |      14 |                         16.451 |  16.451 |  16.451 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |      14 |                         21.690 |  21.690 |  21.690 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |      14 |                         22.313 |  22.313 |  22.313 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |      14 |                         25.930 |  25.930 |  25.930 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |      14 |                         41.930 |  41.930 |  41.930 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |      14 |                         50.225 |  50.225 |  50.225 |       1 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |      16 |                          8.426 |   8.426 |   8.426 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |      16 |                          9.514 |   9.514 |   9.514 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |      16 |                          9.985 |   9.985 |   9.985 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |      16 |                         10.614 |  10.614 |  10.614 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |      16 |                         14.563 |  14.563 |  14.563 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |      16 |                         15.931 |  15.931 |  15.931 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |      16 |                         21.653 |  21.653 |  21.653 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |      16 |                         22.296 |  22.296 |  22.296 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |      16 |                         25.277 |  25.277 |  25.277 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |      16 |                         41.149 |  41.149 |  41.149 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |      16 |                         49.154 |  49.154 |  49.154 |       1 | 16,211 | 16,211 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |      24 |                          7.768 |   7.768 |   7.768 |       1 | 16,211 | 16,211 |       0 |           0 |
| Box3D               |      24 |                          9.035 |   9.035 |   9.035 |       1 | 16,211 | 16,211 |       0 |           0 |
| BEPUphysics         |      24 |                          9.230 |   9.230 |   9.230 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unity DOTS Physics  |      24 |                          9.299 |   9.299 |   9.299 |       1 | 16,211 | 16,211 |       0 |           0 |
| Rapier3D            |      24 |                         15.496 |  15.496 |  15.496 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX 5      |      24 |                         20.109 |  20.109 |  20.109 |       1 | 16,211 | 16,211 |       0 |           0 |
| Jolt Physics        |      24 |                         22.953 |  22.953 |  22.953 |       1 | 16,211 | 16,211 |       0 |           0 |
| Vite's PhysX 3.4    |      24 |                         26.791 |  26.791 |  26.791 |       1 | 16,211 | 16,211 |       0 |           0 |
| NVIDIA PhysX        |      24 |                         27.441 |  27.441 |  27.441 |       1 | 16,211 | 16,211 |       0 |           0 |
| Avian3D             |      24 |                         39.848 |  39.848 |  39.848 |       1 | 16,211 | 16,211 |       0 |           0 |
| Unreal Engine Chaos |      24 |                         46.619 |  46.619 |  46.619 |       1 | 16,211 | 16,211 |       0 |           0 |

## Route Notes

The Result Summary table comes from `summary.csv`, aggregated from `normalized.csv`. Open `PhysicsArena.exe report <result-dir>` and choose **Regenerate report** to refresh it
