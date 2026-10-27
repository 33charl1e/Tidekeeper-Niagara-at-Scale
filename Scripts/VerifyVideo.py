"""Check MP4 structure and compare decoded frames with original capture PNGs."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--frames',type=Path,required=True)
p.add_argument('--video',type=Path,default=ROOT/'Docs'/'Tidekeeper-Demo.mp4')
p.add_argument('--encoder',type=Path,default=ROOT/'Saved'/'EncodeCapture.exe')
p.add_argument('--output',type=Path,default=ROOT/'Docs'/'Results'/'Evidence'/'VideoVerification.json')
args=p.parse_args()

def boxes(data,start=0,end=None):
    end=len(data) if end is None else end
    while start+8<=end:
        size,kind=struct.unpack_from('>I4s',data,start)
        header=8
        if size==1:
            size=struct.unpack_from('>Q',data,start+8)[0]; header=16
        elif size==0: size=end-start
        if size<header or start+size>end: raise ValueError('Invalid MP4 box')
        yield kind,data[start+header:start+size]
        start+=size

def child(data,name):
    return next(payload for kind,payload in boxes(data) if kind==name)

data=args.video.read_bytes()
movie=child(data,b'moov')
tracks=[]
for kind,track in boxes(movie):
    if kind!=b'trak': continue
    media=child(track,b'mdia')
    if child(media,b'hdlr')[8:12]!=b'vide': continue
    header=child(track,b'tkhd')
    width,height=struct.unpack('>II',header[-8:])
    mdhd=child(media,b'mdhd')
    off=20 if mdhd[0] else 12
    timescale=struct.unpack_from('>I',mdhd,off)[0]
    duration=struct.unpack_from('>Q' if mdhd[0] else '>I',mdhd,off+4)[0]
    samples=child(child(child(media,b'minf'),b'stbl'),b'stsz')
    count=struct.unpack_from('>I',samples,8)[0]
    matrix=struct.unpack_from('>9i',header,52 if header[0] else 40)
    assert matrix==(65536,0,0,0,65536,0,0,0,1073741824),'Unexpected video transform'
    tracks.append(dict(width=width>>16,height=height>>16,frames=count,
                       duration_seconds=duration/timescale,fps=count*timescale/duration,
                       transform='identity'))
assert len(tracks)==1,'Expected exactly one video track'
info=tracks[0]
assert (info['width'],info['height'],info['frames'])==(1920,1080,2250),info
assert abs(info['duration_seconds']-75)<.001 and abs(info['fps']-30)<.001,info

qa=ROOT/'Saved'/'VideoRevision'
qa.mkdir(exist_ok=True,parents=True)
comparisons=[]
for seconds,label in [(12,'CPU'),(44,'GPU'),(70,'Stats')]:
    output=qa/f'checked-{label}.png'
    subprocess.run([str(args.encoder),'--extract',str(args.video),str(output),str(seconds)],check=True)
    actual=np.asarray(Image.open(output).convert('RGB'),dtype=np.int16)
    original=np.asarray(Image.open(args.frames/f'Frame{seconds*30:06d}.png').convert('RGB'),dtype=np.int16)
    alternatives=dict(identity=original,vertical_flip=original[::-1],
                      horizontal_flip=original[:,::-1],rotate_180=original[::-1,::-1])
    errors={name:round(float(np.abs(actual-expected).mean()),5) for name,expected in alternatives.items()}
    assert min(errors,key=errors.get)=='identity',errors
    assert errors['identity']<6 and min(v for k,v in errors.items() if k!='identity')>errors['identity']*2,errors
    comparisons.append(dict(segment=label,seconds=seconds,source_frame=seconds*30,
                            mean_absolute_pixel_error=errors,orientation='upright'))
report=dict(video=args.video.name,video_sha256=hashlib.sha256(data).hexdigest(),container=info,decoder='Media Foundation NV12, top-down planar YUV',
            comparison='Decoded pixels compared with original capture PNGs without display transforms',
            samples=comparisons,result='passed')
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
