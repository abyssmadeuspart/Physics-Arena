# Physics Arena sources

Source revisions, runtime requirements and licenses for the included Windows x64 packages. See the [README](README.md) for installation and launch instructions

## Runtime requirements

- Windows x64 with an OpenGL 3.3-capable graphics driver and `PhysicsArena.exe` at the repository root
- The Microsoft Visual C++ x64 runtime (`MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`) for the application. These components are not bundled
- The same Visual C++ DLLs for Box3D, Vite PhysX 3.4, NVIDIA PhysX 5 and Unreal Chaos. Rapier and Avian also import `VCRUNTIME140.dll`
- An x86-64-v3 CPU with OS-enabled XMM/YMM state for Entasis: AVX, AVX2, FMA, BMI1, BMI2, LZCNT and POPCNT. Its package imports only Windows system libraries

The included packages need no engine checkout, compiler, Unity Editor or Unreal Engine installation. BEPUphysics v2 includes its .NET 10 runtime. The Odin compiler and `LLVM-C.dll` are build dependencies only

## Release packages

Each engine package lives under `release/windows-x64/<package>/`. Its `artifact-manifest.json` identifies the executable and required runtime files. The application uses a [schema-3 manifest](release/windows-x64/physics_arena/artifact-manifest.json)

Packages contain runners and runtime files. They exclude upstream source trees, development caches, compiler intermediates, PDBs and map files. Rebuilt binaries omit the workstation checkout prefix from embedded diagnostics. Supplied third-party runtime files can retain original upstream diagnostic paths

## Application build dependencies

| Dependency                                                        | Ref/version used                                              | License                          | Integration                                                                                                                                              |
| :---------------------------------------------------------------- | :------------------------------------------------------------ | :------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [nlohmann/json](https://github.com/nlohmann/json)                 | `v3.12.0`, commit `55f93686c01528224f448c19128836e7df245f72`  | MIT                              | Native configuration parsing, with no dependency source vendored or packaged                                                                             |
| [Vince's CSV Parser](https://github.com/vincentlaucsb/csv-parser) | `5.3.0`, commit `32e99be14236b0585f33fb96f37e4fefc363f448`    | MIT                              | Static CSV reader with background parsing disabled, with no dependency source vendored or packaged                                                       |
| [raylib](https://github.com/raysan5/raylib)                       | 6.0 Windows x64 MSVC static archive                           | Zlib                             | Window, OpenGL 3.3 context and rlgl rendering with the bundled GLFW platform                                                                             |
| [Zstandard](https://github.com/facebook/zstd/tree/v1.5.7)         | v1.5.7, commit `f8745da6ff1ad1e7bab384bd1f9d742439278e99`     | BSD-3-Clause, upstream `LICENSE` | Host-only static C library and upstream seekable extension, compiled with the host clang-cl toolchain and dynamic CRT, with compression workers disabled |
| [rlImGui](https://github.com/raylib-extras/rlImGui)               | Raylib_6_0, commit `3bc5731c4216bb8caa67fbea24aa85ce80d57ccb` | Zlib                             | Low-level ImGui draw-data and texture backend, built with the application's 32-bit indices                                                               |
| [Dear ImGui](https://github.com/ocornut/imgui)                    | Commit `776bf2ab0d61e719e58f2c6d27d109ab5dcf2af1`             | MIT                              | Static Run/Results interface with its Win32 backend for queued keyboard, mouse and clipboard interaction                                                 |
| [ImPlot](https://github.com/epezent/implot)                       | Commit `d65a2bef53d32502407de3a4be80f191e2f412d7`             | MIT                              | Static result plotting                                                                                                                                   |

The [core CMake project](src/physics_arena/CMakeLists.txt) checks pinned JSON, CSV and Zstd source inputs. The [rendering project](src/rendering/CMakeLists.txt) uses the pinned raylib archive and pinned UI dependencies

The replay renderer adapts raylib 6.0 commit `dbc56a87da87d973a9c5baa4e7438a9d20121d28`: `examples/shaders/shaders_mesh_instancing.c`, `shaders_shadowmap_rendering.c` and `resources/shaders/glsl330/{lighting_instancing.vs,shadowmap.vs,shadowmap.fs}`. The altered shaders add instance normals and matte directional lighting. They remove specular highlights, shadow sampling and depth attachments. Shadowmap example copyright (c) 2023-2025 TheManTheMythTheGameDev (@TheManTheMythTheGameDev), reviewed by Ramon Santamaria

```text
Copyright (c) 2013-2026 Ramon Santamaria (@raysan5)

This software is provided "as-is", without any express or implied warranty. In no event
will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial
applications, and to alter it and redistribute it freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not claim that you
  wrote the original software. If you use this software in a product, an acknowledgment
  in the product documentation would be appreciated but is not required.

  2. Altered source versions must be plainly marked as such, and must not be misrepresented
  as being the original software.

  3. This notice may not be removed or altered from any source distribution.
```

## Engine sources

| Engine ID                                   | Release package                          | Source                                                                    | Pinned ref/version                                                                      | License/provenance                                                                    | Source manifest                            |
| :------------------------------------------ | :--------------------------------------- | :------------------------------------------------------------------------ | :-------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------ | :----------------------------------------- |
| <a id="box3d"></a>`box3d`                   | `release/windows-x64/box3d/`             | <https://github.com/erincatto/box3d>                                      | `9e5a4cde862fba95ff19f096b79567f3ea6c01fd`, CMake project `0.1.0`, post-`v0.1.0` source | MIT                                                                                   | [manifest](src/box3d/engine.json)          |
| <a id="joltphysics"></a>`joltphysics`       | `release/windows-x64/joltphysics/`       | <https://github.com/jrouwe/joltphysics>                                   | `82b24442917ba8d9ced3582c2f2d8a49f4c969f6`, Jolt `5.6.1-dev` source                     | MIT                                                                                   | [manifest](src/joltphysics/engine.json)    |
| <a id="bepuphysics2"></a>`bepuphysics2`     | `release/windows-x64/bepuphysics2/`      | <https://github.com/bepu/bepuphysics2>                                    | `2.5.0-beta.24`, commit `c230dd1178d6f481d8b3f03c0f595f8ad910b725`                      | Apache-2.0                                                                            | [manifest](src/bepuphysics2/engine.json)   |
| <a id="rapier3d"></a>`rapier3d`             | `release/windows-x64/rapier3d/`          | Rapier3D: <https://github.com/dimforge/rapier>                            | `28d0ba929b460597f0959fe600c7afd65612f6f9`, Rapier `0.35.3`                             | Apache-2.0                                                                            | [manifest](src/rapier3d/engine.json)       |
| <a id="avian3d"></a>`avian3d`               | `release/windows-x64/avian3d/`           | Avian3D: <https://github.com/avianphysics/avian>                          | `47d5948b2a8ee829848fbc3d4ce5e44ad0fd863f`, Avian `0.8.0-dev`, Bevy `0.19.0`            | MIT OR Apache-2.0                                                                     | [manifest](src/avian3d/engine.json)        |
| <a id="unity_physics"></a>`unity_physics`   | `release/windows-x64/unity_physics/`     | Repo Unity project source                                                 | Unity `6000.5.2f1`, `com.unity.physics` `6.5.0`                                         | Unity package terms                                                                   | [manifest](src/unity_physics/engine.json)  |
| <a id="physx34"></a>`physx34`               | `release/windows-x64/physx34/`           | <https://github.com/GapingPixel/UnrealEngineVite-PhysX>                   | `2b13cae09734616d07d09ecf645326fa0bf43ef7`, PhysX `3.4.0`                               | Original proprietary NVIDIA notice, redistribution grant not established              | [manifest](src/physx34/engine.json)        |
| <a id="nvidia_physx34"></a>`nvidia_physx34` | `release/windows-x64/nvidia-physx-3.4/`  | Official NVIDIA PhysX 3.4: <https://github.com/NVIDIAGameWorks/PhysX-3.4> | PhysX `3.4.2`                                                                           | BSD-3-Clause                                                                          | [manifest](src/nvidia_physx34/engine.json) |
| <a id="nvidia_physx5"></a>`nvidia_physx5`   | `release/windows-x64/nvidia-physx-5.10/` | Official NVIDIA PhysX 5.10: <https://github.com/NVIDIA-Omniverse/PhysX>   | PhysX `5.10.0.75b25f28f9`                                                               | BSD-3-Clause                                                                          | [manifest](src/nvidia_physx5/engine.json)  |
| <a id="unreal_chaos"></a>`unreal_chaos`     | `release/windows-x64/unreal_chaos/`      | Unreal Engine Chaos: `git@github.com:EpicGames/UnrealEngine.git`          | `7deeb413d3dc1fc034f48d1aacc0861301829d32`                                              | Unreal Engine EULA                                                                    | [manifest](src/unreal_chaos/engine.json)   |
| <a id="entasis"></a>`entasis`               | `release/windows-x64/entasis/`           | <https://github.com/Abyssal-Engine/Entasis>                               | `c380d34c57cf83e1cb481d712de4460d16c08774`, report version `1.0.0`                      | Apache-2.0, upstream `LICENSE`/`NOTICE`, Odin license and compiler-rt notice included | [manifest](src/entasis/engine.json)        |

Distribution notice text is retained in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt), with original Rust standard-library notices in the [Rapier](release/windows-x64/rapier3d/Rust-COPYRIGHT-library.html) and [Avian](release/windows-x64/avian3d/Rust-COPYRIGHT-library.html) packages. Entasis retains its upstream license and notice files in its package

The Vite PhysX fork retains an original NVIDIA proprietary notice requiring a separate license grant. Its presence in this tree does not establish redistribution permission. Unity and Unreal packages retain their upstream licensing terms. Included Unity DirectStorage 1.3.0 DLLs retain Microsoft's accompanying third-party notices in the notice file above

## Release tooling

| Package ID       | Toolchain recorded for the checked-in package                                                                                                                                                                                                                                                 |
| :--------------- | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `physics_arena`  | `clang-cl` / LLVM `22.1.8`, CMake `4.3` + Ninja `Release`, LLD, statically linked raylib 6.0, rlImGui, Dear ImGui and ImPlot, OpenGL 3.3 renderer, imports the dynamic Visual C++ runtime (MSVCP140, VCRUNTIME140 and VCRUNTIME140_1) and the Windows Universal CRT                           |
| `box3d`          | `clang-cl` / LLVM `22.1.8`, CMake + Ninja release route, Box3D is linked into the native runner, which imports the Microsoft Visual C++ runtime                                                                                                                                               |
| `joltphysics`    | `clang-cl` / LLVM `22.1.8`, CMake + Ninja `Distribution` route, statically linked Windows x64 executable                                                                                                                                                                                      |
| `bepuphysics2`   | .NET SDK `10.0.301`, `net10.0` self-contained Win-x64 apphost, includes `Microsoft.NETCore.App 10.0.9`, BEPU `ReleaseNoProfiling`                                                                                                                                                             |
| `rapier3d`       | `rustc 1.89.0`, `cargo 1.89.0`, Cargo `release`, Rapier `0.35.3`, `parallel` feature                                                                                                                                                                                                          |
| `avian3d`        | `rustc 1.95.0`, `cargo 1.95.0`, Cargo `release`, Avian `0.8.0-dev`, Bevy `0.19.0`, features `3d,parry-f32,parallel,simd,xpbd_joints`                                                                                                                                                          |
| `unity_physics`  | Unity `6000.5.2f1`, IL2CPP `StandaloneWindows64`, Burst enabled, safety checks disabled, packages: Physics `6.5.0`, Entities `6.5.0`, Burst `1.8.29`, Collections `6.5.0`, Mathematics `1.4.0`                                                                                                |
| `physx34`        | `clang-cl` / LLVM `22.1.8`, CMake `4.3.1-msvc1`, Ninja `1.13.2`, LLD `22.1.8`, Vite PhysX `3.4.0` runtime DLLs                                                                                                                                                                                |
| `nvidia_physx34` | Visual Studio 2026 MSBuild release, PhysX `3.4.2`, SDK toolchain id `vs2026_msbuild_release`, package uses the `vc15win64` SDK layout                                                                                                                                                         |
| `nvidia_physx5`  | PhysX `5.10.0.75b25f28f9`, Visual Studio 2022 MSBuild Release SDK (`vs2022_msbuild_release`, MSVC `19.44.35228.0`), Visual Studio 2026 MSVC Release runner, both `/MD`, `vc17win64-cpu-only` preset                                                                                           |
| `unreal_chaos`   | Unreal Engine Chaos, UnrealBuildTool Program target, Win64 Shipping, `cl.exe`, Windows SDK `rc.exe`                                                                                                                                                                                           |
| `entasis`        | Complete prebuilt Odin bundle `dev-2026-09-nightly:a2fb372`, upstream Release profile: `-target:windows_amd64 -microarch:x86-64-v3 -o:speed -no-bounds-check -disable-assert -source-code-locations:none -vet -warnings-as-errors -linker:lld`. Native Odin source linked into one executable |

Engine manifests above own source preparation and build settings. The [Entasis adapter guide](src/entasis/README.md) documents its world lifecycle and timing boundaries. Build success alone does not establish physical quality or benchmark performance

## Pyramid Wall and Heavy Ray Tracing

Pyramid Wall ports the pinned Box3D `shared/benchmarks.c:CreateLargePyramid` fixture, whose upstream recipe has 90 triangular rows and 4,095 cubes. The Polygon default has 180 triangular rows and 16,290 one-metre cubes, with density 100 kg/m3, friction 0.6, gravity 10 m/s2 and no projectile

Polygon uses discrete collision, matching Box3D's `-nc` option, and disables sleeping. One warmup step and 300 measured steps share the same world. These choices differ from upstream default continuous collision detection (CCD) timings and minimum-of-runs aggregation. Large Pyramid remains a separate square stepped fixture with projectiles

Heavy Ray Tracing uses six 1920 x 1080 views of 65,536 primitives and 1,024 meshes containing 1,048,576 triangles. Entasis uses public context queries and `query_batch`, while Box3D uses native world ray callbacks. Canonical segments map to Entasis direction/maximum-t and Box3D translation/fraction, with returned distances converted to metres

Box3D cooking reorders triangles, so import restores canonical source IDs. Entasis reverses imported triangle winding to preserve canonical front faces

Box3D returns only the nearest triangle per mesh, supporting complete collider enumeration but not mesh-surface enumeration. Entasis reduces all-hit events per collider inside enumeration timing and reports full mesh events separately. Its pinned batch API is a scalar loop, so the API label does not establish SIMD execution

`setup_ms` covers native world/shape import, cooking, source-ID mapping and dispatcher/query-context initialization after loading and checking the scene. Corpus I/O and phase-buffer reservation are outside this clock. Output, scratch, batch and streamed-input buffers are reserved before warmup from the six-view maxima and retained until teardown. Reported buffer bytes include unused retained capacity

The host generates geometry and rays from a fixed recipe. A float64 analytic and triangle reference checks a stratified subset of the canonical float32 inputs without traversing a bounding volume hierarchy (BVH). Reference generation, conditioning, validation and image composition are outside native query clocks. Dispatch, traversal, filtering, returned-surface mapping and worker completion are included

The headline measures ordinary coherent primary rays. Other phases retain their own ray counts, API modes and capability limits. Peak process committed memory includes native world and adapter storage, so it cannot isolate acceleration-structure allocation. Saved native-hit images provide shaded, depth, normals and error channels in Replay
