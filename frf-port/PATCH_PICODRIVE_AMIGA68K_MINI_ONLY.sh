#!/usr/bin/env bash
set -euo pipefail
ROOT="${1:-}"
[[ -n "$ROOT" && -f "$ROOT/Makefile.libretro" && -f "$ROOT/platform/libretro/libretro.c" ]] || {
  echo "Usage: $0 /path/to/notaz-picodrive" >&2; exit 2; }

python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys
root=Path(sys.argv[1])
base_mark='FRF_PICODRIVE_AMIGA68K_V0_1'
mark='FRF_PICODRIVE_AMIGA68K_V0_1_2'

def func_span(text, signature):
    start=text.find(signature)
    if start < 0:
        raise SystemExit('ERROR: function anchor not found: '+signature)
    b=text.find('{', start+len(signature))
    if b < 0:
        raise SystemExit('ERROR: opening brace not found: '+signature)
    depth=0; state='code'; i=b
    while i < len(text):
        c=text[i]; n=text[i+1] if i+1 < len(text) else ''
        if state=='code':
            if c=='/' and n=='/': state='line'; i+=2; continue
            if c=='/' and n=='*': state='block'; i+=2; continue
            if c=='"': state='str'; i+=1; continue
            if c=="'": state='char'; i+=1; continue
            if c=='{': depth+=1
            elif c=='}':
                depth-=1
                if depth==0: return start,i+1
            i+=1; continue
        if state=='line':
            if c=='\n': state='code'
            i+=1; continue
        if state=='block':
            if c=='*' and n=='/': state='code'; i+=2
            else: i+=1
            continue
        q='"' if state=='str' else "'"
        if c=='\\': i+=2
        elif c==q: state='code'; i+=1
        else: i+=1
    raise SystemExit('ERROR: closing brace not found: '+signature)

# ------------------------------------------------------------------
# Baseline V0.1 static Amiga libretro target. Fresh trees get it here;
# trees previously patched by V0.1/V0.1.1 are upgraded in place below.
# ------------------------------------------------------------------
mk=root/'Makefile.libretro'; s=mk.read_text()
if base_mark not in s:
    anchor='# Unix\nifeq ($(platform), unix)'
    if anchor not in s: raise SystemExit('ERROR: Makefile.libretro Unix anchor not found')
    s=s.replace(anchor,r'''# FRF_PICODRIVE_AMIGA68K_V0_1
ifeq ($(platform), amiga68k)
	ARCH := m68k
	TARGET := $(TARGET_NAME)_libretro_amiga68k.a
	CC := $(FRF_AMIGA_CC)
	CC_AS := $(CC)
	AR := $(FRF_AMIGA_AR)
	CFLAGS += -D__AMIGA__ -D__amigaos__ -DFRF_PICODRIVE_AMIGA68K=1
	CFLAGS += $(FRF_AMIGA_CPUFLAGS) $(FRF_AMIGA_OPTFLAGS) -ffunction-sections -fdata-sections
	ASFLAGS += $(FRF_AMIGA_CPUFLAGS)
	CFLAGS += -DMSB_FIRST=1
	NO_ARM_ASM := 1
	STATIC_LINKING := 1
	STATIC_LINKING_LINK := 1
	LOW_MEMORY := 1
	LIBM := -lm

# Unix
else ifeq ($(platform), unix)''',1)
else:
    # V0.1.1 passed FAMEC_NO_GOTOS globally while famec.c defines it itself.
    # Remove only the exact FRF target occurrence to keep the build quiet.
    s=s.replace('-D__amigaos__ -DFRF_PICODRIVE_AMIGA68K=1 -DFAMEC_NO_GOTOS',
                '-D__amigaos__ -DFRF_PICODRIVE_AMIGA68K=1')
mk.write_text(s)

lr=root/'platform/libretro/libretro.c'; s=lr.read_text()
if base_mark not in s:
    anchor='#ifdef _3DS\ntypedef struct'
    if anchor not in s: raise SystemExit('ERROR: libretro.c mmap anchor not found')
    s=s.replace(anchor,r'''#if defined(FRF_PICODRIVE_AMIGA68K)
/* FRF_PICODRIVE_AMIGA68K_V0_1 */
void *plat_mmap(unsigned long addr, size_t size, int need_exec, int is_fixed)
{ (void)addr; (void)need_exec; (void)is_fixed; return malloc(size); }
void *plat_mremap(void *ptr, size_t oldsize, size_t newsize)
{ (void)oldsize; return realloc(ptr, newsize); }
void plat_munmap(void *ptr, size_t size)
{ (void)size; free(ptr); }
#elif defined(_3DS)
typedef struct''',1)

# V0.1.2: the tiny MD/Genesis proof has no Sega Pico page artwork UI.
# Compile the overlay loader down to a NULL-returning stub on this target,
# removing rpng/file-path dependencies from the final static link.
if mark not in s:
    sig='static unsigned short *load_pico_overlay(int page, int w, int h)'
    a,b=func_span(s,sig)
    old=s[a:b]
    wrapped=(r'''#if defined(FRF_PICODRIVE_AMIGA68K)
/* FRF_PICODRIVE_AMIGA68K_V0_1_2
 * Mega Drive/Genesis V0.1 proof does not use Sega Pico artwork overlays.
 */
static unsigned short *load_pico_overlay(int page, int w, int h)
{
   (void)page; (void)w; (void)h;
   return NULL;
}
#else
''' + old + '\n#endif /* FRF_PICODRIVE_AMIGA68K */')
    s=s[:a]+wrapped+s[b:]
lr.write_text(s)

mh=root/'platform/libretro/libretro-common/include/memmap.h'; s=mh.read_text()
if base_mark not in s:
    anchor='#if defined(PSP) || defined(PS2) || defined(GEKKO)'
    if anchor not in s: raise SystemExit('ERROR: memmap.h platform anchor not found')
    s=s.replace(anchor,
        '#if defined(FRF_PICODRIVE_AMIGA68K) || defined(PSP) || defined(PS2) || defined(GEKKO)',1)
    s=s.replace('/* No mman available */', '/* No mman available */\n/* FRF_PICODRIVE_AMIGA68K_V0_1 */',1)
    mh.write_text(s)

# V0.1.2: gettimeofday is used only for Sega Pico keyboard repeat timing.
# Keep the symbol path link-free on Amiga. Ordinary MD execution never calls it.
pm=root/'pico/pico/memory.c'; s=pm.read_text()
if mark not in s:
    sig='u64 get_ticks(void)'
    a,b=func_span(s,sig)
    old=s[a:b]
    wrapped=(r'''#if defined(FRF_PICODRIVE_AMIGA68K)
/* FRF_PICODRIVE_AMIGA68K_V0_1_2
 * No Sega Pico keyboard in the V0.1 MD proof. Avoid a host gettimeofday
 * dependency while retaining a monotonic fallback if this code is touched.
 */
u64 get_ticks(void)
{
   static u64 frf_ticks;
   return ++frf_ticks;
}
#else
''' + old + '\n#endif /* FRF_PICODRIVE_AMIGA68K */')
    s=s[:a]+wrapped+s[b:]
    pm.write_text(s)

print('FRF PicoDrive Amiga68k V0.1.2 static-core patch present')
PY
