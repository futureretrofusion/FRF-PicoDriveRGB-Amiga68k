/* FRF PicoDriveRGB v0.2.20: native AGA 64/128/256 indexed and HAM8.
 *
 * RGB565 -> palette/HAM8 codes -> CHIP planar UWORD writes. Palette is a
 * deterministic RGB cube (no dynamic palette flicker). AGA HAM8 uses
 * six-bit hold-and-modify channels and a 64-colour base RGB cube.
 *
 * First implementation: 68k CPU conversion, single displayed bitmap.
 * No HAM6 ARM acceleration here; HAM6 remains unchanged as a separate mode.
 */
#include "frf_pd_aga.h"
#include "frf_pd_keys.h"
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>

#define AGA_W 320U
#define AGA_MAX_H 256U
#define AGA_WORDS 20U
#define AGA_MAX_PLANES 8U
#define AGA_TABLE_BYTES (65536UL)
#define AGA_SHADOW_BYTES (AGA_MAX_PLANES*AGA_MAX_H*AGA_WORDS*sizeof(UWORD))

extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;
static int own_intuition,own_graphics;
static struct Screen *aga_screen;
static struct BitMap *aga_bitmap;
static UBYTE *aga_lookup;
static UWORD *aga_shadow;
static unsigned aga_colors=256U;
static unsigned aga_depth=8U;
static unsigned aga_h=256U;
static unsigned aga_refresh=50U;
static ULONG aga_row_stride;
static unsigned long aga_frames,aga_changed_words;
static int aga_active;

/* p in range 0..63 for HAM8, 0..255 for 8-bit palette. */
static unsigned index_from_rgb565(unsigned rgb,int bitsr,int bitsg,int bitsb)
{
    unsigned r=(rgb>>11)&31U, g=(rgb>>5)&63U, b=rgb&31U;
    unsigned qr=(r*((1U<<bitsr)-1U)+15U)/31U;
    unsigned qg=(g*((1U<<bitsg)-1U)+31U)/63U;
    unsigned qb=(b*((1U<<bitsb)-1U)+15U)/31U;
    return (qr<<(bitsg+bitsb))|(qg<<bitsb)|qb;
}
static unsigned scale6(unsigned bits,unsigned level)
{
    return (level*63U+((1U<<bits)-1U)/2U)/((1U<<bits)-1U);
}
static ULONG color32(unsigned q,unsigned levels)
{
    unsigned v=(q*255U+(levels-1U)/2U)/(levels-1U);
    return (ULONG)v*0x01010101UL;
}
static void make_palette(void)
{
    ULONG table[2+256*3];
    unsigned i,n=aga_colors?aga_colors:64U;
    int br=aga_colors==64U?2:(aga_colors==128U?3:(aga_colors==256U?3:2));
    int bg=aga_colors==64U?2:(aga_colors==128U?2:(aga_colors==256U?3:2));
    int bb=2;
    table[0]=((ULONG)n<<16);
    for(i=0;i<n;i++) {
        unsigned r=(i>>(bg+bb))&((1U<<br)-1U);
        unsigned g=(i>>bb)&((1U<<bg)-1U);
        unsigned b=i&((1U<<bb)-1U);
        table[1+i*3+0]=color32(r,1U<<br);
        table[1+i*3+1]=color32(g,1U<<bg);
        table[1+i*3+2]=color32(b,1U<<bb);
    }
    table[1+n*3]=0;
    LoadRGB32(&aga_screen->ViewPort,table);
    for(i=0;i<65536UL;i++)
        aga_lookup[i]=(UBYTE)index_from_rgb565(i,br,bg,bb);
}
static ULONG choose_mode(ULONG monitor,unsigned h,unsigned depth,int ham)
{
    return BestModeID(BIDTAG_NominalWidth,AGA_W,
        BIDTAG_NominalHeight,h, BIDTAG_DesiredWidth,AGA_W,
        BIDTAG_DesiredHeight,h, BIDTAG_Depth,depth,
        BIDTAG_DIPFMustHave,ham?DIPF_IS_HAM:0,
        BIDTAG_DIPFMustNotHave,ham?(DIPF_IS_LACE|DIPF_IS_EXTRAHALFBRITE|DIPF_IS_DUALPF):
            (DIPF_IS_HAM|DIPF_IS_LACE|DIPF_IS_EXTRAHALFBRITE|DIPF_IS_DUALPF),
        BIDTAG_MonitorID,monitor,TAG_DONE);
}
void frf_pd_aga_config(int colors)
{
    if(colors==0||colors==64||colors==128||colors==256)
        aga_colors=(unsigned)colors;
}
void frf_pd_aga_shutdown(void)
{
    frf_pd_keys_shutdown();
    if(aga_screen) {ScreenToBack(aga_screen); CloseScreen(aga_screen); aga_screen=NULL;}
    aga_bitmap=NULL;
    if(aga_lookup) { FreeVec(aga_lookup); aga_lookup=NULL; }
    if(aga_shadow) { FreeVec(aga_shadow); aga_shadow=NULL; }
    if(own_graphics && GfxBase) {CloseLibrary((struct Library*)GfxBase); GfxBase=NULL;}
    if(own_intuition && IntuitionBase) {CloseLibrary((struct Library*)IntuitionBase);IntuitionBase=NULL;}
    own_graphics=own_intuition=0;
    aga_active=0;
}
int frf_pd_aga_init_for_fps(double fps)
{
    ULONG mode, monitor;
    unsigned plane,y;
    int attempts;
    if(aga_screen) return 1;
    if(!IntuitionBase) {
        IntuitionBase=(struct IntuitionBase*)OpenLibrary("intuition.library",39);
        if(IntuitionBase) own_intuition=1;
    }
    if(!GfxBase) {
        GfxBase=(struct GfxBase*)OpenLibrary("graphics.library",39);
        if(GfxBase) own_graphics=1;
    }
    if(!IntuitionBase || !GfxBase) goto fail;
    aga_depth=aga_colors==64U?6U:(aga_colors==128U?7U:8U);
    aga_refresh=(fps>=55.0)?60U:50U;
    aga_h=(aga_refresh==60U)?240U:256U;
    monitor=(aga_refresh==60U)?NTSC_MONITOR_ID:PAL_MONITOR_ID;
    mode=INVALID_ID;
    for(attempts=0;attempts<2;attempts++) {
        mode=choose_mode(monitor,aga_h,aga_depth,aga_colors==0U);
        if(mode!=INVALID_ID) break;
        if(aga_refresh==50U) break;
        aga_refresh=50U; aga_h=256U; monitor=PAL_MONITOR_ID;
    }
    if(mode==INVALID_ID) {
        printf("[FRF AGA] No AGA %s mode available\n",aga_colors==0U?"HAM8":"palette");
        goto fail;
    }
    printf("[FRF AGA] Opening mode=%08lx %ux%u depth=%u colors=%u refresh=%uHz\n",
        (unsigned long)mode,AGA_W,aga_h,aga_depth,aga_colors,aga_refresh);
    aga_screen=OpenScreenTags(NULL, SA_DisplayID,mode,
        SA_Width,AGA_W, SA_Height,aga_h, SA_Depth,aga_depth,
        SA_AutoScroll,FALSE, SA_ShowTitle,FALSE, SA_Quiet,TRUE,
        SA_Behind,FALSE, SA_Type,CUSTOMSCREEN,
        SA_Interleaved,TRUE,TAG_DONE);
    if(!aga_screen) {puts("[FRF AGA] OpenScreenTags failed");goto fail;}
    aga_bitmap=aga_screen->RastPort.BitMap;
    if(!aga_bitmap || aga_bitmap->Depth!=aga_depth ||
       aga_bitmap->Rows<aga_h || aga_bitmap->BytesPerRow<AGA_W/8U) {
        puts("[FRF AGA] Unsafe or mismatched bitmap geometry/depth");goto fail;
    }
    aga_row_stride=aga_bitmap->BytesPerRow;
    for(plane=0;plane<aga_depth;plane++) {
        if(!aga_bitmap->Planes[plane] ||
           !(TypeOfMem((APTR)aga_bitmap->Planes[plane])&MEMF_CHIP)) {
            puts("[FRF AGA] Refused non-CHIP planar bitmap");goto fail;
        }
    }
    /* Both sequential and interleaved BitMaps are handled by per-plane
     * row pointers and the actual BytesPerRow returned by graphics.library. */
    aga_lookup=(UBYTE*)AllocVec(AGA_TABLE_BYTES,MEMF_FAST);
    if(!aga_lookup)aga_lookup=(UBYTE*)AllocVec(AGA_TABLE_BYTES,MEMF_PUBLIC);
    aga_shadow=(UWORD*)AllocVec(AGA_SHADOW_BYTES,MEMF_FAST|MEMF_CLEAR);
    if(!aga_shadow)aga_shadow=(UWORD*)AllocVec(AGA_SHADOW_BYTES,MEMF_PUBLIC|MEMF_CLEAR);
    if(!aga_lookup||!aga_shadow){puts("[FRF AGA] Not enough RAM for lookup/shadow");goto fail;}
    WaitBlit();
    for(plane=0;plane<aga_depth;plane++)
        for(y=0;y<aga_h;y++)
            memset((UBYTE*)aga_bitmap->Planes[plane]+y*aga_row_stride,0,AGA_W/8U);
    make_palette();
    aga_active=1;
    aga_frames=aga_changed_words=0;
    ScreenToFront(aga_screen);MakeScreen(aga_screen);RethinkDisplay();
    frf_pd_keys_attach(aga_screen);
    printf("[FRF AGA] Active: %s, %u planes, CHIP writes, CPU 68k converter\n",
        aga_colors==0U?"HAM8":(aga_colors==64U?"AGA64":(aga_colors==128U?"AGA128":"AGA256")),aga_depth);
    return 1;
fail:
    frf_pd_aga_shutdown();
    return 0;
}
unsigned frf_pd_aga_refresh_hz(void){return aga_refresh;}
void frf_pd_aga_wait_vblank(void){if(aga_screen && GfxBase) WaitTOF();}
static unsigned ham8_code(unsigned px,unsigned *pr,unsigned *pg,unsigned *pb)
{
    unsigned r=((px>>11)&31U); r=(r<<1)|(r>>4);
    unsigned g=(px>>5)&63U;
    unsigned b=px&31U; b=(b<<1)|(b>>4);
    unsigned d=aga_lookup[px], dr=scale6(2,(d>>4)&3U);
    unsigned dg=scale6(2,(d>>2)&3U), db=scale6(2,d&3U);
    long er=(long)r-(long)dr,eg=(long)g-(long)dg,eb=(long)b-(long)db;
    unsigned best=(unsigned)(3*er*er+4*eg*eg+2*eb*eb);
    unsigned code=d,cr=dr,cg=dg,cb=db;
    unsigned e;
    er=(long)r-(long)*pr; eg=(long)g-(long)*pg;
    e=(unsigned)(3*er*er+4*eg*eg);
    if(e<best){best=e;code=0x40U|b;cr=*pr;cg=*pg;cb=b;}
    eg=(long)g-(long)*pg; eb=(long)b-(long)*pb;
    e=(unsigned)(4*eg*eg+2*eb*eb);
    if(e<best){best=e;code=0x80U|r;cr=r;cg=*pg;cb=*pb;}
    er=(long)r-(long)*pr;eb=(long)b-(long)*pb;
    e=(unsigned)(3*er*er+2*eb*eb);
    if(e<best){code=0xC0U|g;cr=*pr;cg=g;cb=*pb;}
    *pr=cr;*pg=cg;*pb=cb;
    return code;
}
int frf_pd_aga_present_rgb565(const void *pixels,unsigned width,unsigned height,unsigned pitch)
{
    unsigned y,x,plane,word,bit,dx,dy,pr,pg,pb,changed=0;
    UBYTE codes[AGA_W];
    if(!aga_active||!pixels||!aga_bitmap||width>AGA_W||height>aga_h||
       !width||!height||pitch<width*2U) return 0;
    dx=(AGA_W-width)/2U;dy=(aga_h-height)/2U;
    WaitBlit();
    for(y=0;y<aga_h;y++) {
        UWORD packed[AGA_MAX_PLANES][AGA_WORDS];
        memset(codes,0,sizeof(codes));
        if(y>=dy && y<dy+height) {
            const UWORD *src=(const UWORD*)((const UBYTE*)pixels+(y-dy)*pitch);
            pr=pg=pb=0;
            for(x=0;x<width;x++) {
                unsigned p=src[x];
                codes[dx+x]=(UBYTE)(aga_colors?aga_lookup[p]:ham8_code(p,&pr,&pg,&pb));
            }
        }
        memset(packed,0,sizeof(packed));
        for(word=0;word<AGA_WORDS;word++)
            for(bit=0;bit<16U;bit++) {
                unsigned code=codes[word*16U+bit];
                UWORD mask=(UWORD)(0x8000U>>bit);
                for(plane=0;plane<aga_depth;plane++)
                    if(code&(1U<<plane))packed[plane][word]|=mask;
            }
        for(plane=0;plane<aga_depth;plane++) {
            UWORD *shadow=aga_shadow+(plane*AGA_MAX_H+y)*AGA_WORDS;
            UWORD *dest=(UWORD*)((UBYTE*)aga_bitmap->Planes[plane]+y*aga_row_stride);
            for(word=0;word<AGA_WORDS;word++)
                if(shadow[word]!=packed[plane][word]) {
                    dest[word]=packed[plane][word];
                    shadow[word]=packed[plane][word]; changed++;
                }
        }
    }
    aga_frames++;aga_changed_words+=changed;
    if(aga_frames%250U==0U)
        printf("[FRF AGA] frames=%lu changed_words=%lu avg/frame=%lu\n",
            aga_frames,aga_changed_words,aga_changed_words/aga_frames);
    return 1;
}
