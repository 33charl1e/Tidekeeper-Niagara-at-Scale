"""Assemble local release files after committing and tagging the editable project."""
import hashlib
import json
import re
import shutil
import subprocess
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Submission'
OUT.mkdir(exist_ok=True)
TAG='v1.0.2'
HAN=re.compile(r'[\u3400-\u4dbf\u4e00-\u9fff\U00020000-\U0002fa1f]')
TEXT_EXT={'.md','.txt','.log','.csv','.json','.ini','.py','.ps1','.cpp','.h','.cs','.usf','.cmd','.uproject','.svg'}
tracked=subprocess.check_output(['git','ls-tree','-r','--name-only','-z',TAG],cwd=ROOT).decode('utf-8').split('\0')
for name in filter(None,tracked):
    if HAN.search(name): raise ValueError(f'Non-English submission filename: {name}')
    if Path(name).suffix.lower() in TEXT_EXT:
        content=subprocess.check_output(['git','show',f'{TAG}:{name}'],cwd=ROOT).decode('utf-8-sig')
        if HAN.search(content): raise ValueError(f'Chinese text in submission: {name}')
commit=subprocess.check_output(['git','rev-parse',f'{TAG}^{{commit}}'],cwd=ROOT,text=True).strip()
# Distribute the English snapshot only. Earlier local versions remain available
# locally without carrying superseded documents into the submission history.
subprocess.run(['git','bundle','create',str(OUT/'Tidekeeper-Repository.bundle'),
                'refs/heads/main',f'refs/tags/{TAG}'],cwd=ROOT,check=True)
subprocess.run(['git','archive','--format=zip','--prefix=NiagaraHerd/',
                f'--output={OUT / "Tidekeeper-Source.zip"}',TAG],cwd=ROOT,check=True)

game=ROOT/'Packaged'/'Windows'
with zipfile.ZipFile(OUT/'Tidekeeper-Windows.zip','w',zipfile.ZIP_DEFLATED,compresslevel=5) as z:
    for f in sorted(game.rglob('*')):
        if not f.is_file(): continue
        rel=f.relative_to(game)
        if f.suffix.lower()=='.pdb' or rel.parts[:2]==('NiagaraHerd','Saved'): continue
        z.write(f,Path('Windows')/rel)

for name in ['Tidekeeper-Proposal.pdf','Tidekeeper-Performance-Report.pdf','Tidekeeper-Demo.mp4']:
    shutil.copy2(ROOT/'Docs'/name,OUT/name)
shutil.copy2(ROOT/'Docs'/'QuickStart.md',OUT/'QuickStart.md')

with zipfile.ZipFile(OUT/'Tidekeeper-Profiling-Evidence.zip','w',zipfile.ZIP_DEFLATED,compresslevel=5) as z:
    trace=ROOT/'Saved'/'Profiling'/'Tidekeeper-Final.utrace'
    z.write(trace,'Tidekeeper-Final.utrace')
    for f in sorted((ROOT/'Docs'/'Results').rglob('*')):
        if f.is_file(): z.write(f,Path('Results')/f.relative_to(ROOT/'Docs'/'Results'))

for name,required in [
    ('Tidekeeper-Source.zip',['NiagaraHerd/NiagaraHerd.uproject','NiagaraHerd/Content/Herd/NS_HerdGPU.uasset','NiagaraHerd/Shaders/Herd.usf','NiagaraHerd/.gitattributes']),
    ('Tidekeeper-Windows.zip',['Windows/NiagaraHerd.exe','Windows/NiagaraHerd/Binaries/Win64/NiagaraHerd.exe']),
    ('Tidekeeper-Profiling-Evidence.zip',['Tidekeeper-Final.utrace','Results/Measurements.csv'])]:
    with zipfile.ZipFile(OUT/name) as z:
        assert z.testzip() is None,name
        assert all(x in z.namelist() for x in required),(name,required)
        for entry in z.namelist():
            if HAN.search(entry): raise ValueError(f'Chinese filename inside {name}: {entry}')
            if Path(entry).suffix.lower() in TEXT_EXT:
                if HAN.search(z.read(entry).decode('utf-8-sig')):
                    raise ValueError(f'Chinese text inside {name}: {entry}')

files=[]
for f in sorted(OUT.iterdir()):
    if f.is_file() and f.name!='Manifest.json':
        if HAN.search(f.name): raise ValueError(f'Chinese submission filename: {f.name}')
        if f.suffix.lower() in TEXT_EXT and HAN.search(f.read_text(encoding='utf-8-sig')):
            raise ValueError(f'Chinese submission text: {f.name}')
        h=hashlib.sha256()
        with f.open('rb') as stream:
            for block in iter(lambda:stream.read(4*1024*1024),b''): h.update(block)
        files.append(dict(name=f.name,bytes=f.stat().st_size,sha256=h.hexdigest()))
(OUT/'Manifest.json').write_text(json.dumps(dict(tag=TAG,commit=commit,language_scan='passed',files=files),indent=2),encoding='utf-8')
print(json.dumps(dict(output=str(OUT),tag=TAG,commit=commit,language_scan='passed',files=files),indent=2))
