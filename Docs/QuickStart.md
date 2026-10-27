# Tidekeeper quick start

## Play

Extract Tidekeeper-Windows.zip and launch Windows/NiagaraHerd.exe. Keep the entire
Windows folder together. In the original project folder, the launcher is under
Packaged/Windows. The default mode uses 500 CPU mesh agents; press Tab to switch
to 100,000 GPU sprites.

Move the yellow beacon with WASD or hold the left mouse button. Hold the right
mouse button to attract nearby agents. Press Space for a repel pulse. Rescue at
least 70% of the school into the green sanctuary within two minutes.

R restarts, P pauses, and +/- changes the particle count. F7 enables the automated
test route; F9 starts the full performance sweep, which takes about three minutes.

## Edit in Unreal Engine

Open NiagaraHerd.uproject in UE 5.7.4, allow the C++ modules to build, then press
Play. The editor map is intentionally empty: C++ creates the arena and HUD at
runtime. Checked-in Niagara assets are ready to use.

- CPU Niagara system: /Game/Herd/NS_HerdCPU
- GPU Niagara system: /Game/Herd/NS_HerdGPU
- Rules, input and HUD: Source/NiagaraHerd/HerdDirector.cpp
- CPU behavior: Source/NiagaraHerd/HerdSimulation.cpp
- GPU behavior: Shaders/Herd.usf
- Niagara data interface: Source/NiagaraHerd/HerdDataInterface.cpp

Do not regenerate the Niagara assets before editing them. BuildAssets.ps1
overwrites the generated assets; normal use does not require running it.

## Submission files

The release contains an editable source ZIP, a Windows game ZIP, a 75-second demo,
a proposal, a performance report, profiling evidence and a checksum manifest.
README.md documents controls and reproduction steps. Docs/Validation.md
distinguishes completed checks from outstanding student checks.

Tidekeeper-Repository.bundle contains the English submission snapshot and its
v1.0.2 tag. Clone it with:

    git clone -b main Tidekeeper-Repository.bundle NiagaraHerd

Complete a physical playtest, personally view the two references, and repeat the
performance sweep on the required lab hardware before final submission. Upload
the release to the course destination and verify the instructor's access.
