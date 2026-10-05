# Run, results and replay

Use the [quick start](../README.md#quick-start) to launch a benchmark. This guide explains saved settings, queue behavior, replay controls and physical verification

## Configuration and queue

Review the draft before starting work. Manual engine, thread and repeat selections stay in effect until you apply Recommended

| Control                                            | Effect                                                                                                                                            |
| :------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------ |
| Reset physics                                      | Restores authored physics, solver and initial-camera settings. Retains engines, threads, repeats, output and verification mode                    |
| Recommended                                        | Applies the current case's complete recommended matrix and authored physics, selects Release output and retains verification mode. Starts no work |
| Add recommended Release runs                       | Queues Box Container, Box Contact Islands and Large Pyramid with five repeats and default settings                                                |
| Run queue                                          | Reviews, reorders or edits pending rows                                                                                                           |
| Update queued Test run / Update queued Release run | Saves the edited pending row                                                                                                                      |
| Cancel edit                                        | Restores the previous draft                                                                                                                       |

The GUI remembers Test/Release output. Each queued row freezes its destination and verification mode, so later draft changes leave it unchanged. Pending Test/Release counts appear beside Start. Draft and pending-row edits remain available during execution. Dispatch waits if it reaches a row being edited, until you save or cancel the edit

Save preset opens a name popup. Presets live in `presets/` beside `PhysicsArena.exe`, independently of the current folder. Saved presets applies a preset while retaining the current Test/Release output. An existing name offers Replace preset or Cancel. You can copy existing preset JSON files into that folder without conversion

| Event                     | Queue behavior and retained evidence                                                                                                                                                                     |
| :------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Failed repeat             | Finishes the repeat and retains collected measurements, assessment and requested replay. Skips further repeats and worker counts for that engine in the same queued run. Other engines and rows continue |
| Cancel run and stop queue | Ends the active row and preserves pending rows. Start remaining (N) runs them without repeating finished work                                                                                            |
| Harness failure           | Ends the active row and stops dispatch. Diagnostics remain in the status area                                                                                                                            |
| Window closed             | Stops dispatch, cancels active work and discards the session queue                                                                                                                                       |

Results distinguishes measured failures, execution failures and skipped work. Its charts and aggregates use only actual samples

## Saved results and replay

Save replay captures every selected engine and repeat for Replay threads. The benchmark still measures all selected thread counts. Run shows a conservative storage estimate that includes temporary files

Results provides Benchmark, Thread scaling, Step/Batch/Ray timing, Repeats, Case data and Recordings. Thread scaling needs multiple thread counts. Case data shows the workload, saved configuration, observations and provenance, including failed observations beside valid timings. Ragdoll is quality-only and untimed

### Playback

In Results, open Recordings, choose an engine, thread count and repeat, then select Play selected recording. The Replay workspace also browses saved recordings. Transform playback opens paused at frame 0 and needs no engine package. Heavy Ray Tracing recordings show saved images instead of transform playback

| Input       | Action in 3D replay   |
| :---------- | :-------------------- |
| Space       | Play or pause         |
| Timeline    | Seek to a saved frame |
| Right-drag  | Orbit                 |
| Middle-drag | Pan                   |
| Mouse wheel | Zoom                  |
| R           | Reset the camera      |

To compare an open 3D replay, choose Compare against..., select a saved run and recording, then confirm Compare. Each recording row shows its engine, threads, repeat, status and saved size

| Comparison rule     | Behavior                                                                                   |
| :------------------ | :----------------------------------------------------------------------------------------- |
| Required match      | Same frozen physical case, timestep and warmup                                             |
| Allowed differences | Engine, build, physics, solver settings and recording length                               |
| Playback            | Same Step or Batch number through the shorter recording, with linked camera and appearance |
| Recording details   | Each recording's build, frozen physics, solver settings and recorded outcome               |
| Stop comparison     | Restores the primary recording's full range, paused at the last shared position            |

### Timing and saved state

Timing covers complete native work, including internal substeps and worker completion. Setup, warmup, observation reduction and file output are excluded. Recording consumes CPU, memory and disk bandwidth and can affect cache or scheduling behavior. Compare physical quality and processed workload alongside timings

Schema-8 manifests store effective settings and recording policy. Transform recordings use lossless, seekable Zstd blocks. Frame 0 follows the case's warmup/reset behavior, then each frame represents a completed measured work unit. Ragdoll saves poses and quality observations without durations

| Case                  | Authored warmup                           | State at frame 0                                          |
| :-------------------- | ----------------------------------------: | :-------------------------------------------------------- |
| Container and Ragdoll | 30 steps                                  | Warmup world discarded, authored starting state recreated |
| Contact Islands       | 30 steps (0.5 simulated seconds at 60 Hz) | Warmed world retained                                     |
| Pyramid Wall          | 1 step                                    | Warmed world retained                                     |
| Large Pyramid         | 0 steps                                   | Authored starting state                                   |

These warmup counts do not certify rest or steady state

### Compatibility and cleanup

Historical schema-5/6 results and schema-7 version-1 recordings remain readable. Unfinished schema-7/8 runs retain their recovery settings. A complete raw recording can finish compression without physics. Missing captures cannot be recreated from measurements

Replay shows recording sizes and confirms deletion of one recording, a run's recordings or the completed local recording library. Unfinished leftovers have a separate cleanup action. Deletion preserves measurements and reports. Failures identify retained paths and bytes

## Physical verification

Verify physics defaults to On. Preferences and named presets save the choice, and each queued run freezes its own mode. Reset physics and recommendations preserve the draft choice. The command line accepts `--verify on|off`, and omitted historical settings and results retain On

With On, supported stack cases assess sampled states during execution and save compact assessments in `stability.csv`, including physical failures and interrupted runs with observed samples. Verification does not save a trajectory file. Select Save replay to retain the existing compressed playback recording

Sampling, transfer, waiting and assessment stay outside the complete native-step timer. Saved capture and analysis durations report verification overhead. Instrumentation can still affect caches, scheduling and total wall time

With Off, the simulation still runs and saves native timings, workload counts and optional replay. It skips stack sampling and assessment, Pyramid Wall physical snapshots and Ragdoll quality accumulation. Completed repeats show "Verification Off: physical quality not checked" and remain unverified. Their timings cannot establish an equivalent-quality speedup. Execution errors and malformed or missing output still fail. Ragdoll remains untimed

### Current criteria

| Case            | Assessment window                                                                                                    | Shape limit                                                                                                                                      |
| :-------------- | :------------------------------------------------------------------------------------------------------------------- | :----------------------------------------------------------------------------------------------------------------------------------------------- |
| Contact Islands | Finite-state and fall safety over all sampled states. Slot shape in the final measured second                        | `0.10 * L` metres for whole-box slot-envelope excess and assigned-neighbour projected overlap, where `L` is the minimum full box edge            |
| Large Pyramid   | Unforced trajectory before the first possible projectile-impact interval. Finite-state safety continues after impact | Fixed 0.10 m (10 cm), independent of box size, for whole-box authored-slot excess and assigned-neighbour projected compression, counted per body |
| Pyramid Wall    | Full unforced run                                                                                                    | Same fixed 0.10 m occupied-box shape limit as Large Pyramid                                                                                      |

Contact Islands can move while safety and shape hold. Projected overlap is a structural proxy, not measured contact penetration. At 60 Hz, verification On requires at least 61 measured steps. Off permits shorter runs without a physical-quality assessment

For both pyramids, an earlier breach remains a failure after a later impact or reconstruction

### Historical outcomes and evidence limits

A complete compact assessment remains sufficient for the criterion under which it was collected. It cannot certify a stronger future criterion when the original samples needed by that criterion are unavailable. Earlier committed repeats survive interruption. An unfinished current repeat can remain unavailable after the app terminates

Historical Contact Islands support-plane passes and saved 2 cm/10 cm settling-and-shape assessments retain their original limits and outcomes. Current safety and shape remain Unassessed until the complete trace is reassessed. Known invalid-state or support-plane failures remain failures

Historical pyramid fall-check passes retain their outcome, with current shape Unassessed until complete trace-backed reassessment. Missing or truncated traces cannot certify the stronger Pass

Recordings of measured steps can reveal a terminal violation. They cannot establish full warmup safety or certify a stronger Pass. Saved terminal values must refer to the same frozen geometry, sampling cadence and final recorded samples

The app refuses to resume runs saved under an older verification policy before changing saved files or starting simulation. See [sources and measurement boundaries](../SOURCES.md) for engine-specific details
