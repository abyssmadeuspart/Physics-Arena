# Box Container Pile 10k Benchmark Report

Simulates 10,000 falling dynamic boxes inside a five-static-body open container at 60 Hz

## Evidence

**660 completed repeats, 0 execution failures, 0 skipped**

Verification Off: physical quality not checked

Physical observations not collected for stack, Wall and Ragdoll quality checks. Completed repeats are unverified

| Field                  | Value                                                                                                                                                                                                      |
| :--------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Selected run id        | `2026-10-05_0700_ddr5-4800_threads-1-2-3-4-5-6-8-10-12-14-16-24_r5`                                                                                                                                        |
| Result folder          | `results/box-container-pile-10k/intel-core-i7-13700k/2026-10-05_0700_ddr5-4800_threads-1-2-3-4-5-6-8-10-12-14-16-24_r5/`                                                                                   |
| Case id                | `box_container_pile_10k`                                                                                                                                                                                   |
| Benchmark mode         | `headless_api`                                                                                                                                                                                             |
| Thread counts          | `1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 24`                                                                                                                                                                  |
| Measured steps         | `300`                                                                                                                                                                                                      |
| Warmup steps           | `30`                                                                                                                                                                                                       |
| Bodies                 | `10005`                                                                                                                                                                                                    |
| Shapes                 | `10005`                                                                                                                                                                                                    |
| Queries                | `0`                                                                                                                                                                                                        |
| Constraints            | `0`                                                                                                                                                                                                        |
| Timestep               | `60 Hz`                                                                                                                                                                                                    |
| Host                   | Intel Core i7-13700K (8P+8E / 24T); max 3.40 GHz; 64 GB DDR5 4800 MHz; Micro-Star International Co., Ltd. PRO Z790-A WIFI (MS-7E07) BIOS A.E0; Microsoft Windows 11 Pro for Workstations 64-bit 10.0.26200 |
| Build settings         | Entasis: odin_dev_2026_09_windows_amd64_v3                                                                                                                                                                 |
|                        | BEPUphysics: dotnet10_release_no_profiling_self_contained                                                                                                                                                  |
|                        | Box3D: windows_clang_cl_release                                                                                                                                                                            |
|                        | Unity DOTS Physics: unity_player                                                                                                                                                                           |
|                        | Rapier3D: rust_1_89_msvc_cargo_release                                                                                                                                                                     |
|                        | Vite's PhysX 3.4: windows_clang_cl_release                                                                                                                                                                 |
|                        | Avian3D: rust_1_95_msvc_cargo_release                                                                                                                                                                      |
|                        | NVIDIA PhysX: vs2026_msbuild_release                                                                                                                                                                       |
|                        | NVIDIA PhysX 5: vs2022_msbuild_release                                                                                                                                                                     |
|                        | Jolt Physics: windows_clang_cl_distribution                                                                                                                                                                |
|                        | Unreal Engine Chaos: unreal_chaos_ubt_win64_shipping                                                                                                                                                       |
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
| Unity DOTS Physics | sleep=not\_applicable, solver\_iterations=2, solver\_type=iterative, stabilization=on, substeps=3, synchronize\_collision\_world=on, workers=threads - 1 |
| Vite's PhysX 3.4 | ccd=disabled, position\_iterations=5, sleep=disabled, velocity\_iterations=1, workers=threads |
| NVIDIA PhysX | ccd=disabled, position\_iterations=5, sleep=disabled, velocity\_iterations=1, workers=threads |
| NVIDIA PhysX 5 | ccd=disabled, position\_iterations=5, sleep=disabled, velocity\_iterations=1, workers=threads |
| Unreal Engine Chaos | ccd=disabled, position\_iterations=3, projection\_iterations=1, sleep=disabled, unit\_scale=100\_chaos\_units\_per\_meter, velocity\_iterations=1, workers=threads |
| Entasis | angular\_damping=0, ccd=discrete, friction=0.5, linear\_damping=0, restitution=0, sleep=disabled, substeps=4, velocity\_iterations=1, workers=threads - 1 |

## Result Summary

Main metric: median Physics ms/step, lower is better. Rows are grouped by thread count and sorted best to worst within each thread group

| Engine              | Threads | Median median_ms_per_step (ms) | Minimum | Maximum | Repeats | Bodies | Shapes | Queries | Constraints |
| :------------------ | ------: | -----------------------------: | ------: | ------: | ------: | -----: | -----: | ------: | ----------: |
| Entasis             |       1 |                         12.566 |  12.214 |  12.690 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |       1 |                         17.050 |  16.868 |  17.825 |       5 | 10,005 | 10,005 |       0 |           0 |
| Box3D               |       1 |                         23.710 |  23.562 |  24.626 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |       1 |                         29.624 |  29.289 |  32.084 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |       1 |                         29.807 |  28.948 |  31.373 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |       1 |                         38.049 |  35.843 |  39.318 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |       1 |                         48.185 |  46.462 |  48.446 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |       1 |                         50.571 |  49.080 |  54.216 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |       1 |                         51.491 |  49.113 |  52.901 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |       1 |                         52.452 |  52.148 |  54.396 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |       1 |                         73.446 |  71.981 |  75.451 |       5 | 10,005 | 10,005 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       2 |                          7.761 |   7.612 |   7.944 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |       2 |                         11.314 |  11.263 |  11.561 |       5 | 10,005 | 10,005 |       0 |           0 |
| Box3D               |       2 |                         12.875 |  12.847 |  12.977 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |       2 |                         15.778 |  15.594 |  16.708 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |       2 |                         16.073 |  15.868 |  16.150 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |       2 |                         21.097 |  20.119 |  23.126 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |       2 |                         28.218 |  28.161 |  30.669 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |       2 |                         28.390 |  28.308 |  30.356 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |       2 |                         28.860 |  28.752 |  29.386 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |       2 |                         29.744 |  29.532 |  30.459 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |       2 |                         46.332 |  45.583 |  47.873 |       5 | 10,005 | 10,005 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       3 |                          5.869 |   5.767 |   5.944 |       5 | 10,005 | 10,005 |       0 |           0 |
| Box3D               |       3 |                          9.349 |   9.263 |   9.409 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |       3 |                          9.502 |   9.234 |   9.703 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |       3 |                         11.454 |  11.307 |  11.631 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |       3 |                         12.544 |  12.245 |  13.204 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |       3 |                         16.553 |  15.687 |  17.211 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |       3 |                         20.851 |  20.715 |  21.184 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |       3 |                         21.656 |  21.174 |  22.350 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |       3 |                         22.761 |  22.363 |  23.579 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |       3 |                         23.420 |  23.255 |  23.614 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |       3 |                         41.717 |  41.414 |  44.046 |       5 | 10,005 | 10,005 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       4 |                          5.106 |   4.964 |   5.174 |       5 | 10,005 | 10,005 |       0 |           0 |
| Box3D               |       4 |                          7.472 |   7.409 |   7.482 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |       4 |                          8.110 |   7.804 |   8.296 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |       4 |                          8.931 |   8.909 |   9.026 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |       4 |                         10.379 |  10.116 |  10.617 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |       4 |                         14.644 |  14.404 |  14.899 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |       4 |                         16.951 |  16.836 |  16.985 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |       4 |                         17.735 |  17.553 |  18.260 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |       4 |                         19.429 |  19.314 |  20.484 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |       4 |                         19.655 |  19.626 |  19.696 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |       4 |                         39.651 |  39.362 |  40.016 |       5 | 10,005 | 10,005 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       5 |                          4.515 |   4.442 |   4.607 |       5 | 10,005 | 10,005 |       0 |           0 |
| Box3D               |       5 |                          6.209 |   6.147 |   6.253 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |       5 |                          7.308 |   7.079 |   7.383 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |       5 |                          7.358 |   7.279 |   7.444 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |       5 |                          8.988 |   8.778 |   9.061 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |       5 |                         12.971 |  12.695 |  13.152 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |       5 |                         14.557 |  14.346 |  14.571 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |       5 |                         15.099 |  15.079 |  15.242 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |       5 |                         17.557 |  17.421 |  17.819 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |       5 |                         18.010 |  17.870 |  18.673 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |       5 |                         37.015 |  36.606 |  38.504 |       5 | 10,005 | 10,005 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       6 |                          4.057 |   3.935 |   4.155 |       5 | 10,005 | 10,005 |       0 |           0 |
| Box3D               |       6 |                          5.357 |   5.327 |   5.380 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |       6 |                          6.361 |   6.349 |   6.403 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |       6 |                          6.769 |   6.650 |   6.787 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |       6 |                          8.158 |   8.078 |   8.391 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |       6 |                         12.026 |  11.960 |  12.703 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |       6 |                         12.683 |  12.636 |  13.154 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |       6 |                         13.337 |  13.200 |  13.612 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |       6 |                         16.613 |  16.020 |  16.959 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |       6 |                         16.880 |  16.550 |  17.015 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |       6 |                         36.062 |  35.767 |  37.787 |       5 | 10,005 | 10,005 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |       8 |                          3.376 |   3.328 |   3.431 |       5 | 10,005 | 10,005 |       0 |           0 |
| Box3D               |       8 |                          4.267 |   4.191 |   4.383 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |       8 |                          5.137 |   5.129 |   5.183 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |       8 |                          6.233 |   6.193 |   6.489 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |       8 |                          7.172 |   7.066 |   7.242 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |       8 |                         10.591 |  10.521 |  11.032 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |       8 |                         11.365 |  10.692 |  11.546 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |       8 |                         11.618 |  11.467 |  11.753 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |       8 |                         14.890 |  14.838 |  15.397 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |       8 |                         15.264 |  14.898 |  15.627 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |       8 |                         35.244 |  34.084 |  35.587 |       5 | 10,005 | 10,005 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |      10 |                          3.728 |   3.695 |   3.807 |       5 | 10,005 | 10,005 |       0 |           0 |
| Box3D               |      10 |                          4.129 |   4.108 |   4.151 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |      10 |                          4.855 |   4.823 |   4.918 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |      10 |                          6.661 |   6.512 |   6.738 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |      10 |                          6.840 |   6.731 |   6.930 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |      10 |                          9.849 |   9.706 |  10.510 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |      10 |                         10.790 |  10.744 |  11.013 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |      10 |                         10.815 |  10.480 |  11.253 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |      10 |                         14.432 |  13.924 |  15.096 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |      10 |                         15.275 |  15.191 |  15.403 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |      10 |                         34.982 |  34.298 |  36.311 |       5 | 10,005 | 10,005 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Box3D               |      12 |                          3.874 |   3.859 |   3.893 |       5 | 10,005 | 10,005 |       0 |           0 |
| Entasis             |      12 |                          3.927 |   3.859 |   3.980 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |      12 |                          4.651 |   4.577 |   4.664 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |      12 |                          6.735 |   6.647 |   6.771 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |      12 |                          7.266 |   6.823 |   7.362 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |      12 |                          9.106 |   9.099 |   9.181 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |      12 |                         10.320 |  10.198 |  10.436 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |      12 |                         10.539 |  10.508 |  10.831 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |      12 |                         13.853 |  13.696 |  14.470 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |      12 |                         14.569 |  14.439 |  14.728 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |      12 |                         34.907 |  34.422 |  36.036 |       5 | 10,005 | 10,005 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Box3D               |      14 |                          3.687 |   3.669 |   3.711 |       5 | 10,005 | 10,005 |       0 |           0 |
| Entasis             |      14 |                          3.858 |   3.836 |   3.872 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |      14 |                          4.453 |   4.437 |   4.494 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |      14 |                          6.480 |   6.412 |   6.658 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |      14 |                          6.836 |   6.683 |   7.150 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |      14 |                          8.765 |   8.692 |   9.455 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |      14 |                         10.108 |  10.022 |  10.145 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |      14 |                         10.535 |  10.478 |  10.739 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |      14 |                         13.811 |  13.628 |  13.856 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |      14 |                         14.318 |  14.205 |  14.402 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |      14 |                         34.452 |  33.589 |  34.851 |       5 | 10,005 | 10,005 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Box3D               |      16 |                          3.578 |   3.572 |   3.616 |       5 | 10,005 | 10,005 |       0 |           0 |
| Entasis             |      16 |                          3.707 |   3.678 |   3.809 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |      16 |                          4.378 |   4.362 |   4.444 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |      16 |                          6.366 |   6.236 |   6.554 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |      16 |                          6.832 |   6.637 |   6.899 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |      16 |                          8.412 |   8.366 |   8.997 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |      16 |                          9.979 |   9.930 |  10.693 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |      16 |                         10.349 |  10.247 |  10.473 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |      16 |                         13.567 |  13.491 |  13.766 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |      16 |                         13.989 |  13.778 |  14.294 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |      16 |                         33.532 |  33.117 |  33.847 |       5 | 10,005 | 10,005 |       0 |           0 |
|                     |         |                                |         |         |         |        |        |         |             |
| Entasis             |      24 |                          3.303 |   3.285 |   3.313 |       5 | 10,005 | 10,005 |       0 |           0 |
| Box3D               |      24 |                          3.747 |   3.736 |   3.781 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unity DOTS Physics  |      24 |                          3.953 |   3.936 |   3.969 |       5 | 10,005 | 10,005 |       0 |           0 |
| Rapier3D            |      24 |                          6.182 |   6.112 |   6.324 |       5 | 10,005 | 10,005 |       0 |           0 |
| BEPUphysics         |      24 |                          6.473 |   6.390 |   6.667 |       5 | 10,005 | 10,005 |       0 |           0 |
| Jolt Physics        |      24 |                          7.790 |   7.734 |   8.778 |       5 | 10,005 | 10,005 |       0 |           0 |
| Avian3D             |      24 |                         13.790 |  13.627 |  13.951 |       5 | 10,005 | 10,005 |       0 |           0 |
| Vite's PhysX 3.4    |      24 |                         15.485 |  14.559 |  15.967 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX 5      |      24 |                         18.152 |  17.734 |  18.367 |       5 | 10,005 | 10,005 |       0 |           0 |
| NVIDIA PhysX        |      24 |                         19.480 |  18.897 |  20.221 |       5 | 10,005 | 10,005 |       0 |           0 |
| Unreal Engine Chaos |      24 |                         31.597 |  31.511 |  31.621 |       5 | 10,005 | 10,005 |       0 |           0 |

## Route Notes

The Result Summary table comes from `summary.csv`, aggregated from `normalized.csv`. Open `PhysicsArena.exe report <result-dir>` and choose **Regenerate report** to refresh it
