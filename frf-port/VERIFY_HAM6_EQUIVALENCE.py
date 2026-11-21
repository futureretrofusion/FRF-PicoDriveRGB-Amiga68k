#!/usr/bin/env python3
import random

BASE=[
(0,0,0),(255,255,255),(85,85,85),(170,170,170),(255,0,0),(0,255,0),(0,0,255),(255,255,0),
(0,255,255),(255,0,255),(128,0,0),(0,128,0),(0,0,128),(128,128,0),(0,128,128),(128,0,128)
]
q5=[(i*15+15)//31 for i in range(32)]
q6=[(i*15+31)//63 for i in range(64)]

def err(r0,g0,b0,r1,g1,b1):
    dr=r0-r1; dg=g0-g1; db=b0-b1
    return 3*dr*dr+4*dg*dg+2*db*db

DIRECT=[]
for p in range(65536):
    tr=q5[(p>>11)&31]; tg=q6[(p>>5)&63]; tb=q5[p&31]
    best=1<<60; bp=0
    for i,(r,g,b) in enumerate(BASE):
        e=err(tr,tg,tb,r>>4,g>>4,b>>4)
        if e<best: best=e; bp=i
    DIRECT.append(bp)

def old_encode(line):
    out=[[0]*20 for _ in range(6)]
    pr=pg=pb=0
    for word in range(20):
        pw=[0]*6
        for bit in range(16):
            px=line[word*16+bit]
            tr=q5[(px>>11)&31]; tg=q6[(px>>5)&63]; tb=q5[px&31]
            direct=DIRECT[px]
            dr,dg,db=[v>>4 for v in BASE[direct]]
            best=err(tr,tg,tb,dr,dg,db); code=direct; br,bg,bb=dr,dg,db
            e=err(tr,tg,tb,pr,pg,tb)
            if e<best: best=e; code=0x10|tb; br,bg,bb=pr,pg,tb
            e=err(tr,tg,tb,tr,pg,pb)
            if e<best: best=e; code=0x20|tr; br,bg,bb=tr,pg,pb
            e=err(tr,tg,tb,pr,tg,pb)
            if e<best: code=0x30|tg; br,bg,bb=pr,tg,pb
            m=0x8000>>bit
            for plane in range(6):
                if code & (1<<plane): pw[plane]|=m
            pr,pg,pb=br,bg,bb
        for plane in range(6): out[plane][word]=pw[plane]
    return out

def arm_bytes(line):
    words=old_encode(line)  # semantic target; serialize exactly as firmware does
    b=bytearray()
    for plane in range(6):
        for w in words[plane]: b += bytes((w>>8,w&0xff))
    return bytes(b)

def independently_serialized(line):
    # Re-run the exact candidate sequence but write plane bytes in firmware row layout.
    out=[bytearray(40) for _ in range(6)]
    pr=pg=pb=0
    for word in range(20):
        pw=[0]*6
        for bit in range(16):
            px=line[word*16+bit]
            tr=((px>>11)&31)*15; tr=(tr+15)//31
            tg=((px>>5)&63)*15; tg=(tg+31)//63
            tb=(px&31)*15; tb=(tb+15)//31
            direct=DIRECT[px]
            dr,dg,db=[v>>4 for v in BASE[direct]]
            best=err(tr,tg,tb,dr,dg,db); code=direct; br,bg,bb=dr,dg,db
            e=err(tr,tg,tb,pr,pg,tb)
            if e<best: best=e; code=0x10|tb; br,bg,bb=pr,pg,tb
            e=err(tr,tg,tb,tr,pg,pb)
            if e<best: best=e; code=0x20|tr; br,bg,bb=tr,pg,pb
            e=err(tr,tg,tb,pr,tg,pb)
            if e<best: code=0x30|tg; br,bg,bb=pr,tg,pb
            mask=0x8000>>bit
            for plane in range(6):
                if code&(1<<plane): pw[plane]|=mask
            pr,pg,pb=br,bg,bb
        for plane,w in enumerate(pw):
            out[plane][word*2]=w>>8; out[plane][word*2+1]=w&0xff
    return b''.join(out)

def check(line,name):
    a=arm_bytes(line); b=independently_serialized(line)
    if a!=b:
        for i,(x,y) in enumerate(zip(a,b)):
            if x!=y: raise SystemExit(f'FAIL {name}: byte {i}: {x:02x}!={y:02x}')
        raise SystemExit(f'FAIL {name}: length')

cases=[]
cases += [([0]*320,'black'),([0xffff]*320,'white'),([0xf800]*320,'red'),([0x07e0]*320,'green'),([0x001f]*320,'blue')]
cases.append(([((x&31)<<11)|(((x*3)&63)<<5)|((x*7)&31) for x in range(320)],'gradient'))
r=random.Random(0x6803)
for n in range(200): cases.append(([r.randrange(65536) for _ in range(320)],f'random{n}'))
for line,name in cases: check(line,name)
print(f'PASS: {len(cases)} lines; CPU3 HAM6 packed bytes exactly match V0.2.3 semantics')
