#!/usr/bin/env python3
"""Host-only test of the actual hardware joystick decoder in frf_pd_input.c."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "frf-port/src/frf_pd_input.c").read_text()
start = source.index("static ULONG frf_hw_decode(")
opened = source.index("{", start)
depth = 0
end = None
for i in range(opened, len(source)):
    depth += (source[i] == "{") - (source[i] == "}")
    if depth == 0:
        end = i + 1
        break
assert end
fn = source[start:end]
head = """
#include <stdio.h>
#include <stdint.h>
typedef uint32_t ULONG;
typedef uint16_t UWORD;
typedef uint8_t UBYTE;
#define JPF_JOY_LEFT (1UL<<0)
#define JPF_JOY_RIGHT (1UL<<1)
#define JPF_JOY_UP (1UL<<2)
#define JPF_JOY_DOWN (1UL<<3)
#define JPF_BUTTON_RED (1UL<<4)
"""
test = """
#define F JPF_BUTTON_RED
#define L JPF_JOY_LEFT
#define R JPF_JOY_RIGHT
#define U JPF_JOY_UP
#define D JPF_JOY_DOWN
struct sample { UWORD joy; UBYTE pra; int connector; ULONG want; const char* name; };
int main(void) {
    static const struct sample cases[] = {
        {0,0xff,2,0,"neutral 2"}, {0x0100,0xff,2,U,"up"},
        {0x0001,0xff,2,D,"down"}, {0x0300,0xff,2,L,"left"},
        {0x0003,0xff,2,R,"right"},
        {0x0200,0xff,2,L|U,"up-left"},
        {0x0301,0xff,2,L|D,"down-left"},
        {0x0103,0xff,2,R|U,"up-right"},
        {0x0002,0xff,2,R|D,"down-right"},
        {0,0x7f,2,F,"fire port 2"},
        {0x0200,0x7f,2,L|U|F,"up-left + fire"},
        {0,0xbf,1,F,"fire port 1"},
        {0,0xbf,2,0,"port 1 fire ignored on port 2"},
        {0,0x7f,1,0,"port 2 fire ignored on port 1"}
    };
    unsigned i;
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        ULONG got=frf_hw_decode(cases[i].joy,cases[i].pra,cases[i].connector);
        if(got!=cases[i].want) {
            fprintf(stderr,"FAIL: %s joy=%04x pra=%02x expected=%x got=%x\\n",
                cases[i].name,cases[i].joy,cases[i].pra,
                (unsigned)cases[i].want,(unsigned)got);
            return 1;
        }
    }
    puts("PASS: neutral, eight directions, diagonals, fire, physical port 1/2");
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="frf-joytest-") as tmp:
    inp = Path(tmp) / "input.c"
    exe = Path(tmp) / "input"
    inp.write_text(head + fn + test)
    subprocess.run(["gcc","-std=c99","-Wall","-Wextra","-Werror","-O2",str(inp),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
