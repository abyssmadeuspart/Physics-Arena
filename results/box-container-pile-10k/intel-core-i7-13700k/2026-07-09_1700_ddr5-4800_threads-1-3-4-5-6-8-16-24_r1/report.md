# Box Container Pile 10k Benchmark Report

## Evidence

| Field            | Value                                                                                                                                                                                                      |
| :--------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Selected run id  | `2026-07-09_1700_ddr5-4800_threads-1-3-4-5-6-8-16-24_r1`                                                                                                                                                   |
| Result folder    | `results/box-container-pile-10k/intel-core-i7-13700k/2026-07-09_1700_ddr5-4800_threads-1-3-4-5-6-8-16-24_r1/`                                                                                              |
| Case id          | `box_container_pile_10k`                                                                                                                                                                                   |
| Benchmark mode   | `headless_api`                                                                                                                                                                                             |
| Thread counts    | `1, 3, 4, 5, 6, 8, 16, 24`                                                                                                                                                                                 |
| Step count       | `300`                                                                                                                                                                                                      |
| Warmup steps     | `30`                                                                                                                                                                                                       |
| Host             | Intel Core i7-13700K (8P+8E / 24T); max 3.40 GHz; 64 GB DDR5 4800 MHz; Micro-Star International Co., Ltd. PRO Z790-A WIFI (MS-7E07) BIOS A.E0; Microsoft Windows 11 Pro for Workstations 64-bit 10.0.26200 |
| Physics settings | Physics solver iterations: 4; rigid-body sleeping: disabled.                                                                                                                                               |
| Build settings   | Unreal Engine Chaos: Unreal Engine release branch Chaos Program target; Win64 Shipping.                                                                                                                    |
| Data source      | `summary.csv` generated from `normalized.csv`                                                                                                                                                              |
| Chart            | `summary.svg`                                                                                                                                                                                              |
| Run route        | windows                                                                                                                                                                                                    |

## Result Summary

Main metric: lower median Physics ms/frame is better. Rows are grouped by thread count and sorted fastest to slowest within each thread group.

| Engine              | Threads | Physics ms/frame median | Physics ms/frame mean | Fastest repeat ms/frame | Steps/s mean | Repeats | Bodies | Shapes |
| :------------------ | ------: | ----------------------: | --------------------: | ----------------------: | -----------: | ------: | -----: | -----: |
| Unreal Engine Chaos |       1 |                1052.086 |              1052.086 |                1052.086 |        0.950 |       1 | 10,005 | 10,005 |
|                     |         |                         |                       |                         |              |         |        |        |
| Unreal Engine Chaos |       3 |                1051.412 |              1051.412 |                1051.412 |        0.951 |       1 | 10,005 | 10,005 |
|                     |         |                         |                       |                         |              |         |        |        |
| Unreal Engine Chaos |       4 |                 951.364 |               951.364 |                 951.364 |        1.051 |       1 | 10,005 | 10,005 |
|                     |         |                         |                       |                         |              |         |        |        |
| Unreal Engine Chaos |       5 |                 857.892 |               857.892 |                 857.892 |        1.166 |       1 | 10,005 | 10,005 |
|                     |         |                         |                       |                         |              |         |        |        |
| Unreal Engine Chaos |       6 |                 791.965 |               791.965 |                 791.965 |        1.263 |       1 | 10,005 | 10,005 |
|                     |         |                         |                       |                         |              |         |        |        |
| Unreal Engine Chaos |       8 |                 704.660 |               704.660 |                 704.660 |        1.419 |       1 | 10,005 | 10,005 |
|                     |         |                         |                       |                         |              |         |        |        |
| Unreal Engine Chaos |      16 |                 615.894 |               615.894 |                 615.894 |        1.624 |       1 | 10,005 | 10,005 |
|                     |         |                         |                       |                         |              |         |        |        |
| Unreal Engine Chaos |      24 |                 581.246 |               581.246 |                 581.246 |        1.720 |       1 | 10,005 | 10,005 |

## Route Notes

The table above is generated from `summary.csv`; `summary.csv` is generated from `normalized.csv`. Regenerate the report with `./bench.sh report <result-dir>` after a new benchmark run.
