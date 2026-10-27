# Tidekeeper - Niagara at Scale

A one-level flock-herding game for UE 5.7.4, assignment Track B. Move a beacon,
attract or repel a school, and rescue 70% of the agents into the green sanctuary
before the two-minute timer expires. The same game runs as CPU Niagara mesh
agents or GPU Niagara sprite agents. Rescues are calculated from simulated agent
positions and returned to C++ gameplay.

## Play

Open `NiagaraHerd.uproject` with UE 5.7.4 and press Play. The arena, orthographic
camera and HUD are constructed by C++ at runtime; the editor map starts empty.
Alternatively, run `Scripts/RunGame.ps1` for a separate 1920x1080 game window.
The packaged game, when present, starts at `Packaged/Windows/NiagaraHerd.exe`.
Keep the complete `Windows` folder together; the root EXE is a launcher.

| Input | Action |
| --- | --- |
| WASD | Move the beacon |
| Hold left mouse | Move toward the cursor |
| Hold right mouse | Attract agents within range |
| Space | Repel pulse, 2.5-second cooldown |
| Tab | Switch CPU/GPU and restart at the mode's target count |
| R / P | Restart / toggle simulation pause |
| + / - | Change particle-count preset and restart |
| F6 | Toggle CPU spatial hashing / all-pairs neighbor queries |
| F7 | Toggle the automated test route |
| F9 | Run the full benchmark sweep |
| Tilde | Open the Unreal console |

Blue agents graze, orange CPU agents flee, purple CPU agents follow, and gold CPU
agents are leaders while grazing. Rescued agents turn green and remain in the
sanctuary. GPU agents deliberately have fewer behaviors and no CPU state machine.

## Build

Requirements: UE 5.7.4, Visual Studio with the Unreal C++ toolchain and a Windows
SDK, Windows DX12/SM6-compatible graphics. No additional packages are required.

```powershell
.\Scripts\BuildEditor.ps1
.\Scripts\InitializeMap.ps1
.\Scripts\BuildAssets.ps1
.\Scripts\PackageGame.ps1
```

The map initializer preserves an existing map. The Niagara asset builder replaces
the generated `/Game/Herd` systems, materials and dynamic-input scripts; save
manual Niagara edits elsewhere before regenerating. Checked-in assets are ready
to open, so asset regeneration is optional.

`PackageGame.ps1` creates a Windows Development build, retaining console controls
and stats for reproducible grading. Python and editor modules are excluded from
the game target. Project files, build intermediates, project DDC and packages are
under this project on D:. Engine/compiler user logs and temporary files may use
their normal Windows locations.

## Console variables

| Variable | Default | Meaning |
| --- | --- | --- |
| `herd.Mode` | 0 | 0 CPU meshes, 1 GPU sprites; switching restarts |
| `herd.Count` | 500 | Runtime count, CPU 1-5,000 / GPU 1-4,000,000 |
| `herd.SpatialHash` | 1 | 0 all pairs, 1 exact CPU spatial hash |
| `herd.Auto` | 0 | Deterministic attraction route for test reproduction |
| `herd.Benchmark` | 0 | Set to 1 to start a sweep |
| `herd.RoundSeconds` | 120 | Duration applied to the next round |
| `herd.TestGoalAtSpawn` | 0 | Test only: put the rescue area inside the spawn region |
| `herd.TestWinThreshold` | 70 | Test only: required rescue percentage |

For the normal GPU configuration, use `herd.Mode 1` and `herd.Count 100000`.
The count slider requirement is met through console variables and +/- presets.
The director disables further bursts after the requested count has spawned;
runtime logs separately record requested and actual particle counts.

## Performance reproduction

Budget: 1920x1080, 60 fps / 16.6 ms per frame, Niagara GT+RT at most 4 ms.
This project declares a p95 frame-time criterion for sustained 60 fps. CPU target:
500 complex mesh agents. GPU target: 100,000 simple sprite agents. Higher presets
are stress tests, not claims that every count meets the budget.

Run `Scripts/RunBenchmark.ps1`. It uses the packaged Development game when present
(or the editor's standalone game otherwise; `-UseEditor` explicitly selects it).
It tests five CPU counts with hashing disabled,
the same five with hashing enabled, and ten GPU counts up to four million.
Every stage lasts nine seconds, with a four-second warmup and five-second sample.
VSync and frame limiting are disabled. The test route, resolution, seed, camera,
map and rendering settings are held constant. Simulation timesteps are capped at
1/30 s; overloaded configurations consequently advance more slowly than wall time.

Outputs:

- `Packaged/Windows/NiagaraHerd/Saved/Benchmarks/HerdSweep-*.csv` (packaged) or
  `Saved/Benchmarks/HerdSweep-*.csv` (editor): mean/p95 wall frame time, custom CPU solve
  time, actual count, and the engine's component Niagara GT/worker/RT/GPU counters.
- `Saved/Profiling/HerdSweep-*.utrace`: Unreal Insights CPU/GPU frames, named events,
  bookmarks and one named sampling region per configuration.
- `Saved/Logs/Benchmark-*.log`: reproducibility and count/callback diagnostics.

`stat Niagara` and `stat GPU` remain visible during the benchmark. Open a trace in
Unreal Insights and inspect `CPU_500_hash1`, `GPU_100000_hash1`, etc. Custom work has
CPU scopes `Herd_CPU_Simulate` and `Herd_GPU_PreStage`, and GPU stat `HerdBoids`.
Custom GPU compute is scheduled inside Niagara's GPU dispatch; built-in component
GPU counters alone do not describe all custom DI work. Keep its Insights GPU
timers alongside the built-in counters. See `Docs/Architecture.md`.

For the Niagara Debugger in the editor, use Tools > Debug > Niagara Debugger,
select the PIE world and this component, then inspect particle counts and timing.
Do not use debugger-heavy frames as the primary benchmark sample.
`Scripts/RunDebuggerCapture.ps1` opens a dedicated editor process, captures GPU
overview and CPU performance HUDs with the editor UI, then closes that process.

Measurements made on the development laptop do not establish lab-machine results.
Repeat the same script on the lab machine and record its hardware and settings.

## Validation and capture

Automation tests cover input-dependent trajectories, absorbing rescue state, and
hash/all-pairs trajectory agreement within float summation tolerance. Rendered
runtime checks additionally verify actual counts, asynchronous GPU score updates,
mode switching, restarts and controlled win/lose paths. These checks do not replace
the student's physical keyboard/mouse playtest or subjective feel review.

The optional `-HerdCapture=DemoName` game argument records 2,250 PNGs at a fixed
simulation clock of 30 fps under `Saved/Capture/DemoName`. It uses the automated
route, changes from CPU to GPU after 32 seconds, and displays stats after 64 seconds.
The capture is explicitly labeled as fixed-rate and is separate from benchmarking.

## Submission files

The editable project comprises `Source`, `Content`, `Config`, `Shaders`, `Scripts`,
`Docs`, the `.uproject`, `.gitignore` and this README. Generated build/cache/capture
directories are ignored by Git. The final report is limited to two pages.

`Submission/` contains the local release assets: editable source ZIP, Windows
game ZIP, 75-second MP4, proposal, performance report, profiling-evidence ZIP and
a SHA-256 manifest. A Git bundle preserves the submission snapshot and tag offline:
`git clone -b main Tidekeeper-Repository.bundle NiagaraHerd`.
The public submission snapshot is tagged `v1.0.2`.
Download the demo, Windows build and PDFs from the
[release page](https://github.com/33charl1e/Tidekeeper-Niagara-at-Scale/releases/tag/v1.0.2).

Verified packaged target results on the development laptop: CPU 500 p95 7.3768
ms and Niagara GT+RT 3.0917 ms; GPU 100,000 p95 5.2084 ms and Niagara GT+RT
0.0563 ms. The GPU uses a bounded neighbor approximation with severe overflow
in dense cells. `Docs/Validation.md` records evidence and outstanding checks;
`Docs/QuickStart.md` provides the quick start.
