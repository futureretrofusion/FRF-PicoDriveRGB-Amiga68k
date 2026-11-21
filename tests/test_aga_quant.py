#!/usr/bin/env python3
"""Host-only regression for pure RGB565 -> indexed/HAM8 encoder functions.

Extracts the *actual* C functions from frf_pd_aga.c and exercises them using
host gcc; no Amiga runtime or display hardware is required.
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "frf-port/src/frf_pd_aga.c").read_text()

def extract(name):
    start = source.index("static unsigned " + name + "(")
    opening = source.index("{", start)
    level = 0
    for i in range(opening, len(source)):
        if source[i] == "{": level += 1
        if source[i] == "}":
            level -= 1
            if level == 0:
                return source[start:i+1]
    raise ValueError(name)

functions = "\n\n".join(extract(x) for x in ("index_from_rgb565", "scale6", "ham8_code"))
program = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t UBYTE;
static UBYTE lookup_array[65536];
static UBYTE *aga_lookup=lookup_array;
__FUNCTIONS__
static int error(unsigned p, unsigned r, unsigned g, unsigned b) {
    int tr=((p>>11)&31),tg=(p>>5)&63,tb=(p&31);
    tr=(tr<<1)|(tr>>4); tb=(tb<<1)|(tb>>4);
    int dr=(int)r-tr,dg=(int)g-tg,db=(int)b-tb;
    return 3*dr*dr+4*dg*dg+2*db*db;
}
int main(void) {
    unsigned v,m,i;
    static const int bits[3][3]={{2,2,2},{3,2,2},{3,3,2}};
    for(m=0;m<3;m++) {
        UBYTE seen[256]={0};
        unsigned colors=1U<<(bits[m][0]+bits[m][1]+bits[m][2]);
        for(v=0;v<65536U;v++) {
            i=index_from_rgb565(v,bits[m][0],bits[m][1],bits[m][2]);
            if(i>=colors) return 10;
            seen[i]=1;
        }
        if(index_from_rgb565(0,bits[m][0],bits[m][1],bits[m][2])!=0) return 11;
        if(index_from_rgb565(0xffff,bits[m][0],bits[m][1],bits[m][2])!=colors-1) return 12;
        for(i=0;i<colors;i++) if(!seen[i]) return 13;
        printf("AGA%u: full palette range valid\n",colors);
    }
    for(v=0;v<65536U;v++) aga_lookup[v]=index_from_rgb565(v,2,2,2);
    {
        unsigned pr=0,pg=0,pb=0,dr=0,dg=0,db=0;
        for(v=0;v<65536U;v++) {
            unsigned oldr=dr,oldg=dg,oldb=db;
            unsigned code=ham8_code(v,&pr,&pg,&pb);
            unsigned op=code>>6,component=code&63U;
            if(code>255) return 20;
            if(op==0) {
                dr=scale6(2,(code>>4)&3);
                dg=scale6(2,(code>>2)&3);
                db=scale6(2,code&3);
            } else if(op==1) { dr=oldr;dg=oldg;db=component; }
              else if(op==2) { dr=component;dg=oldg;db=oldb; }
              else { dr=oldr;dg=component;db=oldb; }
            if(dr!=pr || dg!=pg || db!=pb) return 21;
            {
                unsigned direct=aga_lookup[v];
                int baseline=error(v,scale6(2,(direct>>4)&3),
                    scale6(2,(direct>>2)&3),scale6(2,direct&3));
                if(error(v,dr,dg,db)>baseline) return 22;
            }
        }
        puts("HAM8: 65536 RGB565 candidates code/decode and error guard PASS");
    }
    return 0;
}
""".replace("__FUNCTIONS__", functions)
with tempfile.TemporaryDirectory(prefix="frf-aga-test-") as tmp:
    p = Path(tmp) / "test_aga.c"
    exe = Path(tmp) / "test_aga"
    p.write_text(program)
    subprocess.run(["gcc", "-std=c99", "-O2", "-Wall", "-Werror", str(p), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("Host-side AGA palette/HAM8 encoder regression PASS")
