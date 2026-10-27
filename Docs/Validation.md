# Validation record

Completed on the development laptop, October 26-27, 2026. No lab-machine result
or physical keyboard/mouse acceptance is asserted.

## Build and simulation

- UE 5.7.4 Editor Development build passed, including the editor asset commandlet.
- Windows Development package built and launched successfully with DX12/SM6.
- Cooked CPU and GPU systems report valid/ready; GPU shader is present.
- Three automation tests passed: `CaptureIsAbsorbing`, `HashMatchesAllPairs`,
  `InputChangesTrajectory`. An unchanged test-result excerpt is included with
  the evidence; unrelated startup/device messages are omitted.
- Packaged GPU mode holds the actual Niagara count at 100,000 after repeated
  emitter loops and asynchronously returns score/overflow summaries.
- Controlled packaged win/switch test passed: GPU 100,000 wins, switch at eight
  seconds resets to CPU 500, CPU wins. Goal-at-spawn and 5% threshold are test-only
  parameters, not evidence that normal manual play is easy.
- Controlled one-second loss test with CPU 501 reached `HERD_LOSE` and exited 0.
- The normal automated capture route also won at the regular 70% threshold in
  both CPU 500 and GPU 100,000 configurations.

## Measurements

`Results/Raw-Packaged-Sweep.csv` records 20 completed configurations: five CPU
counts before and after hashing and ten GPU counts. Each count matches the
engine's actual particle count. `Measurements.csv` adds Insights-derived GPU and
custom render-thread setup timings and validates count/counter availability.

Target results at 1920x1080:

| Mode | Mean frame | p95 frame | Niagara GT+RT | GPU compute |
| --- | ---: | ---: | ---: | ---: |
| CPU 500, spatial hash | 5.7459 ms | 7.3768 ms | 3.0917 ms | n/a |
| GPU 100,000 | 4.4214 ms | 5.2084 ms | 0.0563 ms | 0.1352 ms |

First sampled p95 frame-budget failures: CPU 1,000 and GPU 4,000,000. CPU GT+RT
also exceeds 4 ms at 1,000. Worker time is retained separately in CSV.

The GPU uses bounded neighbor storage/sampling, with substantial cell overflow
in dense clusters. These timings do not demonstrate exact CPU-equivalent
neighbor coverage. See the report and Architecture.md.

Insights 5.7's aggregate-stat exporter includes GPU queues even when CPU thread
filters are requested. Thus RT/GT exports contain GPU rows as well as filtered
CPU rows. Analysis selects the uniquely named CPU `Herd_GPU_PreStage` scope for
RT setup and the outer GPU Niagara simulation scope for compute. Nested timers
are not added twice. GPU timestamp-order warnings emitted during trace analysis
are retained in the export log; wall frame times and component CPU counters are
independent of those GPU timestamps.

## Media and documents

- Demo: 2,250 captured frames, 1920x1080, 30 fps, 75 seconds, H.264 MP4, no audio.
- Actual gameplay rendering, automated route, fixed simulation clock. CPU/GPU
  mode change and stat overlays are visible. This is separate from profiling.
- Decoded CPU/GPU/stat frames visually inspected after encoding.
- Release v1.0.1 replaces the vertically inverted RGB32 encoding with explicit
  top-down NV12 encoding. Decoded CPU, GPU and stats samples were compared with
  the original capture PNGs at the same timestamps. Identity orientation had
  the lowest pixel error in all three samples. Container checks confirm 2,250
  frames, 75 seconds, 1920x1080, 30 fps and an identity display transform.
  Results are recorded in Results/Evidence/VideoVerification.json.
- A separate Chromium HTML video player also rendered the corrected file
  upright. Results/Evidence/Video-Player-Check.png records that player check.
- Submission documents, filenames and text logs were checked for Chinese text.
  The distributed Git bundle contains only the English submission snapshot;
  superseded local history and backups are excluded.
- One-page proposal and two-page report rendered to PNG and visually inspected.
- Niagara Debugger HUD captured in PIE with the editor UI included. Normal
  viewport-only screenshots omit this separate debug layer. The dedicated
  `RunDebuggerCapture.ps1` process reproduces the captures; it closes after them.

## Remaining student checks

- Perform the physical controls/feel checklist in PlaytestChecklist.md.
- Personally watch/play the two references and add your own observations.
- Run the supplied benchmark on the specified lab hardware and retain the new
  CSV/trace; the laptop result does not establish that requirement.
- The public repository and v1.0.2 release provide the source and submission
  downloads. Submit their links through the course portal and verify access.
