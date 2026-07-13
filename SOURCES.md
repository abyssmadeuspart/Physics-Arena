# Physics Arena Sources

Pinned source, package, license, and toolchain details for the checked-in Windows x64 executables.

## Run Requirements

- A shell that can launch Windows x64 executables from `release/windows-x64/`.
- Python 3.10+ from `PYTHON_EXE`, `python3`, `python`, or `py -3`.
- No engine source checkout is required for the checked-in release packages.
- BEPUphysics v2 is self-contained; running `release/windows-x64/bepuphysics2/` does not require an installed .NET 10 runtime.

## Release Packages

Each public engine package lives under `release/windows-x64/<package>/` and has an `artifact-manifest.json` that identifies the executable and required sidecars.

The release packages contain the runner executable plus required runtime files. They do not contain upstream source trees, sample projects, editor caches, Cargo caches, package caches, compiler intermediates, PDB files, map files, private machine paths, or downloaded dependency caches.

## Engine Sources

| Engine ID | Release package | Source | Pinned ref/version | License/provenance | Source manifest |
| :-------- | :-------------- | :----- | :----------------- | :----------------- | :-------------- |
| <a id="box3d"></a>`box3d` | `release/windows-x64/box3d/` | <https://github.com/erincatto/box3d> | `8441b4a06d6d09dcfb0b0f704df4d847d1437b92` | MIT | `src/box3d/engine.json` |
| <a id="joltphysics"></a>`joltphysics` | `release/windows-x64/joltphysics/` | <https://github.com/jrouwe/joltphysics> | `3b47ec390cb9b933769183eff599f72195c5873c` | MIT | `src/joltphysics/engine.json` |
| <a id="bepuphysics2"></a>`bepuphysics2` | `release/windows-x64/bepuphysics2/` | <https://github.com/bepu/bepuphysics2> | `f73164bb3c9ca733eb3329f1f6b1cea4e216ece7` | Apache-2.0 | `src/bepuphysics2/engine.json` |
| <a id="rapier3d"></a>`rapier3d` | `release/windows-x64/rapier3d/` | Rapier3D: <https://github.com/dimforge/rapier> | `a1ef31035613154dfb97a9e1d480c6a5eb9d0010`, Rapier `0.34.0` | Apache-2.0 | `src/rapier3d/engine.json` |
| <a id="avian3d"></a>`avian3d` | `release/windows-x64/avian3d/` | Avian3D: <https://github.com/avianphysics/avian> | `fc99fdcdbff804fbbe6dc1eb7fc4137e677853d2`, Avian `0.8.0-dev`, Bevy `0.19.0` | MIT OR Apache-2.0 | `src/avian3d/engine.json` |
| <a id="unity_physics"></a>`unity_physics` | `release/windows-x64/unity_physics/` | Repo Unity project source | Unity `6000.5.2f1`, `com.unity.physics` `6.5.0` | Unity package terms | `src/unity_physics/engine.json` |
| <a id="physx34"></a>`physx34` | `release/windows-x64/physx34/` | <https://github.com/GapingPixel/UnrealEngineVite-PhysX> | `2b13cae09734616d07d09ecf645326fa0bf43ef7`, PhysX `3.4.0` | BSD-3-Clause PhysX provenance | `src/physx34/engine.json` |
| <a id="nvidia_physx34"></a>`nvidia_physx34` | `release/windows-x64/nvidia-physx-3.4/` | Official NVIDIA PhysX 3.4: <https://github.com/NVIDIAGameWorks/PhysX-3.4> | PhysX `3.4.2` | BSD-3-Clause | `src/nvidia_physx34/engine.json` |
| <a id="nvidia_physx5"></a>`nvidia_physx5` | `release/windows-x64/nvidia-physx-5.6/` | Official NVIDIA PhysX 5.6: <https://github.com/NVIDIA-Omniverse/PhysX> | PhysX `5.6.1.51c1f783` | BSD-3-Clause | `src/nvidia_physx5/engine.json` |
| <a id="unreal_chaos"></a>`unreal_chaos` | `release/windows-x64/unreal_chaos/` | Unreal Engine Chaos: `git@github.com:EpicGames/UnrealEngine.git` | `7deeb413d3dc1fc034f48d1aacc0861301829d32` | Unreal Engine EULA | `src/unreal_chaos/engine.json` |

## Release Tooling

| Engine ID | Toolchain recorded for the checked-in package |
| :-------- | :-------------------------------------------- |
| `box3d` | `clang-cl` / LLVM `22.1.8`; CMake + Ninja release route; statically linked Windows x64 executable. |
| `joltphysics` | `clang-cl` / LLVM `22.1.8`; CMake + Ninja `Distribution` route; statically linked Windows x64 executable. |
| `bepuphysics2` | .NET SDK `10.0.301`; `net10.0` self-contained Win-x64 apphost; includes `Microsoft.NETCore.App 10.0.9`; BEPU `ReleaseNoProfiling`. |
| `rapier3d` | `rustc 1.89.0`, `cargo 1.89.0`; Cargo `release`; Rapier `0.34.0`; `parallel` feature. |
| `avian3d` | `rustc 1.95.0`, `cargo 1.95.0`; Cargo `release`; Avian `0.8.0-dev`; Bevy `0.19.0`; features `3d,f32,parry-f32,parallel,simd`. |
| `unity_physics` | Unity `6000.5.2f1`; IL2CPP `StandaloneWindows64`; Burst enabled; safety checks disabled; packages: Physics `6.5.0`, Entities `6.5.0`, Burst `1.8.29`, Collections `6.5.0`, Mathematics `1.4.0`. |
| `physx34` | `clang-cl` / LLVM `22.1.8`; CMake `4.3.1-msvc1`; Ninja `1.13.2`; LLD `22.1.8`; Vite PhysX `3.4.0` runtime DLLs. |
| `nvidia_physx34` | Visual Studio 2026 MSBuild release; PhysX `3.4.2`; SDK toolchain id `vs2026_msbuild_release`; package uses the `vc15win64` SDK layout. |
| `nvidia_physx5` | Visual Studio 2026 MSBuild release; PhysX `5.6.1.51c1f783`; SDK toolchain id `vs2026_msbuild_release`; `vc17win64-cpu-only` preset. |
| `unreal_chaos` | Unreal Engine Chaos; UnrealBuildTool Program target, Win64 Shipping; `cl.exe`; Windows SDK `rc.exe`; explicit-only public package. |

`visual-run` uses `release/windows-x64/shared_visual_renderer/`, a checked-in shared renderer executable built by the repo release route.
