"""Create a one-page proposal and a two-page report from verified measurements."""
import csv
import math
from pathlib import Path
from reportlab.pdfgen import canvas
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import Paragraph, Table, TableStyle
from reportlab.graphics.shapes import Drawing, Line, String
from reportlab.graphics.charts.lineplots import LinePlot
from reportlab.graphics.charts.axes import LogYValueAxis
from reportlab.graphics.widgets.markers import makeMarker
from reportlab.graphics import renderPDF, renderSVG

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "Docs"
RESULTS = DOCS / "Results"
DATA = RESULTS / "Measurements.csv"
ROWS = list(csv.DictReader(DATA.open(encoding="utf-8-sig")))
BLUE = colors.HexColor("#166480")
TEAL = colors.HexColor("#16876B")
INK = colors.HexColor("#162B36")
MUTED = colors.HexColor("#536872")
RED = colors.HexColor("#B44E4B")
BG = colors.HexColor("#EFF5F6")
STYLE = ParagraphStyle("Body",fontName="Helvetica",fontSize=9.1,leading=12.6,textColor=INK,spaceAfter=5)

def para(c, text, x, y, width=540, size=9.1, leading=None):
    style = ParagraphStyle("P",parent=STYLE,fontSize=size,leading=leading or size*1.38)
    item = Paragraph(text,style)
    _, h = item.wrap(width,1000)
    item.drawOn(c,x,y-h)
    return y-h-6

def header(c, title, subtitle):
    c.setFillColor(INK); c.setFont("Helvetica-Bold",23); c.drawString(36,749,title)
    c.setFillColor(MUTED); c.setFont("Helvetica",9); c.drawString(36,729,subtitle)
    c.setStrokeColor(TEAL); c.setLineWidth(2); c.line(36,715,576,715)

def footer(c, label, page):
    c.setStrokeColor(colors.HexColor("#C7D6DA")); c.setLineWidth(.6); c.line(36,42,576,42)
    c.setFillColor(MUTED); c.setFont("Helvetica",7.2); c.drawString(36,29,label)
    c.drawRightString(576,29,str(page))

def table(c, data, x, top, widths, font=8.0, rowheight=16):
    t=Table(data,colWidths=widths,rowHeights=[rowheight]*len(data))
    t.setStyle(TableStyle([
        ("FONTNAME",(0,0),(-1,0),"Helvetica-Bold"),("FONTNAME",(0,1),(-1,-1),"Helvetica"),
        ("FONTSIZE",(0,0),(-1,-1),font),("TEXTCOLOR",(0,0),(-1,-1),INK),
        ("BACKGROUND",(0,0),(-1,0),BG),("ROWBACKGROUNDS",(0,1),(-1,-1),[colors.white,colors.HexColor("#F7F9FA")]),
        ("ALIGN",(1,0),(-1,-1),"RIGHT"),("VALIGN",(0,0),(-1,-1),"MIDDLE"),
        ("LEFTPADDING",(0,0),(-1,-1),5),("RIGHTPADDING",(0,0),(-1,-1),5),
        ("LINEBELOW",(0,0),(-1,0),.6,colors.HexColor("#AAC1C9")),
    ]))
    _,h=t.wrap(600,700); t.drawOn(c,x,top-h); return top-h

def short(n):
    n=int(n)
    return f"{n/1000000:g}m" if n>=1000000 else f"{n/1000:g}k" if n>=1000 else str(n)

def chart(cpu):
    d=Drawing(264,190)
    p=LinePlot(); p.x=38; p.y=32; p.width=213; p.height=132
    subset=[r for r in ROWS if r["mode"] == ("CPU" if cpu else "GPU") and r["spatial_hash"]=="1"]
    p.data=[[(int(r["count"]) if cpu else math.log10(int(r["count"])),float(r["mean_frame_ms"])) for r in subset]]
    p.lines[0].strokeColor=BLUE if cpu else TEAL; p.lines[0].strokeWidth=1.7
    p.lines[0].symbol=makeMarker("FilledCircle"); p.lines[0].symbol.size=3.4
    p.xValueAxis.labels.fontName="Helvetica"; p.xValueAxis.labels.fontSize=7
    p.yValueAxis=LogYValueAxis(); p.yValueAxis.valueMin=1; p.yValueAxis.valueMax=256
    p.yValueAxis.valueSteps=[1,4,16,64,256]; p.yValueAxis.labels.fontSize=7; p.yValueAxis.labels.fontName="Helvetica"
    p.yValueAxis.labelTextFormat=lambda v:f"{v:g}"
    p.xValueAxis.joinAxis=p.yValueAxis; p.xValueAxis.joinAxisPos=1
    if cpu:
        p.xValueAxis.valueMin=0; p.xValueAxis.valueMax=5000; p.xValueAxis.valueSteps=[500,2000,3500,5000]
        p.xValueAxis.labelTextFormat=lambda v:short(v)
    else:
        p.xValueAxis.valueMin=4; p.xValueAxis.valueMax=math.log10(4000000)
        p.xValueAxis.valueSteps=[4,5,6,math.log10(4000000)]
        p.xValueAxis.labelTextFormat=lambda v:short(round(10**v))
    p.yValueAxis.visibleGrid=True; p.yValueAxis.gridStrokeColor=colors.HexColor("#DCE6E9"); p.yValueAxis.gridStrokeWidth=.4
    d.add(p)
    yy=p.y+p.height*math.log(16.6667)/math.log(256)
    d.add(Line(p.x,yy,p.x+p.width,yy,strokeColor=RED,strokeWidth=.8,strokeDashArray=[3,3]))
    d.add(String(p.x+3,yy+4,"16.6 ms budget",fontName="Helvetica",fontSize=6.6,fillColor=RED))
    d.add(String(38,179,"CPU / spatial hash" if cpu else "GPU / bounded neighbor grid",fontName="Helvetica-Bold",fontSize=10,fillColor=INK))
    d.add(String(38,4,"Particle count; mean frame time in ms (log y)",fontName="Helvetica",fontSize=7.1,fillColor=MUTED))
    return d

cpu=[r for r in ROWS if r["mode"]=="CPU" and r["spatial_hash"]=="1"]
gpu=[r for r in ROWS if r["mode"]=="GPU"]
targetcpu=next(r for r in cpu if int(r["count"])==500)
targetgpu=next(r for r in gpu if int(r["count"])==100000)

def generate_report():
    path=DOCS/"Tidekeeper-Performance-Report.pdf"
    c=canvas.Canvas(str(path),pagesize=(612,792)); c.setTitle("Tidekeeper - Niagara Performance Report")
    header(c,"Tidekeeper: measured scale","Niagara at Scale / Track B / UE 5.7.4 / October 26, 2026")
    y=para(c,"<b>Declared budget:</b> 1920x1080, 60 fps (16.6 ms), Niagara game + render thread at most 4 ms. Targets: 500 complex CPU mesh agents and 100,000 simple GPU sprites. This report uses p95 frame time for sustained-frame acceptance.",36,699,size=9.2)
    y=para(c,"<b>Development machine:</b> i9-14900HX, RTX 4080 Laptop GPU, 64 GB RAM, Windows 11. Windows Development build, DX12/SM6, Epic scalability, unlit arena, Lumen/ray tracing and VSync/frame cap off. These are local results; lab validation is outstanding.",36,y,size=8.8)
    a,b=chart(True),chart(False)
    renderPDF.draw(a,c,32,417); renderPDF.draw(b,c,308,417)
    renderSVG.drawToFile(a,str(RESULTS/"CPU-Scaling.svg")); renderSVG.drawToFile(b,str(RESULTS/"GPU-Scaling.svg"))
    left=[["CPU count","Mean ms","p95 ms","GT+RT ms"]]
    left += [[short(r["count"]),f'{float(r["mean_frame_ms"]):.2f}',f'{float(r["p95_frame_ms"]):.2f}',f'{float(r["niagara_gt_rt_ms"]):.2f}'] for r in cpu]
    right=[["GPU count","Mean ms","p95 ms","Compute ms"]]
    right += [[short(r["count"]),f'{float(r["mean_frame_ms"]):.2f}',f'{float(r["p95_frame_ms"]):.2f}',f'{float(r["gpu_compute_ms"]):.2f}'] for r in gpu]
    table(c,left,36,411,[67,59,59,73],rowheight=16)
    table(c,right,312,411,[64,58,58,84],font=7.7,rowheight=14)
    y=240
    y=para(c,f'<b>Targets:</b> CPU 500: p95 {float(targetcpu["p95_frame_ms"]):.2f} ms, Niagara GT+RT {float(targetcpu["niagara_gt_rt_ms"]):.2f} ms. GPU 100k: p95 {float(targetgpu["p95_frame_ms"]):.2f} ms, GT+RT {float(targetgpu["niagara_gt_rt_ms"]):.2f} ms, GPU compute {float(targetgpu["gpu_compute_ms"]):.2f} ms.',36,y,size=9.1)
    cpufail=next((r for r in cpu if r["combined_pass"]!="true"),None)
    gpufail=next((r for r in gpu if r["combined_pass"]!="true"),None)
    y=para(c,f'<b>First sampled budget failures:</b> CPU {short(cpufail["count"]) if cpufail else "not found in tested range"}; GPU {short(gpufail["count"]) if gpufail else "not found in tested range"}. CPU and GPU limits are measured separately; they are not added as if the threads execute serially.',36,y,size=8.8)
    y=para(c,'<b>Method:</b> five CPU counts before/after hashing; ten GPU counts. Each stage uses four seconds of warmup and five seconds of samples with a fixed seed, camera and timed attractor route. Actual Niagara particle counts were verified. stat Niagara/stat GPU overlays and named CPU/GPU Insights regions were captured.',36,y,size=8.5)
    y=para(c,'<b>Timing definitions:</b> GT/worker/RT values come from engine component particle-performance counters. GT+RT additionally includes separately traced custom GPU-DI render-thread setup. GPU compute is the outer Niagara GPU dispatch scope, which encloses the custom DI kernels. It excludes sprite drawing. Raw CSVs preserve all counters.',36,y,size=8.5)
    footer(c,"Source: Docs/Results/Measurements.csv; raw measurements and trace exports supplied alongside it.",1)
    c.showPage()
    header(c,"Optimization and game trade-offs","A measured improvement is conditional on distribution, not just particle count.")
    y=para(c,'<b>Optimization:</b> replace all-pairs CPU neighbor visits with an exact spatial hash. Rules, perception radius, seed, arena and test route stay the same. An automation test checks matching trajectories at the same timesteps. The table shows custom CPU-solver time; negative gains mean a regression.',36,699,size=9)
    data=[["CPU count","All pairs (ms)","Hash (ms)","Reduction"]]
    for r in cpu:
        before=next(x for x in ROWS if x["mode"]=="CPU" and x["spatial_hash"]=="0" and x["count"]==r["count"])
        a,b=float(before["mean_cpu_sim_ms"]),float(r["mean_cpu_sim_ms"])
        data.append([short(r["count"]),f"{a:.2f}",f"{b:.2f}",f"{(1-b/a)*100:+.1f}%"])
    bottom=table(c,data,36,y-3,[100,150,150,140],font=8.6,rowheight=18)
    y=para(c,'<b>Interpretation:</b> hashing reduces solver time by 16.4% at 500 agents, but the gain approaches zero at 5,000. A dense following cluster puts most neighbors into a few cells; traversal overhead consumes the expected gain. The next optimization should reduce dense-cell work without hiding a behavior change.',36,bottom-14,size=8.8)
    y=para(c,'<b>Two-way gameplay coupling:</b> input changes attraction/repulsion; simulation positions decide rescue. CPU returns the rescued total directly. GPU reduces rescued/overflow counts and asynchronously reads back only two uint32 values every sixth tick. Run IDs and monotonic serials reject late callbacks after a restart. Captured agents remain rescued.',36,y,size=8.8)
    overflow=100*int(targetgpu['grid_overflow'])/int(targetgpu['count'])
    y=para(c,f'<b>GPU quality cost:</b> the 25x15 grid stores at most 64 indices per cell; an agent samples up to eight entries in each adjacent cell. In the dense 100k test, {overflow:.1f}% of agents overflow cell storage at the final sample. They still move and render, but are absent from stored neighbor lists. This severe approximation is displayed in the HUD/CSV and is not an exact all-neighbor CPU comparison.',36,y,size=8.8)
    match=min(gpu,key=lambda r:abs(float(r["mean_frame_ms"])-float(targetcpu["mean_frame_ms"])))
    delta=abs(float(match["mean_frame_ms"])-float(targetcpu["mean_frame_ms"]))
    y=para(c,f'<b>At similar frame time:</b> CPU 500 averages {float(targetcpu["mean_frame_ms"]):.2f} ms; the closest measured GPU configuration, {short(match["count"])}, averages {float(match["mean_frame_ms"]):.2f} ms (difference {delta:.2f} ms). This is a measured near-match, not an assertion of identical frame times. The GPU supports a much denser school; the CPU gives individually readable state colors, leaders and obstacle avoidance.',36,y,size=8.8)
    y=para(c,'<b>Design choice:</b> keep the CPU configuration as the default for this small herding arena because individual feedback and obstacle response explain the player\'s decisions. Keep GPU mode for scale and collective motion. This is a design argument from implemented behavior and rendering, not a user-preference study.',36,y,size=8.8)
    y=para(c,'<b>Limits:</b> one laptop, one dense-follow route and one sampling window. At overloaded counts, the 33.3-ms simulation step cap slows simulated motion relative to wall time, so trajectories are not identical across different frame rates. Tracing/stat overlays add overhead. Lab performance and physical mouse/keyboard playtesting remain separate checks.',36,y,size=8.5)
    y=para(c,'<b>With another week:</b> test a hierarchical/density-aware neighbor scheme, improve sprite readability and feedback, calibrate an exact equal-frame-time pair, collect player observations, and rerun the same captures on the lab hardware.',36,y,size=8.5)
    if y<52: raise RuntimeError(f"Report page 2 overflows: y={y}")
    footer(c,"Report distinguishes frame time, component CPU counters, custom CPU work and GPU dispatch time.",2)
    c.save(); print(path)

def generate_proposal():
    path=DOCS/"Tidekeeper-Proposal.pdf"; c=canvas.Canvas(str(path),pagesize=(612,792))
    c.setTitle("Tidekeeper - One-page Proposal")
    header(c,"Tidekeeper","Niagara at Scale / Track B / One-page proposal / October 26, 2026")
    y=699
    for title,text in [
        ("Mechanic", "A top-down, one-level rescue game. Move a beacon, attract a school, or drive it with a repel pulse. Rescue 70% of the agents into a sanctuary within 120 seconds. Agent positions determine rescue; progress changes the score and win/lose state."),
        ("Two configurations", "GPU: 100,000+ sprite particles using separation, alignment and cohesion with a spatial neighbor grid. CPU: 500-5,000 Niagara mesh agents with grazing, fleeing and following states, a perception cone, leader roles and obstacle avoidance. Switch configurations and change counts at runtime."),
        ("Budget", "Declare 1920x1080 at 60 fps / 16.6 ms and Niagara GT+RT at most 4 ms. Use p95 frame time to assess sustained frame pacing. Start at 500 CPU agents and 100,000 GPU agents, then sweep counts upward to identify failure. Development-laptop data must be distinguished from lab-machine validation."),
        ("Data reaches gameplay", "C++ supplies beacon position, attraction and pulse controls to a custom Niagara data interface. CPU occupancy returns directly. GPU rescue and overflow totals are reduced into an 8-byte summary and transferred asynchronously through Niagara's GPU readback manager. Run IDs protect restarts from stale results."),
        ("Measurement and optimization", "Capture at least five counts per configuration with stat Niagara, stat GPU, Niagara Debugger and Unreal Insights. Provide a console count control. Compare CPU all-pairs and spatial-hash searches before/after under the same seeded test route; record frame, CPU and GPU times and explain density-dependent limits."),
        ("References", "Borrow indirect flock guidance from Sheepherds! and large, responsive fish schools from ABZU. Official descriptions and trailer links are in ReferenceNotes.md. Personal reference viewing/playing must be completed by the student; no viewing session is asserted here."),
        ("Three-day scope", "Day 1: playable rescue loop, both Niagara configurations and reliable data return. Day 2: count sweeps, measured optimization and runtime checks. Day 3: packaged Windows build, tagged Git release, a maximum two-page performance report and a 60-90-second captured demo."),
    ]:
        y=para(c,f"<b>{title}.</b> {text}",36,y,size=9.5)
    if y<52: raise RuntimeError(f"Proposal overflows: y={y}")
    footer(c,"Individual project / UE 5.7.4 / C++ gameplay and custom Niagara simulation.",1)
    c.save(); print(path)

generate_report()
generate_proposal()
