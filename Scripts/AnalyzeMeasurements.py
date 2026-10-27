"""Join real frame samples, component counters and Insights GPU/RT timings."""
import argparse
import csv
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--csv", required=True)
p.add_argument("--exportdir", required=True)
p.add_argument("--output", required=True)
args = p.parse_args()
rows = list(csv.DictReader(Path(args.csv).open(encoding="utf-8-sig")))
exports = Path(args.exportdir)

def timers(prefix, region):
    file = exports / f"{prefix}-{region}.csv"
    if not file.exists():
        raise FileNotFoundError(file)
    return list(csv.DictReader(file.open(encoding="utf-8-sig")))

for r in rows:
    if int(r["count"]) != int(r["rendered_count"]) or r["spawn_locked"] != "true":
        raise ValueError(f"Requested/actual particle mismatch: {r}")
    if float(r["niagara_gt_ms"]) < 0 or float(r["niagara_rt_ms"]) < 0:
        raise ValueError("Engine component performance counters are missing")
    region = f'{r["mode"]}_{r["count"]}_hash{r["spatial_hash"]}'
    gpu = timers("GPU", region)
    rt = timers("RT", region)
    frames = int(r["samples"])
    compute = [x for x in gpu if x["Name"] in ("NiagaraGpuComputeDispatch", "NiagaraGPUSimulation")]
    # One outer GPU dispatch scope covers the custom DI passes too. Do not add
    # the nested HerdBoids timer again and double-count it.
    r["gpu_compute_ms"] = f'{max((float(x["Incl"]) for x in compute), default=0) * 1000 / frames:.4f}'
    frame = [x for x in gpu if x["Name"] == "SceneRender"]
    r["gpu_scene_ms"] = f'{max((float(x["Incl"]) for x in frame), default=0) * 1000 / frames:.4f}'
    setup = sum(float(x["Incl"]) for x in rt if x["Name"] == "Herd_GPU_PreStage") * 1000 / frames
    r["rt_di_setup_ms"] = f"{setup:.4f}"
    # Engine per-component RT counters cover render updates/mesh gathering;
    # separately include custom render-thread DI dispatch setup conservatively.
    combined = float(r["niagara_gt_ms"]) + float(r["niagara_rt_ms"]) + setup
    r["niagara_gt_rt_ms"] = f"{combined:.4f}"
    r["frame_pass_p95"] = str(float(r["p95_frame_ms"]) <= 16.6667).lower()
    r["niagara_cpu_pass"] = str(combined <= 4).lower()
    r["combined_pass"] = str(r["frame_pass_p95"] == "true" and combined <= 4).lower()

out = Path(args.output)
out.parent.mkdir(parents=True, exist_ok=True)
with out.open("w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0]))
    w.writeheader()
    w.writerows(rows)
print(f"Validated and consolidated {len(rows)} configurations: {out}")
for r in rows:
    print(r["mode"],r["count"],r["spatial_hash"],"frame",r["mean_frame_ms"],"p95",r["p95_frame_ms"],"Niagara GT+RT",r["niagara_gt_rt_ms"],"GPU compute",r["gpu_compute_ms"],"pass",r["combined_pass"])
