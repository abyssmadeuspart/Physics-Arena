# Box Contact Islands 10k Benchmark Report

Simulates 50 independent stacks of 200 initially aligned, touching one-metre boxes at 60 Hz

## Evidence

**Repeats: 600 passed, 1 failed, 59 skipped, 0 unassessed**

Contact Islands: safety and final-second shape, limit 10% of box edge

| Field                  | Value                                                                                                                                                                                                      |
| :--------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Selected run id        | `2026-10-05_0800_ddr5-4800_threads-1-2-3-4-5-6-8-10-12-14-16-24_r5`                                                                                                                                        |
| Result folder          | `results/box-contact-islands-10k/intel-core-i7-13700k/2026-10-05_0800_ddr5-4800_threads-1-2-3-4-5-6-8-10-12-14-16-24_r5/`                                                                                  |
| Case id                | `box_contact_islands_10k`                                                                                                                                                                                  |
| Benchmark mode         | `headless_api`                                                                                                                                                                                             |
| Thread counts          | `1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 24`                                                                                                                                                                  |
| Measured steps         | `300`                                                                                                                                                                                                      |
| Warmup steps           | `30`                                                                                                                                                                                                       |
| Bodies                 | `10050`                                                                                                                                                                                                    |
| Shapes                 | `10050`                                                                                                                                                                                                    |
| Queries                | `0`                                                                                                                                                                                                        |
| Constraints            | `0`                                                                                                                                                                                                        |
| Timestep               | `60 Hz`                                                                                                                                                                                                    |
| Host                   | Intel Core i7-13700K (8P+8E / 24T); max 3.40 GHz; 64 GB DDR5 4800 MHz; Micro-Star International Co., Ltd. PRO Z790-A WIFI (MS-7E07) BIOS A.E0; Microsoft Windows 11 Pro for Workstations 64-bit 10.0.26200 |
| Build settings         | Entasis: odin_dev_2026_09_windows_amd64_v3                                                                                                                                                                 |
|                        | BEPUphysics: dotnet10_release_no_profiling_self_contained                                                                                                                                                  |
|                        | Vite's PhysX 3.4: windows_clang_cl_release                                                                                                                                                                 |
|                        | NVIDIA PhysX: vs2026_msbuild_release                                                                                                                                                                       |
|                        | Jolt Physics: windows_clang_cl_distribution                                                                                                                                                                |
|                        | NVIDIA PhysX 5: vs2022_msbuild_release                                                                                                                                                                     |
|                        | Rapier3D: rust_1_89_msvc_cargo_release                                                                                                                                                                     |
|                        | Box3D: windows_clang_cl_release                                                                                                                                                                            |
|                        | Unity DOTS Physics: unity_player                                                                                                                                                                           |
|                        | Unreal Engine Chaos: unreal_chaos_ubt_win64_shipping                                                                                                                                                       |
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
| Unity DOTS Physics | sleep=not\_applicable, solver\_iterations=2, solver\_type=iterative, stabilization=on, substeps=3, synchronize\_collision\_world=on, workers=threads - 1 |
| Vite's PhysX 3.4 | ccd=disabled, position\_iterations=5, sleep=disabled, velocity\_iterations=1, workers=threads |
| NVIDIA PhysX | ccd=disabled, position\_iterations=5, sleep=disabled, velocity\_iterations=1, workers=threads |
| NVIDIA PhysX 5 | ccd=disabled, position\_iterations=5, sleep=disabled, velocity\_iterations=1, workers=threads |
| Unreal Engine Chaos | ccd=disabled, position\_iterations=3, projection\_iterations=1, sleep=disabled, unit\_scale=100\_chaos\_units\_per\_meter, velocity\_iterations=1, workers=threads |
| Entasis | angular\_damping=0, ccd=discrete, friction=0.5, linear\_damping=0, restitution=0, sleep=disabled, substeps=4, velocity\_iterations=1, workers=threads - 1 |

## Result Summary

Main metric: median Physics ms/step, lower is better. Rows are grouped by thread count and sorted best to worst within each thread group

| Engine              | Threads | Median median_ms_per_step (ms) |     Minimum |     Maximum | Repeats | Bodies | Shapes | Queries | Constraints |
| :------------------ | ------: | -----------------------------: | ----------: | ----------: | ------: | -----: | -----: | ------: | ----------: |
| Entasis             |       1 |                         11.362 |      11.282 |      11.376 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |       1 |                         18.966 |      18.727 |      19.156 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |       1 |                         36.608 |      35.585 |      37.288 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |       1 |                         41.053 |      40.396 |      41.335 |       5 | 10,050 | 10,050 |       0 |           0 |
| Jolt Physics        |       1 |                         41.399 |      41.399 |      41.399 |       1 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |       1 |                         45.612 |      44.321 |      45.902 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |       1 |                         73.400 |      69.744 |      90.239 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |       1 |                         73.776 |      69.421 |      77.152 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |       1 |                         79.741 |      79.014 |      82.068 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |       1 |                        101.519 |      96.546 |     104.727 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |       1 |                        178.414 |     177.503 |     180.413 |       5 | 10,050 | 10,050 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Jolt Physics        |       2 |                    unavailable | unavailable | unavailable |       0 | 10,050 | 10,050 |       0 |           0 |
| Entasis             |       2 |                          7.472 |       7.379 |       7.686 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |       2 |                         13.227 |      13.004 |      13.375 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |       2 |                         18.874 |      18.744 |      19.801 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |       2 |                         21.877 |      21.289 |      22.197 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |       2 |                         23.811 |      23.607 |      24.800 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |       2 |                         36.685 |      34.222 |      37.179 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |       2 |                         43.004 |      41.812 |      45.156 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |       2 |                         44.056 |      43.723 |      44.973 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |       2 |                         51.700 |      51.336 |      53.913 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |       2 |                        102.625 |     101.961 |     105.287 |       5 | 10,050 | 10,050 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Jolt Physics        |       3 |                    unavailable | unavailable | unavailable |       0 | 10,050 | 10,050 |       0 |           0 |
| Entasis             |       3 |                          5.727 |       5.507 |       5.987 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |       3 |                         10.111 |      10.009 |      10.281 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |       3 |                         13.952 |      13.383 |      14.931 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |       3 |                         15.641 |      15.178 |      15.969 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |       3 |                         17.017 |      16.938 |      17.101 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |       3 |                         29.058 |      28.314 |      31.912 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |       3 |                         30.586 |      30.053 |      30.901 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |       3 |                         36.331 |      35.560 |      36.461 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |       3 |                         43.386 |      42.394 |      44.578 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |       3 |                         81.379 |      81.114 |      81.931 |       5 | 10,050 | 10,050 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Jolt Physics        |       4 |                    unavailable | unavailable | unavailable |       0 | 10,050 | 10,050 |       0 |           0 |
| Entasis             |       4 |                          4.844 |       4.636 |       4.880 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |       4 |                          8.220 |       8.197 |       8.424 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |       4 |                         10.982 |      10.661 |      11.394 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |       4 |                         12.101 |      12.004 |      12.288 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |       4 |                         13.625 |      13.224 |      14.010 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |       4 |                         23.014 |      22.891 |      23.451 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |       4 |                         26.807 |      26.046 |      27.536 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |       4 |                         30.094 |      29.434 |      31.813 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |       4 |                         38.402 |      38.064 |      38.924 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |       4 |                         70.189 |      69.804 |      70.867 |       5 | 10,050 | 10,050 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Jolt Physics        |       5 |                    unavailable | unavailable | unavailable |       0 | 10,050 | 10,050 |       0 |           0 |
| Entasis             |       5 |                          4.186 |       4.067 |       4.358 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |       5 |                          7.036 |       6.977 |       7.247 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |       5 |                          8.806 |       8.576 |       9.207 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |       5 |                          9.933 |       9.760 |      10.261 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |       5 |                         11.050 |      10.729 |      11.696 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |       5 |                         18.562 |      18.526 |      18.779 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |       5 |                         22.820 |      22.148 |      23.510 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |       5 |                         26.756 |      26.314 |      27.759 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |       5 |                         35.743 |      35.511 |      35.845 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |       5 |                         63.235 |      62.205 |      65.051 |       5 | 10,050 | 10,050 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Jolt Physics        |       6 |                    unavailable | unavailable | unavailable |       0 | 10,050 | 10,050 |       0 |           0 |
| Entasis             |       6 |                          3.895 |       3.869 |       3.996 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |       6 |                          6.338 |       6.315 |       6.450 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |       6 |                          8.316 |       7.917 |       8.442 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |       6 |                          8.792 |       8.670 |       8.831 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |       6 |                          9.479 |       9.252 |       9.863 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |       6 |                         15.811 |      15.740 |      15.844 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |       6 |                         21.086 |      20.577 |      21.850 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |       6 |                         24.793 |      24.403 |      25.391 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |       6 |                         34.187 |      34.076 |      34.617 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |       6 |                         58.989 |      58.521 |      59.867 |       5 | 10,050 | 10,050 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Jolt Physics        |       8 |                    unavailable | unavailable | unavailable |       0 | 10,050 | 10,050 |       0 |           0 |
| Entasis             |       8 |                          3.269 |       3.192 |       3.386 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |       8 |                          5.775 |       5.723 |       6.086 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |       8 |                          6.766 |       6.635 |       6.830 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |       8 |                          7.536 |       7.463 |       7.619 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |       8 |                          8.038 |       7.593 |       8.210 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |       8 |                         12.426 |      12.308 |      12.627 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |       8 |                         19.031 |      18.071 |      19.250 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |       8 |                         21.576 |      21.387 |      21.818 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |       8 |                         31.285 |      30.799 |      32.345 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |       8 |                         54.990 |      54.776 |      55.918 |       5 | 10,050 | 10,050 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Jolt Physics        |      10 |                    unavailable | unavailable | unavailable |       0 | 10,050 | 10,050 |       0 |           0 |
| Entasis             |      10 |                          3.564 |       3.438 |       3.722 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |      10 |                          6.189 |       6.105 |       6.330 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |      10 |                          6.272 |       6.254 |       6.334 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |      10 |                          7.003 |       6.906 |       7.033 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |      10 |                          7.553 |       7.494 |       7.634 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |      10 |                         11.499 |      11.440 |      11.661 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |      10 |                         17.478 |      16.833 |      17.970 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |      10 |                         21.170 |      20.931 |      21.646 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |      10 |                         30.353 |      30.239 |      30.461 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |      10 |                         57.195 |      57.036 |      57.936 |       5 | 10,050 | 10,050 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Jolt Physics        |      12 |                    unavailable | unavailable | unavailable |       0 | 10,050 | 10,050 |       0 |           0 |
| Entasis             |      12 |                          3.778 |       3.721 |       3.903 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |      12 |                          5.929 |       5.840 |       6.135 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |      12 |                          6.419 |       6.200 |       6.517 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |      12 |                          6.613 |       6.563 |       6.621 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |      12 |                          7.123 |       7.105 |       7.162 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |      12 |                         10.701 |      10.682 |      10.716 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |      12 |                         17.273 |      16.869 |      17.350 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |      12 |                         20.345 |      20.263 |      20.476 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |      12 |                         30.616 |      30.422 |      30.823 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |      12 |                         55.026 |      54.809 |      55.575 |       5 | 10,050 | 10,050 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Jolt Physics        |      14 |                    unavailable | unavailable | unavailable |       0 | 10,050 | 10,050 |       0 |           0 |
| Entasis             |      14 |                          3.601 |       3.593 |       3.617 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |      14 |                          5.689 |       5.522 |       5.847 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |      14 |                          6.020 |       5.929 |       6.086 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |      14 |                          6.330 |       6.299 |       6.352 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |      14 |                          6.791 |       6.672 |       6.854 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |      14 |                         10.044 |      10.017 |      10.112 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |      14 |                         16.835 |      16.647 |      17.166 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |      14 |                         20.286 |      20.164 |      20.412 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |      14 |                         29.828 |      29.694 |      30.000 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |      14 |                         54.037 |      53.601 |      54.498 |       5 | 10,050 | 10,050 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Jolt Physics        |      16 |                    unavailable | unavailable | unavailable |       0 | 10,050 | 10,050 |       0 |           0 |
| Entasis             |      16 |                          3.523 |       3.487 |       3.591 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |      16 |                          5.388 |       5.338 |       5.429 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |      16 |                          5.602 |       5.465 |       5.727 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |      16 |                          5.899 |       5.891 |       5.919 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |      16 |                          6.552 |       6.541 |       6.597 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |      16 |                          9.564 |       9.560 |       9.602 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |      16 |                         16.930 |      16.594 |      17.357 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |      16 |                         20.504 |      20.326 |      20.744 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |      16 |                         29.393 |      29.373 |      29.429 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |      16 |                         53.078 |      52.408 |      54.832 |       5 | 10,050 | 10,050 |       0 |           0 |
|                     |         |                                |             |             |         |        |        |         |             |
| Jolt Physics        |      24 |                    unavailable | unavailable | unavailable |       0 | 10,050 | 10,050 |       0 |           0 |
| Entasis             |      24 |                          3.045 |       2.986 |       3.073 |       5 | 10,050 | 10,050 |       0 |           0 |
| BEPUphysics         |      24 |                          5.739 |       5.079 |       5.774 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unity DOTS Physics  |      24 |                          8.312 |       8.296 |       8.373 |       5 | 10,050 | 10,050 |       0 |           0 |
| Vite's PhysX 3.4    |      24 |                         13.889 |      13.854 |      14.071 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX        |      24 |                         14.621 |      14.494 |      14.665 |       5 | 10,050 | 10,050 |       0 |           0 |
| NVIDIA PhysX 5      |      24 |                         15.581 |      15.417 |      15.768 |       5 | 10,050 | 10,050 |       0 |           0 |
| Rapier3D            |      24 |                         16.283 |      15.652 |      16.534 |       5 | 10,050 | 10,050 |       0 |           0 |
| Box3D               |      24 |                         21.097 |      20.919 |      21.213 |       5 | 10,050 | 10,050 |       0 |           0 |
| Unreal Engine Chaos |      24 |                         29.081 |      29.072 |      29.119 |       5 | 10,050 | 10,050 |       0 |           0 |
| Avian3D             |      24 |                         51.506 |      50.510 |      52.924 |       5 | 10,050 | 10,050 |       0 |           0 |

## Assessment diagnostics

<details>
<summary>Show saved assessment details</summary>

Physical rows identify the determining saved repeat for each engine/thread configuration. In diagnostic identities, t is the thread count, r is the zero-based repeat index, and s is the capture segment. Execution diagnostics are in [manifest.json](manifest.json), all recorded physical metrics are in [stability.csv](stability.csv)

| Engine | Threads | Diagnostic |
| --- | ---: | --- |
| Entasis | 1 | Entasis t1 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195019121 m, limit 0.1 m |
| BEPUphysics | 1 | BEPUphysics t1 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0194682709 m, limit 0.1 m |
| Vite's PhysX 3.4 | 1 | Vite's PhysX 3.4 t1 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399747156 m, limit 0.1 m |
| NVIDIA PhysX | 1 | NVIDIA PhysX t1 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| Jolt Physics | 1 | Jolt Physics t1 r0: box fell below current support plane, body 5720 at s0 measured 207, assessed s0 construction 0 to s0 measured 300. final 1s measured window 240-300: shape 22.2593021 m, limit 0.1 m |
| NVIDIA PhysX 5 | 1 | NVIDIA PhysX 5 t1 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Rapier3D | 1 | Rapier3D t1 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Box3D | 1 | Box3D t1 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unity DOTS Physics | 1 | Unity DOTS Physics t1 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Unreal Engine Chaos | 1 | Unreal Engine Chaos t1 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 1 | Avian3D t1 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 2 | Jolt Physics t2 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 2 | Entasis t2 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0193823491 m, limit 0.1 m |
| BEPUphysics | 2 | BEPUphysics t2 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195204861 m, limit 0.1 m |
| Vite's PhysX 3.4 | 2 | Vite's PhysX 3.4 t2 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399913361 m, limit 0.1 m |
| NVIDIA PhysX | 2 | NVIDIA PhysX t2 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| NVIDIA PhysX 5 | 2 | NVIDIA PhysX 5 t2 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Rapier3D | 2 | Rapier3D t2 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Box3D | 2 | Box3D t2 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unity DOTS Physics | 2 | Unity DOTS Physics t2 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Unreal Engine Chaos | 2 | Unreal Engine Chaos t2 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 2 | Avian3D t2 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 3 | Jolt Physics t3 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 3 | Entasis t3 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0194357377 m, limit 0.1 m |
| BEPUphysics | 3 | BEPUphysics t3 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195204861 m, limit 0.1 m |
| Vite's PhysX 3.4 | 3 | Vite's PhysX 3.4 t3 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399913361 m, limit 0.1 m |
| NVIDIA PhysX | 3 | NVIDIA PhysX t3 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| NVIDIA PhysX 5 | 3 | NVIDIA PhysX 5 t3 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Rapier3D | 3 | Rapier3D t3 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Unity DOTS Physics | 3 | Unity DOTS Physics t3 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Box3D | 3 | Box3D t3 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unreal Engine Chaos | 3 | Unreal Engine Chaos t3 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 3 | Avian3D t3 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 4 | Jolt Physics t4 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 4 | Entasis t4 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0192544095 m, limit 0.1 m |
| BEPUphysics | 4 | BEPUphysics t4 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195204861 m, limit 0.1 m |
| Vite's PhysX 3.4 | 4 | Vite's PhysX 3.4 t4 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399913361 m, limit 0.1 m |
| NVIDIA PhysX | 4 | NVIDIA PhysX t4 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| NVIDIA PhysX 5 | 4 | NVIDIA PhysX 5 t4 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Unity DOTS Physics | 4 | Unity DOTS Physics t4 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Rapier3D | 4 | Rapier3D t4 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Box3D | 4 | Box3D t4 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unreal Engine Chaos | 4 | Unreal Engine Chaos t4 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 4 | Avian3D t4 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 5 | Jolt Physics t5 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 5 | Entasis t5 r0: safety and shape passed. final 1s measured window 240-300: shape 0.019321255 m, limit 0.1 m |
| BEPUphysics | 5 | BEPUphysics t5 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195204861 m, limit 0.1 m |
| Vite's PhysX 3.4 | 5 | Vite's PhysX 3.4 t5 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399913361 m, limit 0.1 m |
| NVIDIA PhysX | 5 | NVIDIA PhysX t5 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| NVIDIA PhysX 5 | 5 | NVIDIA PhysX 5 t5 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Unity DOTS Physics | 5 | Unity DOTS Physics t5 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Rapier3D | 5 | Rapier3D t5 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Box3D | 5 | Box3D t5 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unreal Engine Chaos | 5 | Unreal Engine Chaos t5 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 5 | Avian3D t5 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 6 | Jolt Physics t6 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 6 | Entasis t6 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0191189285 m, limit 0.1 m |
| BEPUphysics | 6 | BEPUphysics t6 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195204861 m, limit 0.1 m |
| Vite's PhysX 3.4 | 6 | Vite's PhysX 3.4 t6 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399913361 m, limit 0.1 m |
| NVIDIA PhysX | 6 | NVIDIA PhysX t6 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| NVIDIA PhysX 5 | 6 | NVIDIA PhysX 5 t6 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Unity DOTS Physics | 6 | Unity DOTS Physics t6 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Rapier3D | 6 | Rapier3D t6 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Box3D | 6 | Box3D t6 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unreal Engine Chaos | 6 | Unreal Engine Chaos t6 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 6 | Avian3D t6 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 8 | Jolt Physics t8 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 8 | Entasis t8 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195312178 m, limit 0.1 m |
| BEPUphysics | 8 | BEPUphysics t8 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195204861 m, limit 0.1 m |
| Vite's PhysX 3.4 | 8 | Vite's PhysX 3.4 t8 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399913361 m, limit 0.1 m |
| NVIDIA PhysX | 8 | NVIDIA PhysX t8 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| NVIDIA PhysX 5 | 8 | NVIDIA PhysX 5 t8 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Unity DOTS Physics | 8 | Unity DOTS Physics t8 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Rapier3D | 8 | Rapier3D t8 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Box3D | 8 | Box3D t8 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unreal Engine Chaos | 8 | Unreal Engine Chaos t8 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 8 | Avian3D t8 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 10 | Jolt Physics t10 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 10 | Entasis t10 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0192213737 m, limit 0.1 m |
| BEPUphysics | 10 | BEPUphysics t10 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195204861 m, limit 0.1 m |
| Vite's PhysX 3.4 | 10 | Vite's PhysX 3.4 t10 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399913361 m, limit 0.1 m |
| NVIDIA PhysX | 10 | NVIDIA PhysX t10 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| NVIDIA PhysX 5 | 10 | NVIDIA PhysX 5 t10 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Unity DOTS Physics | 10 | Unity DOTS Physics t10 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Rapier3D | 10 | Rapier3D t10 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Box3D | 10 | Box3D t10 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unreal Engine Chaos | 10 | Unreal Engine Chaos t10 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 10 | Avian3D t10 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 12 | Jolt Physics t12 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 12 | Entasis t12 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0191623593 m, limit 0.1 m |
| Vite's PhysX 3.4 | 12 | Vite's PhysX 3.4 t12 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399913361 m, limit 0.1 m |
| BEPUphysics | 12 | BEPUphysics t12 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195204861 m, limit 0.1 m |
| NVIDIA PhysX | 12 | NVIDIA PhysX t12 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| NVIDIA PhysX 5 | 12 | NVIDIA PhysX 5 t12 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Unity DOTS Physics | 12 | Unity DOTS Physics t12 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Rapier3D | 12 | Rapier3D t12 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Box3D | 12 | Box3D t12 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unreal Engine Chaos | 12 | Unreal Engine Chaos t12 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 12 | Avian3D t12 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 14 | Jolt Physics t14 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 14 | Entasis t14 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0192999027 m, limit 0.1 m |
| Vite's PhysX 3.4 | 14 | Vite's PhysX 3.4 t14 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399913361 m, limit 0.1 m |
| BEPUphysics | 14 | BEPUphysics t14 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195204861 m, limit 0.1 m |
| NVIDIA PhysX | 14 | NVIDIA PhysX t14 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| NVIDIA PhysX 5 | 14 | NVIDIA PhysX 5 t14 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Unity DOTS Physics | 14 | Unity DOTS Physics t14 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Rapier3D | 14 | Rapier3D t14 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Box3D | 14 | Box3D t14 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unreal Engine Chaos | 14 | Unreal Engine Chaos t14 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 14 | Avian3D t14 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 16 | Jolt Physics t16 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 16 | Entasis t16 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0194026232 m, limit 0.1 m |
| Vite's PhysX 3.4 | 16 | Vite's PhysX 3.4 t16 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399913361 m, limit 0.1 m |
| BEPUphysics | 16 | BEPUphysics t16 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195204861 m, limit 0.1 m |
| NVIDIA PhysX | 16 | NVIDIA PhysX t16 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| NVIDIA PhysX 5 | 16 | NVIDIA PhysX 5 t16 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Unity DOTS Physics | 16 | Unity DOTS Physics t16 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Rapier3D | 16 | Rapier3D t16 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Box3D | 16 | Box3D t16 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unreal Engine Chaos | 16 | Unreal Engine Chaos t16 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 16 | Avian3D t16 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 24 | Jolt Physics t24 r0: unassessed (saved trajectory evidence unavailable) |
| Entasis | 24 | Entasis t24 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0192746658 m, limit 0.1 m |
| BEPUphysics | 24 | BEPUphysics t24 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0195204861 m, limit 0.1 m |
| Unity DOTS Physics | 24 | Unity DOTS Physics t24 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0560929198 m, limit 0.1 m |
| Vite's PhysX 3.4 | 24 | Vite's PhysX 3.4 t24 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0399913361 m, limit 0.1 m |
| NVIDIA PhysX | 24 | NVIDIA PhysX t24 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0276767303 m, limit 0.1 m |
| NVIDIA PhysX 5 | 24 | NVIDIA PhysX 5 t24 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0274896277 m, limit 0.1 m |
| Rapier3D | 24 | Rapier3D t24 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0165325163 m, limit 0.1 m |
| Box3D | 24 | Box3D t24 r0: safety and shape passed. final 1s measured window 240-300: shape 0.00835413806 m, limit 0.1 m |
| Unreal Engine Chaos | 24 | Unreal Engine Chaos t24 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0510383121 m, limit 0.1 m |
| Avian3D | 24 | Avian3D t24 r0: safety and shape passed. final 1s measured window 240-300: shape 0.0408734986 m, limit 0.1 m |
| Jolt Physics | 1 | Jolt Physics t1: 1/5 measured repeats, 4 skipped. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 2 | Jolt Physics t2: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 3 | Jolt Physics t3: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 4 | Jolt Physics t4: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 5 | Jolt Physics t5: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 6 | Jolt Physics t6: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 8 | Jolt Physics t8: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 10 | Jolt Physics t10: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 12 | Jolt Physics t12: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 14 | Jolt Physics t14: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 16 | Jolt Physics t16: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |
| Jolt Physics | 24 | Jolt Physics t24: 0/5 measured repeats, 5 skipped. Skipped after failed repeat. Measurements unavailable. Skipped after failed repeat: engine=joltphysics thread=1 repeat=0 reason=stack&#95;assessment&#95;failed evaluated |

</details>

## Execution outcomes

Completed with failures. Costs describe completed measurements only, unavailable measurements are not zeroes. Original tuple identities and diagnostics are retained in [manifest.json](manifest.json)

| Engine | Threads | Repeat | Stage | Outcome | Reason | Exit code |
| --- | ---: | ---: | --- | --- | --- | ---: |
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

## Route Notes

The Result Summary table comes from `summary.csv`, aggregated from `normalized.csv`. Open `PhysicsArena.exe report <result-dir>` and choose **Regenerate report** to refresh it
