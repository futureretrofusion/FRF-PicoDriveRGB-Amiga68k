/*
 * FRF PicoDriveRGB Emu68 V0.2.16C - dynamic-geometry-safe ScreenBuffer HAM6 presenter
 *
 * Derived from the proven FRF PocketSNESRGB/MAME106RGB Direct-Chip HAM6
 * architecture: RGB565 source comparison in FAST RAM, fused HAM6 selection
 * + six-plane packing, then dirty contiguous UWORD runs written to the
 * displayed CHIP bitmap. V0.2.16 keeps that direct-front path as the default,
 * and adds an optional two-ScreenBuffer page-flip path for CPU3 HAM6 output.
 * The optional path never writes the currently displayed bitmap.
 *
 * This file is deliberately independent of PicoDrive internals.  A frontend
 * only has to pass the completed RGB565 frame to
 * frf_pd_ham6_present_rgb565().
 */

#include "frf_pd_ham6.h"
#include "frf_pd_coproc_client.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(__amigaos__) || defined(__AMIGA__) || defined(AMIGA) || defined(__amiga__)
#define FRF_PD_AMIGA 1
#else
#define FRF_PD_AMIGA 0
#endif

#if FRF_PD_AMIGA

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <graphics/rastport.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#ifndef MEMF_FAST
#define MEMF_FAST 0
#endif

#define FRF_PD_W              320U
#define FRF_PD_H              256U /* maximum host screen height */
#define FRF_PD_MAX_SRC_H      240U
#define FRF_PD_PLANES         6U
#define FRF_PD_WORDS          (FRF_PD_W/16U)
#define FRF_PD_PLANE_ROW      (FRF_PD_W/8U)
#define FRF_PD_SHADOW_WORDS   (FRF_PD_PLANES*FRF_PD_H*FRF_PD_WORDS)
#define FRF_PD_PREV_PIXELS    (FRF_PD_W*FRF_PD_MAX_SRC_H)

/* The Amiga GCC proto stubs use these library-base symbols. */
/* Weak definitions coexist with an Amiga frontend/CRT which already owns
 * the conventional library-base symbols. We only close bases opened here. */
struct ExecBase *SysBase __attribute__((weak)) = NULL;
struct IntuitionBase *IntuitionBase __attribute__((weak)) = NULL;
struct GfxBase *GfxBase __attribute__((weak)) = NULL;
static UBYTE frf_own_intuition = 0;
static UBYTE frf_own_graphics = 0;

static struct Screen *frf_screen = NULL;
static struct BitMap *frf_bm = NULL;
static UWORD *frf_planar_shadow = NULL;
static UWORD *frf_prev_rgb565 = NULL;
static UBYTE *frf_direct_pen = NULL;
static ULONG frf_row_stride = 0;
static UBYTE frf_interleaved = 0;
static UBYTE frf_started = 0;
static UBYTE frf_prev_valid = 0;
static unsigned frf_prev_w = 0, frf_prev_h = 0;
static unsigned frf_screen_h = 256U;
static unsigned frf_refresh_hz = 50U;
static ULONG frf_monitor_id = PAL_MONITOR_ID;
static ULONG frf_frames = 0, frf_chip_bytes = 0, frf_runs = 0;
static ULONG frf_changed_lines = 0, frf_reused_lines = 0;
static UBYTE frf_arm_ham6 = 0;
static ULONG frf_arm_fallbacks = 0;
static ULONG frf_arm_pipe_submitted = 0;
static ULONG frf_arm_pipe_presented = 0;
static ULONG frf_arm_pipe_busy = 0;
static ULONG frf_arm_pipe_stale_dropped = 0;
static UBYTE frf_arm_pipe_drop_next = 0;

/* V0.2.16 optional true native page flipping.  Default is OFF so the proven
 * V0.2.15 direct-front path remains byte-for-byte the normal behaviour. */
static UBYTE frf_dbuf_requested = 0;
static UBYTE frf_dbuf_active = 0;
static UBYTE frf_dbuf_attempted = 0;
static struct ScreenBuffer *frf_screen_buffers[2] = { NULL, NULL };
static struct MsgPort *frf_safe_port = NULL;
static struct MsgPort *frf_display_port = NULL;
static UBYTE frf_buffer_safe[2] = { 0, 0 };
static UBYTE frf_buffer_displayed[2] = { 0, 0 };
static unsigned frf_front_index = 0U;
static unsigned frf_back_index = 1U;
static UWORD *frf_dbuf_shadow1 = NULL;
static UBYTE frf_dbuf_force_full[2] = { 0, 0 };
static ULONG frf_dbuf_flips = 0;
static ULONG frf_dbuf_busy = 0;
static ULONG frf_dbuf_change_fail = 0;
static ULONG frf_dbuf_full_refresh = 0;
static ULONG frf_dbuf_gen_seq = 0;
static ULONG frf_dbuf_row_gen[FRF_PD_MAX_SRC_H];
static ULONG frf_dbuf_applied_gen[2][FRF_PD_MAX_SRC_H];

/* 16 absolute HAM base colours.  The remaining HAM6 codes modify B/R/G. */
static const UBYTE frf_ham_base_rgb[16][3] = {
    {  0,  0,  0}, {255,255,255}, { 85, 85, 85}, {170,170,170},
    {255,  0,  0}, {  0,255,  0}, {  0,  0,255}, {255,255,  0},
    {  0,255,255}, {255,  0,255}, {128,  0,  0}, {  0,128,  0},
    {  0,  0,128}, {128,128,  0}, {  0,128,128}, {128,  0,128}
};

static UBYTE q5to4[32];
static UBYTE q6to4[64];

static int env_wants_ham6(void)
{
#ifdef FRF_PD_FORCE_HAM6
    return 1;
#else
    const char *s = getenv("FRF_PD_VIDEO");
    if (!s || !*s)
        return 0;
    return !strcmp(s,"ham6") || !strcmp(s,"HAM6") ||
           !strcmp(s,"chip") || !strcmp(s,"CHIP") ||
           !strcmp(s,"directchip") || !strcmp(s,"DIRECTCHIP");
#endif
}

static ULONG weighted_error(unsigned r0,unsigned g0,unsigned b0,
                            unsigned r1,unsigned g1,unsigned b1)
{
    LONG dr=(LONG)r0-(LONG)r1;
    LONG dg=(LONG)g0-(LONG)g1;
    LONG db=(LONG)b0-(LONG)b1;
    return (ULONG)(3L*dr*dr + 4L*dg*dg + 2L*db*db);
}

static void build_palette_and_lookup(void)
{
    ULONG table[2 + 16*3];
    unsigned p, i;

    for(i=0;i<32U;i++) q5to4[i]=(UBYTE)((i*15U+15U)/31U);
    for(i=0;i<64U;i++) q6to4[i]=(UBYTE)((i*15U+31U)/63U);

    table[0]=(16UL<<16)|0UL;
    for(i=0;i<16U;i++) {
        table[1+i*3+0]=(ULONG)frf_ham_base_rgb[i][0]*0x01010101UL;
        table[1+i*3+1]=(ULONG)frf_ham_base_rgb[i][1]*0x01010101UL;
        table[1+i*3+2]=(ULONG)frf_ham_base_rgb[i][2]*0x01010101UL;
    }
    table[1+16*3]=0;
    LoadRGB32(&frf_screen->ViewPort,table);

    /* 64 KiB direct-code table. The hot HAM path then evaluates only four
     * candidates: direct palette colour, modify B, modify R, modify G. */
    for(p=0;p<65536U;p++) {
        unsigned tr=q5to4[(p>>11)&31U];
        unsigned tg=q6to4[(p>>5)&63U];
        unsigned tb=q5to4[p&31U];
        ULONG best=0xffffffffUL;
        UBYTE best_code=0;
        for(i=0;i<16U;i++) {
            ULONG e=weighted_error(tr,tg,tb,
                (unsigned)(frf_ham_base_rgb[i][0]>>4),
                (unsigned)(frf_ham_base_rgb[i][1]>>4),
                (unsigned)(frf_ham_base_rgb[i][2]>>4));
            if(e<best) { best=e; best_code=(UBYTE)i; }
        }
        frf_direct_pen[p]=best_code;
    }
}

static int bitmap_layout(struct BitMap *bm, ULONG *row_stride, UBYTE *interleaved)
{
    unsigned p;
    if(!bm || !row_stride || !interleaved) return 0;
    if(bm->Depth != 6 || bm->Rows < frf_screen_h) return 0;

    for(p=0;p<FRF_PD_PLANES;p++) {
        if(!bm->Planes[p]) return 0;
        if((TypeOfMem((APTR)bm->Planes[p]) & MEMF_CHIP)==0) return 0;
    }

    if((ULONG)bm->BytesPerRow == FRF_PD_PLANE_ROW) {
        *row_stride=FRF_PD_PLANE_ROW;
        *interleaved=0;
        return 1;
    }

    if((ULONG)bm->BytesPerRow == FRF_PD_PLANE_ROW*FRF_PD_PLANES) {
        UBYTE *base=(UBYTE *)bm->Planes[0];
        for(p=1;p<FRF_PD_PLANES;p++)
            if((UBYTE *)bm->Planes[p] != base+p*FRF_PD_PLANE_ROW)
                return 0;
        *row_stride=FRF_PD_PLANE_ROW*FRF_PD_PLANES;
        *interleaved=1;
        return 1;
    }
    return 0;
}

static void copy_word_run(UWORD *dst,const UWORD *src,unsigned words)
{
    if(words<=8U) {
        unsigned i;
        for(i=0;i<words;i++) dst[i]=src[i];
    } else {
        CopyMem((APTR)src,(APTR)dst,(ULONG)words*sizeof(UWORD));
    }
}

static void encode_pack_line(const UWORD *src,UWORD packed[FRF_PD_PLANES][FRF_PD_WORDS])
{
    unsigned word;
    unsigned pr=0U,pg=0U,pb=0U;

    for(word=0;word<FRF_PD_WORDS;word++) {
        UWORD pw0=0,pw1=0,pw2=0,pw3=0,pw4=0,pw5=0;
        unsigned bit;
        for(bit=0;bit<16U;bit++) {
            unsigned x=word*16U+bit;
            UWORD px=src[x];
            unsigned tr=q5to4[(px>>11)&31U];
            unsigned tg=q6to4[(px>>5)&63U];
            unsigned tb=q5to4[px&31U];
            UBYTE direct=frf_direct_pen[px];
            unsigned dr=(unsigned)(frf_ham_base_rgb[direct][0]>>4);
            unsigned dg=(unsigned)(frf_ham_base_rgb[direct][1]>>4);
            unsigned db=(unsigned)(frf_ham_base_rgb[direct][2]>>4);
            ULONG best=weighted_error(tr,tg,tb,dr,dg,db);
            UBYTE code=direct;
            unsigned br=dr,bg=dg,bb=db;
            UWORD mask=(UWORD)(0x8000U>>bit);

            /* 01bbbb = modify blue */
            {
                ULONG e=weighted_error(tr,tg,tb,pr,pg,tb);
                if(e<best) { best=e; code=(UBYTE)(0x10U|tb); br=pr; bg=pg; bb=tb; }
            }
            /* 10rrrr = modify red */
            {
                ULONG e=weighted_error(tr,tg,tb,tr,pg,pb);
                if(e<best) { best=e; code=(UBYTE)(0x20U|tr); br=tr; bg=pg; bb=pb; }
            }
            /* 11gggg = modify green */
            {
                ULONG e=weighted_error(tr,tg,tb,pr,tg,pb);
                if(e<best) { code=(UBYTE)(0x30U|tg); br=pr; bg=tg; bb=pb; }
            }

            if(code&0x01U) pw0|=mask;
            if(code&0x02U) pw1|=mask;
            if(code&0x04U) pw2|=mask;
            if(code&0x08U) pw3|=mask;
            if(code&0x10U) pw4|=mask;
            if(code&0x20U) pw5|=mask;
            pr=br; pg=bg; pb=bb;
        }
        packed[0][word]=pw0; packed[1][word]=pw1;
        packed[2][word]=pw2; packed[3][word]=pw3;
        packed[4][word]=pw4; packed[5][word]=pw5;
    }
}


static int arm_ham6_equivalence_selftest(void)
{
    UWORD line[FRF_PD_W];
    UWORD expected[FRF_PD_PLANES][FRF_PD_WORDS];
    const uint8_t *packed=NULL,*dirty=NULL;
    uint32_t changed=0;
    unsigned x,plane,word;

    for(x=0;x<FRF_PD_W;x++)
        line[x]=(UWORD)(((x&31U)<<11)|(((x*3U)&63U)<<5)|((x*7U)&31U));
    encode_pack_line(line,expected);
    if(!frfcp_ham6_encode_rgb565(line,FRF_PD_W,1U,FRF_PD_W*2U,
                                 &packed,&dirty,&changed))
        return 0;
    if(!packed || !dirty || dirty[0]!=1U || changed!=1U) {
        frfcp_ham6_disable("HAM6 equivalence dirty/result mismatch");
        return 0;
    }
    for(plane=0;plane<FRF_PD_PLANES;plane++) {
        const UBYTE *srcrow=packed+plane*FRF_PD_PLANE_ROW;
        for(word=0;word<FRF_PD_WORDS;word++) {
            UWORD got=(UWORD)(((UWORD)srcrow[word*2U]<<8)|srcrow[word*2U+1U]);
            if(got!=expected[plane][word]) {
                printf("[FRF PD HAM6] CPU3 equivalence FAIL plane=%u word=%u got=$%04x expected=$%04x\n",
                       plane,word,(unsigned)got,(unsigned)expected[plane][word]);
                frfcp_ham6_disable("HAM6 equivalence mismatch");
                return 0;
            }
        }
    }
    printf("[FRF PD HAM6] CPU3 equivalence PASS: 240 packed bytes exactly match 68K V0.2.3\n");
    return 1;
}

void frf_pd_ham6_set_double_buffer(int enable)
{
    if(!frf_started) frf_dbuf_requested=(UBYTE)(enable?1:0);
}

static void clear_bitmap(struct BitMap *bm)
{
    struct RastPort rp;
    if(!bm) return;
    InitRastPort(&rp);
    rp.BitMap=bm;
    SetRast(&rp,0);
    WaitBlit();
}

static UWORD *dbuf_shadow(unsigned index)
{
    return index?frf_dbuf_shadow1:frf_planar_shadow;
}

static void dbuf_drain_messages(void)
{
    struct Message *msg;
    int i;

    /* Proven Amiga ScreenBuffer ownership rule used in PocketSNESRGB:
     * dbi_SafeMessage for buffer N releases the PREVIOUS buffer N^1. */
    if(frf_safe_port) {
        while((msg=GetMsg(frf_safe_port))!=NULL) {
            for(i=0;i<2;i++) {
                if(frf_screen_buffers[i] && frf_screen_buffers[i]->sb_DBufInfo &&
                   msg==&frf_screen_buffers[i]->sb_DBufInfo->dbi_SafeMessage) {
                    frf_buffer_safe[i^1]=1;
                    break;
                }
            }
        }
    }

    /* DispMessage belongs to the buffer that has actually reached display. */
    if(frf_display_port) {
        while((msg=GetMsg(frf_display_port))!=NULL) {
            for(i=0;i<2;i++) {
                if(frf_screen_buffers[i] && frf_screen_buffers[i]->sb_DBufInfo &&
                   msg==&frf_screen_buffers[i]->sb_DBufInfo->dbi_DispMessage) {
                    frf_buffer_displayed[i]=1;
                    break;
                }
            }
        }
    }
}

static void dbuf_free_pair(void)
{
    int i;
    if(frf_screen) {
        WaitBlit();
        dbuf_drain_messages();
        for(i=1;i>=0;i--) {
            if(frf_screen_buffers[i]) {
                FreeScreenBuffer(frf_screen,frf_screen_buffers[i]);
                frf_screen_buffers[i]=NULL;
            }
        }
    } else {
        frf_screen_buffers[0]=frf_screen_buffers[1]=NULL;
    }
    if(frf_safe_port) { DeleteMsgPort(frf_safe_port); frf_safe_port=NULL; }
    if(frf_display_port) { DeleteMsgPort(frf_display_port); frf_display_port=NULL; }
    if(frf_dbuf_shadow1) { FreeVec(frf_dbuf_shadow1); frf_dbuf_shadow1=NULL; }
    frf_buffer_safe[0]=frf_buffer_safe[1]=0;
    frf_buffer_displayed[0]=frf_buffer_displayed[1]=0;
    frf_front_index=0U;
    frf_back_index=1U;
    frf_dbuf_active=0;
}

static int dbuf_restore_screen_bitmap(void)
{
    int tries;

    if(!frf_dbuf_active || !frf_screen || !frf_screen_buffers[0]) return 1;

    if(frf_front_index!=0U) {
        for(tries=0;tries<8;tries++) {
            dbuf_drain_messages();
            if(frf_buffer_safe[0] && frf_buffer_displayed[frf_front_index]) break;
            WaitTOF();
        }
        dbuf_drain_messages();
        WaitBlit();
        if(!frf_buffer_safe[0] || !frf_buffer_displayed[frf_front_index])
            return 0;
        if(!ChangeScreenBuffer(frf_screen,frf_screen_buffers[0]))
            return 0;
        frf_buffer_safe[0]=0;
        frf_buffer_displayed[0]=0;
        frf_front_index=0U;
        frf_back_index=1U;
        WaitTOF();
        WaitTOF();
        dbuf_drain_messages();
    }

    for(tries=0;tries<8;tries++) {
        dbuf_drain_messages();
        if(frf_buffer_safe[1] && frf_buffer_displayed[0]) break;
        WaitTOF();
    }
    dbuf_drain_messages();
    return frf_buffer_safe[1] && frf_buffer_displayed[0];
}

static int dbuf_disable_to_single(const char *reason)
{
    if(!frf_dbuf_active) return 1;
    if(!dbuf_restore_screen_bitmap()) {
        printf("[FRF PD DBUF] cannot safely leave page-flip mode yet (%s); keeping buffers owned\n",
               reason?reason:"unknown");
        return 0;
    }
    dbuf_free_pair();
    frf_bm=frf_screen?frf_screen->RastPort.BitMap:NULL;
    frf_row_stride=0;
    frf_interleaved=0;
    if(frf_bm && !bitmap_layout(frf_bm,&frf_row_stride,&frf_interleaved)) {
        printf("[FRF PD DBUF] original screen bitmap layout became invalid\n");
        return 0;
    }
    frf_prev_valid=0;
    printf("[FRF PD DBUF] disabled -> proven single displayed-bitmap presenter (%s)\n",
           reason?reason:"fallback");
    return 1;
}

static int dbuf_try_enable(void)
{
    ULONG row0=0,row1=0;

    frf_dbuf_attempted=1;
    UBYTE inter0=0,inter1=0;
    ULONG chip_before,chip_after;
    int i;

    if(!frf_dbuf_requested) return 0;
    if(!frf_arm_ham6) {
        printf("[FRF PD DBUF] requested but CPU3 HAM6 is inactive; keeping proven single-buffer path\n");
        return 0;
    }

    chip_before=AvailMem(MEMF_CHIP);
    frf_safe_port=CreateMsgPort();
    frf_display_port=CreateMsgPort();
    frf_screen_buffers[0]=AllocScreenBuffer(frf_screen,NULL,SB_SCREEN_BITMAP);
    frf_screen_buffers[1]=AllocScreenBuffer(frf_screen,NULL,0);
    if(!frf_safe_port || !frf_display_port ||
       !frf_screen_buffers[0] || !frf_screen_buffers[1]) {
        printf("[FRF PD DBUF] allocation unavailable; keeping proven single-buffer path\n");
        dbuf_free_pair();
        return 0;
    }

    if(!bitmap_layout(frf_screen_buffers[0]->sb_BitMap,&row0,&inter0) ||
       !bitmap_layout(frf_screen_buffers[1]->sb_BitMap,&row1,&inter1) ||
       row0!=row1 || inter0!=inter1) {
        printf("[FRF PD DBUF] ScreenBuffer bitmap layout mismatch; keeping proven single-buffer path\n");
        dbuf_free_pair();
        return 0;
    }

    frf_dbuf_shadow1=(UWORD *)AllocVec(FRF_PD_SHADOW_WORDS*sizeof(UWORD),MEMF_FAST|MEMF_CLEAR);
    if(!frf_dbuf_shadow1)
        frf_dbuf_shadow1=(UWORD *)AllocVec(FRF_PD_SHADOW_WORDS*sizeof(UWORD),MEMF_PUBLIC|MEMF_CLEAR);
    if(!frf_dbuf_shadow1) {
        printf("[FRF PD DBUF] second FAST shadow allocation failed; keeping proven single-buffer path\n");
        dbuf_free_pair();
        return 0;
    }

    for(i=0;i<2;i++) {
        frf_screen_buffers[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort=frf_safe_port;
        frf_screen_buffers[i]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort=frf_display_port;
        clear_bitmap(frf_screen_buffers[i]->sb_BitMap);
    }
    memset(frf_planar_shadow,0,FRF_PD_SHADOW_WORDS*sizeof(UWORD));
    memset(frf_dbuf_shadow1,0,FRF_PD_SHADOW_WORDS*sizeof(UWORD));
    memset(frf_dbuf_row_gen,0,sizeof(frf_dbuf_row_gen));
    memset(frf_dbuf_applied_gen,0,sizeof(frf_dbuf_applied_gen));
    frf_dbuf_gen_seq=0UL;

    frf_front_index=0U;
    frf_back_index=1U;
    frf_buffer_safe[0]=0;
    frf_buffer_safe[1]=1;
    frf_buffer_displayed[0]=1;
    frf_buffer_displayed[1]=0;
    /* Each CHIP page starts cleared and therefore has no relationship to the
     * persistent CPU3 packed output. Force the first use of EACH page to copy
     * every source row. This also makes later geometry resets exact. */
    frf_dbuf_force_full[0]=frf_dbuf_force_full[1]=1;
    frf_row_stride=row0;
    frf_interleaved=inter0;
    frf_bm=frf_screen_buffers[1]->sb_BitMap;
    frf_dbuf_active=1;
    chip_after=AvailMem(MEMF_CHIP);

    printf("[FRF PD DBUF] ACTIVE true ScreenBuffer page flip front=0 back=1 chip_cost=%lu chip_free=%lu\n",
           (unsigned long)(chip_before-chip_after),(unsigned long)chip_after);
    printf("[FRF PD DBUF] SafeMessage N releases N^1; DispMessage gates front ownership; no writes to visible bitmap\n");
    return 1;
}

static void dbuf_note_cpu3_changes(const uint8_t *dirty,unsigned height)
{
    unsigned y;
    if(!frf_dbuf_active || !dirty) return;
    ++frf_dbuf_gen_seq;
    if(frf_dbuf_gen_seq==0UL) {
        memset(frf_dbuf_row_gen,0,sizeof(frf_dbuf_row_gen));
        memset(frf_dbuf_applied_gen,0,sizeof(frf_dbuf_applied_gen));
        frf_dbuf_gen_seq=1UL;
    }
    for(y=0;y<height;y++)
        if(dirty[y]) frf_dbuf_row_gen[y]=frf_dbuf_gen_seq;
}

static int dbuf_publish_cpu3_frame(const uint8_t *packed,unsigned height,
                                   ULONG *frame_bytes,ULONG *frame_runs)
{
    unsigned y,plane,dy,dest_y;
    struct BitMap *bm;
    UWORD *base_shadow;
    int any_line;
    int force_all=0;

    if(!frf_dbuf_active || !packed || !frame_bytes || !frame_runs) return 0;
    dbuf_drain_messages();
    if(!frf_buffer_safe[frf_back_index] ||
       !frf_buffer_displayed[frf_front_index]) {
        ++frf_dbuf_busy;
        return 0;
    }

    bm=frf_screen_buffers[frf_back_index]->sb_BitMap;
    base_shadow=dbuf_shadow(frf_back_index);
    if(!bm || !base_shadow) return 0;

    if(frf_dbuf_force_full[frf_back_index]) {
        clear_bitmap(bm);
        memset(base_shadow,0,FRF_PD_SHADOW_WORDS*sizeof(UWORD));
        memset(frf_dbuf_applied_gen[frf_back_index],0,
               sizeof(frf_dbuf_applied_gen[frf_back_index]));
        frf_dbuf_force_full[frf_back_index]=0;
        force_all=1;
        ++frf_dbuf_full_refresh;
    }

    dest_y=(frf_screen_h-height)/2U;
    for(y=0;y<height;y++) {
        /* A cleared/recently re-geometrized back page must be repopulated from
         * the persistent packed CPU3 image even for rows ARM considered
         * unchanged this frame. Generation equality is only valid after that
         * page has received one complete image for the current geometry. */
        if(!force_all &&
           frf_dbuf_applied_gen[frf_back_index][y]==frf_dbuf_row_gen[y]) {
            ++frf_reused_lines;
            continue;
        }
        any_line=0;
        dy=dest_y+y;
        for(plane=0;plane<FRF_PD_PLANES;plane++) {
            const UBYTE *srcrow=packed+
                y*(FRF_PD_PLANES*FRF_PD_PLANE_ROW)+plane*FRF_PD_PLANE_ROW;
            UWORD *shadow=base_shadow+
                plane*(FRF_PD_H*FRF_PD_WORDS)+dy*FRF_PD_WORDS;
            UWORD *dst=(UWORD *)((UBYTE *)bm->Planes[plane]+dy*frf_row_stride);
            int run=-1;
            unsigned i;
            for(i=0;i<FRF_PD_WORDS;i++) {
                UWORD v=(UWORD)(((UWORD)srcrow[i*2U]<<8)|srcrow[i*2U+1U]);
                if(v!=shadow[i]) {
                    if(run<0) run=(int)i;
                    shadow[i]=v;
                    any_line=1;
                } else if(run>=0) {
                    unsigned words=i-(unsigned)run;
                    copy_word_run(dst+run,shadow+run,words);
                    *frame_bytes+=(ULONG)words*2UL;
                    ++(*frame_runs);
                    run=-1;
                }
            }
            if(run>=0) {
                unsigned words=FRF_PD_WORDS-(unsigned)run;
                copy_word_run(dst+run,shadow+run,words);
                *frame_bytes+=(ULONG)words*2UL;
                ++(*frame_runs);
            }
        }
        if(any_line) ++frf_changed_lines;
        else ++frf_reused_lines;
        frf_dbuf_applied_gen[frf_back_index][y]=frf_dbuf_row_gen[y];
    }

    WaitBlit();
    if(ChangeScreenBuffer(frf_screen,frf_screen_buffers[frf_back_index])) {
        unsigned old_front=frf_front_index;
        unsigned new_front=frf_back_index;
        frf_buffer_safe[new_front]=0;
        frf_buffer_displayed[new_front]=0;
        frf_front_index=new_front;
        frf_back_index=old_front;
        frf_bm=frf_screen_buffers[frf_back_index]->sb_BitMap;
        ++frf_dbuf_flips;
        return 1;
    }

    ++frf_dbuf_change_fail;
    return 0;
}

static void clear_native(void)
{
    if(frf_dbuf_active) {
        clear_bitmap(frf_screen_buffers[0]?frf_screen_buffers[0]->sb_BitMap:NULL);
        clear_bitmap(frf_screen_buffers[1]?frf_screen_buffers[1]->sb_BitMap:NULL);
        if(frf_planar_shadow)
            memset(frf_planar_shadow,0,FRF_PD_SHADOW_WORDS*sizeof(UWORD));
        if(frf_dbuf_shadow1)
            memset(frf_dbuf_shadow1,0,FRF_PD_SHADOW_WORDS*sizeof(UWORD));
    } else {
        clear_bitmap(frf_bm);
        if(frf_planar_shadow)
            memset(frf_planar_shadow,0,FRF_PD_SHADOW_WORDS*sizeof(UWORD));
    }
    if(frf_prev_rgb565)
        memset(frf_prev_rgb565,0,FRF_PD_PREV_PIXELS*sizeof(UWORD));
    frf_prev_valid=0;
}

static ULONG choose_ham_mode(ULONG monitor_id,unsigned screen_h)
{
    ULONG mode=monitor_id|HAM_KEY;
    if(ModeNotAvailable(mode)==0) return mode;
    return BestModeID(BIDTAG_NominalWidth,320,
                      BIDTAG_NominalHeight,screen_h,
                      BIDTAG_DesiredWidth,320,
                      BIDTAG_DesiredHeight,screen_h,
                      BIDTAG_Depth,6,
                      BIDTAG_DIPFMustHave,DIPF_IS_HAM,
                      BIDTAG_DIPFMustNotHave,DIPF_IS_LACE,
                      BIDTAG_MonitorID,monitor_id,
                      TAG_DONE);
}

int frf_pd_ham6_init(void)
{
    return frf_pd_ham6_init_for_fps(50.0);
}

int frf_pd_ham6_init_for_fps(double fps)
{
    ULONG mode, row=0;
    UBYTE inter=0;
    const char *standard;

    if(frf_started) return frf_screen != NULL;
    frf_started=1;
    if(!env_wants_ham6()) return 0;

    if(!SysBase) SysBase=*(struct ExecBase **)4;
    if(!IntuitionBase) {
        IntuitionBase=(struct IntuitionBase *)OpenLibrary("intuition.library",39);
        if(IntuitionBase) frf_own_intuition=1;
    }
    if(!GfxBase) {
        GfxBase=(struct GfxBase *)OpenLibrary("graphics.library",39);
        if(GfxBase) frf_own_graphics=1;
    }
    if(!IntuitionBase || !GfxBase) {
        printf("[FRF PD HAM6] native libraries unavailable\n");
        frf_pd_ham6_shutdown();
        return 0;
    }

    /* Match the physical display cadence to the core whenever possible.
     * Mega Drive NTSC content is ~60 Hz; forcing it through a PAL 50 Hz
     * WaitTOF clock caused V0.2.1's slow/catch-up/fast oscillation. */
    if(fps>=55.0) {
        frf_monitor_id=NTSC_MONITOR_ID;
        frf_refresh_hz=60U;
        frf_screen_h=240U;
        standard="NTSC";
    } else {
        frf_monitor_id=PAL_MONITOR_ID;
        frf_refresh_hz=50U;
        frf_screen_h=256U;
        standard="PAL";
    }

    mode=choose_ham_mode(frf_monitor_id,frf_screen_h);

    /* A PAL-only machine/display still gets a usable fallback. The host will
     * then use a deterministic 60->50 presentation ratio instead of letting
     * audio-buffer AUTO frameskip become the pacing clock. */
    if(mode==INVALID_ID && frf_refresh_hz==60U) {
        printf("[FRF PD HAM6] native NTSC HAM6 unavailable; falling back to PAL 50 Hz\n");
        frf_monitor_id=PAL_MONITOR_ID;
        frf_refresh_hz=50U;
        frf_screen_h=256U;
        standard="PAL-FALLBACK";
        mode=choose_ham_mode(frf_monitor_id,frf_screen_h);
    }

    if(mode==INVALID_ID) {
        printf("[FRF PD HAM6] no native HAM6 mode available\n");
        frf_pd_ham6_shutdown();
        return 0;
    }

    printf("[FRF PD HAM6] opening %s HAM6 mode=%08lx refresh=%uHz height=%u chip_free=%lu largest=%lu\n",
           standard,(unsigned long)mode,frf_refresh_hz,frf_screen_h,
           (unsigned long)AvailMem(MEMF_CHIP),
           (unsigned long)AvailMem(MEMF_CHIP|MEMF_LARGEST));

    frf_screen=OpenScreenTags(NULL,
        SA_DisplayID,mode,
        SA_Width,320,
        SA_Height,frf_screen_h,
        SA_Depth,6,
        SA_Overscan,(frf_refresh_hz==60U)?OSCAN_MAX:OSCAN_STANDARD,
        SA_AutoScroll,FALSE,
        SA_ShowTitle,FALSE,
        SA_Quiet,TRUE,
        SA_Behind,FALSE,
        SA_Type,CUSTOMSCREEN,
        SA_Interleaved,TRUE,
        TAG_DONE);
    if(!frf_screen) {
        printf("[FRF PD HAM6] OpenScreenTags failed mode=%08lx refresh=%uHz height=%u chip_free=%lu largest=%lu\n",
               (unsigned long)mode,frf_refresh_hz,frf_screen_h,
               (unsigned long)AvailMem(MEMF_CHIP),
               (unsigned long)AvailMem(MEMF_CHIP|MEMF_LARGEST));
        frf_pd_ham6_shutdown();
        return 0;
    }

    frf_bm=frf_screen->RastPort.BitMap;
    if(!bitmap_layout(frf_bm,&row,&inter)) {
        printf("[FRF PD HAM6] REFUSED: displayed bitmap is not a supported 6-plane CHIP layout\n");
        frf_pd_ham6_shutdown();
        return 0;
    }
    frf_row_stride=row;
    frf_interleaved=inter;

    frf_planar_shadow=(UWORD *)AllocVec(FRF_PD_SHADOW_WORDS*sizeof(UWORD),MEMF_FAST|MEMF_CLEAR);
    if(!frf_planar_shadow)
        frf_planar_shadow=(UWORD *)AllocVec(FRF_PD_SHADOW_WORDS*sizeof(UWORD),MEMF_PUBLIC|MEMF_CLEAR);
    frf_prev_rgb565=(UWORD *)AllocVec(FRF_PD_PREV_PIXELS*sizeof(UWORD),MEMF_FAST|MEMF_CLEAR);
    if(!frf_prev_rgb565)
        frf_prev_rgb565=(UWORD *)AllocVec(FRF_PD_PREV_PIXELS*sizeof(UWORD),MEMF_PUBLIC|MEMF_CLEAR);
    frf_direct_pen=(UBYTE *)AllocVec(65536UL,MEMF_FAST);
    if(!frf_direct_pen)
        frf_direct_pen=(UBYTE *)AllocVec(65536UL,MEMF_PUBLIC);
    if(!frf_planar_shadow || !frf_prev_rgb565 || !frf_direct_pen) {
        printf("[FRF PD HAM6] Fast/Public RAM allocation failed\n");
        frf_pd_ham6_shutdown();
        return 0;
    }

    frf_arm_pipe_submitted=0;
    frf_arm_pipe_presented=0;
    frf_arm_pipe_busy=0;
    frf_arm_pipe_stale_dropped=0;
    frf_arm_pipe_drop_next=0;
    build_palette_and_lookup();
    frf_arm_ham6=(UBYTE)(frfcp_available() && frfcp_ham6_prepare((const uint8_t *)frf_direct_pen));
    if(frf_arm_ham6) {
        if(!arm_ham6_equivalence_selftest()) {
            frf_arm_ham6=0;
            printf("[FRF PD HAM6] CPU3 equivalence gate failed; exact 68K path retained\n");
        } else {
            /* Reset ARM previous/output state after the synthetic line so the
             * first real frame starts from a clean authoritative baseline. */
            frf_arm_ham6=(UBYTE)frfcp_ham6_prepare((const uint8_t *)frf_direct_pen);
            if(frf_arm_ham6)
                printf("[FRF PD HAM6] HAM6 accelerator=CPU3 PIPELINED; one-frame overlap enabled; 68K dirty-UWORD CHIP writes retained\n");
        }
    }
    /* V0.2.16A: defer optional ScreenBuffer ports/buffer until the first
     * video callback.  main() has completed frf_pd_input_init() by then, so
     * the classic gameport fallback keeps the proven V0.2.15 acquisition order. */
    clear_native();
    ScreenToFront(frf_screen);
    MakeScreen(frf_screen);
    RethinkDisplay();

    printf("[FRF PD HAM6] DIRECT CHIP ACTIVE 320x%u depth=6 refresh=%uHz rowstride=%lu layout=%s\n",
           frf_screen_h,frf_refresh_hz,(unsigned long)frf_row_stride, frf_interleaved?"interleaved":"planar");
    if(frf_arm_ham6 && frf_dbuf_active)
        printf("[FRF PD HAM6] RGB565 -> async CPU3 HAM6 -> safe BACK bitmap -> ChangeScreenBuffer page flip -> physical RGB\n");
    else if(frf_arm_ham6)
        printf("[FRF PD HAM6] RGB565 -> async FRF CPU3 HAM6 planar -> next-frame overlap -> dirty UWORD CHIP runs -> physical RGB\n");
    else
        printf("[FRF PD HAM6] RGB565 -> 68K fused HAM6 -> dirty UWORD CHIP runs -> physical RGB\n");
    return 1;
}

void frf_pd_ham6_mark_pipeline_stale(void)
{
    if(frf_arm_ham6) frf_arm_pipe_drop_next=1;
}

unsigned frf_pd_ham6_refresh_hz(void)
{
    return frf_refresh_hz;
}

int frf_pd_ham6_present_rgb565(const void *pixels,unsigned width,unsigned height,unsigned pitch_bytes)
{
    unsigned y, dest_y, xoff;
    ULONG frame_bytes=0, frame_runs=0;
    UWORD line[FRF_PD_W];
    const uint8_t *arm_packed=NULL,*arm_dirty=NULL;
    uint32_t arm_changed=0;
    unsigned arm_h=0;
    int poll_state=FRFCP_HAM6_POLL_IDLE;
    int geometry_wait_busy=0;
    int geometry_changed=0;

    if(!pixels || width==0 || height==0 || width>FRF_PD_W || height>FRF_PD_MAX_SRC_H)
        return 0;
    if(!frf_started && !frf_pd_ham6_init()) return 0;
    if(!frf_screen) return 0;
    if(pitch_bytes < width*2U) return 0;
    if(height>frf_screen_h) return 0;

    /* V0.2.16C: DBUF activation remains deferred until the first video callback.
     * V0.2.16B now claims input before even the base HAM6 screen is opened. */
    if(frf_dbuf_requested && !frf_dbuf_active && !frf_dbuf_attempted)
        (void)dbuf_try_enable();

    /* V0.2.16C DYNAMIC-GEOMETRY REPAIR
     *
     * H32/H40 and 224/240 transitions can happen at title/loading boundaries.
     * V0.2.16 previously disabled CPU3 if the old-geometry job was still
     * pending, but left DBUF active. The 68K fallback then wrote the hidden
     * back bitmap without ChangeScreenBuffer(), freezing the visible loading
     * screen while emulation/audio continued.
     *
     * Consume an old-geometry job non-blockingly instead. If it is still busy,
     * skip only this presentation callback. When it completes, discard that
     * stale packed frame, reset both CHIP pages, and submit the new geometry. */
    geometry_changed=(width!=frf_prev_w || height!=frf_prev_h) && frf_prev_w!=0U;
    if(geometry_changed && frf_arm_ham6 && frfcp_ham6_async_pending()) {
        poll_state=frfcp_ham6_async_poll(&arm_packed,&arm_dirty,&arm_changed,
                                         NULL,&arm_h);
        if(poll_state==FRFCP_HAM6_POLL_BUSY) {
            ++frf_arm_pipe_busy;
            geometry_wait_busy=1;
        } else if(poll_state==FRFCP_HAM6_POLL_DONE) {
            /* Never display an old-layout result on the new layout. */
            ++frf_arm_pipe_stale_dropped;
            arm_packed=NULL;
            arm_dirty=NULL;
            arm_changed=0;
            arm_h=0;
            poll_state=FRFCP_HAM6_POLL_IDLE;
        } else if(poll_state==FRFCP_HAM6_POLL_ERROR) {
            ++frf_arm_fallbacks;
            frf_arm_ham6=0;
            frf_prev_valid=0;
            if(frf_dbuf_active && !dbuf_disable_to_single("geometry-transition CPU3 error"))
                return 1;
            printf("[FRF PD HAM6] geometry-transition CPU3 job failed; exact 68K fallback retained (%s)\n",
                   frfcp_last_error());
        }
    }

    if(!geometry_wait_busy && (width!=frf_prev_w || height!=frf_prev_h)) {
        if(frf_dbuf_active) {
            frf_dbuf_force_full[0]=1;
            frf_dbuf_force_full[1]=1;
            memset(frf_dbuf_row_gen,0,sizeof(frf_dbuf_row_gen));
            memset(frf_dbuf_applied_gen,0,sizeof(frf_dbuf_applied_gen));
            frf_dbuf_gen_seq=0UL;
            frf_prev_valid=0;
        } else {
            clear_native();
        }
        frf_prev_w=width;
        frf_prev_h=height;
    }

    if(frf_arm_ham6 && geometry_wait_busy) {
        /* Old-layout CPU3 work is still running. Do not fall through to the
         * 68K writer while DBUF owns the display pages; simply keep the current
         * front page visible and let emulation/audio advance. */
        ++frf_frames;
        if((frf_frames%250UL)==0) {
            printf("[FRF PD HAM6] geometry wait: old CPU3 frame still busy; current front page retained\n");
        }
        return 1;
    }

    if(frf_arm_ham6 && !geometry_wait_busy) {
        /* First consume frame N-1 without waiting. Its shared output remains
         * stable until we submit frame N below. A geometry-transition poll may
         * already have consumed and discarded the stale old-layout job. */
        poll_state=frfcp_ham6_async_poll(&arm_packed,&arm_dirty,&arm_changed,
                                         NULL,&arm_h);
        if(poll_state==FRFCP_HAM6_POLL_DONE) {
            unsigned done_dest_y=(frf_screen_h-arm_h)/2U;
            (void)arm_changed;
            if(frf_dbuf_active) dbuf_note_cpu3_changes(arm_dirty,arm_h);
            if(frf_arm_pipe_drop_next) {
                /* Audio recovery can intentionally discard an already-finished
                 * frame. Consume it without touching either display buffer. */
                frf_arm_pipe_drop_next=0;
                ++frf_arm_pipe_stale_dropped;
            } else if(frf_dbuf_active) {
                /* CPU3 output is persistent: unchanged rows retain current
                 * packed data in shared RAM. Per-row generations accumulate the
                 * union of ARM dirty rows since each back buffer was last used,
                 * so a two-frames-old back buffer is brought exactly current
                 * without scanning/copying every row. */
                if(dbuf_publish_cpu3_frame(arm_packed,arm_h,&frame_bytes,&frame_runs))
                    ++frf_arm_pipe_presented;
            } else {
                ++frf_arm_pipe_presented;
                for(y=0;y<arm_h;y++) {
                    unsigned plane,dy=done_dest_y+y;
                    if(!arm_dirty[y]) {
                        frf_reused_lines++;
                        continue;
                    }
                    frf_changed_lines++;
                    for(plane=0;plane<FRF_PD_PLANES;plane++) {
                        const UBYTE *srcrow=arm_packed+
                            y*(FRF_PD_PLANES*FRF_PD_PLANE_ROW)+plane*FRF_PD_PLANE_ROW;
                        UWORD *shadow=frf_planar_shadow+
                            plane*(FRF_PD_H*FRF_PD_WORDS)+dy*FRF_PD_WORDS;
                        UWORD *dst=(UWORD *)((UBYTE *)frf_bm->Planes[plane]+dy*frf_row_stride);
                        int run=-1;
                        unsigned i;
                        for(i=0;i<FRF_PD_WORDS;i++) {
                            UWORD v=(UWORD)(((UWORD)srcrow[i*2U]<<8)|srcrow[i*2U+1U]);
                            if(v!=shadow[i]) {
                                if(run<0) run=(int)i;
                                shadow[i]=v;
                            } else if(run>=0) {
                                unsigned words=i-(unsigned)run;
                                copy_word_run(dst+run,shadow+run,words);
                                frame_bytes+=(ULONG)words*2UL;
                                frame_runs++;
                                run=-1;
                            }
                        }
                        if(run>=0) {
                            unsigned words=FRF_PD_WORDS-(unsigned)run;
                            copy_word_run(dst+run,shadow+run,words);
                            frame_bytes+=(ULONG)words*2UL;
                            frame_runs++;
                        }
                    }
                }
            }
            frf_prev_valid=0;
        } else if(poll_state==FRFCP_HAM6_POLL_BUSY) {
            /* Never spin here. Leave the currently displayed frame intact and
             * let emulation continue. The pending shared input still belongs
             * to CPU3, so this source frame is presentation-skipped. */
            ++frf_arm_pipe_busy;
        } else if(poll_state==FRFCP_HAM6_POLL_ERROR) {
            ++frf_arm_fallbacks;
            frf_arm_ham6=0;
            frf_prev_valid=0;
            if(frf_dbuf_active && !dbuf_disable_to_single("CPU3 pipeline error"))
                return 1;
            printf("[FRF PD HAM6] CPU3 pipeline failed; continuing exact 68K HAM6 (%s)\n",
                   frfcp_last_error());
        }

        /* Only reuse the shared input after the previous job is complete.
         * If poll reported BUSY there is deliberately no wait and no submit. */
        if(frf_arm_ham6 && !frfcp_ham6_async_pending()) {
            int sr=frfcp_ham6_async_submit_rgb565(pixels,width,height,pitch_bytes);
            if(sr>0) {
                ++frf_arm_pipe_submitted;
            } else if(sr<0) {
                ++frf_arm_fallbacks;
                frf_arm_ham6=0;
                frf_prev_valid=0;
                if(frf_dbuf_active && !dbuf_disable_to_single("CPU3 submit error"))
                    return 1;
                printf("[FRF PD HAM6] CPU3 pipeline submit failed; continuing exact 68K HAM6 (%s)\n",
                       frfcp_last_error());
            }
        }

        /* Warm-up: the first call submits frame 0 and returns immediately.
         * We intentionally do not duplicate the expensive 68K conversion. */
        if(frf_arm_ham6) {
            frf_frames++;
            frf_chip_bytes+=frame_bytes;
            frf_runs+=frame_runs;
            if((frf_frames%250UL)==0) {
                printf("[FRF PD HAM6] frames=%lu last_chip=%lu avg_chip=%lu runs=%lu changed=%lu reused=%lu path=%s submitted=%lu presented=%lu busy=%lu stale_drop=%lu dbuf_flips=%lu dbuf_busy=%lu dbuf_fail=%lu arm_fallbacks=%lu\n",
                    (unsigned long)frf_frames,
                    (unsigned long)frame_bytes,
                    (unsigned long)(frf_chip_bytes/frf_frames),
                    (unsigned long)frf_runs,
                    (unsigned long)frf_changed_lines,
                    (unsigned long)frf_reused_lines,
                    frf_dbuf_active?"CPU3-DBUF":"CPU3-PIPE",
                    (unsigned long)frf_arm_pipe_submitted,
                    (unsigned long)frf_arm_pipe_presented,
                    (unsigned long)frf_arm_pipe_busy,
                    (unsigned long)frf_arm_pipe_stale_dropped,
                    (unsigned long)frf_dbuf_flips,
                    (unsigned long)frf_dbuf_busy,
                    (unsigned long)frf_dbuf_change_fail,
                    (unsigned long)frf_arm_fallbacks);
            }
            return 1;
        }
    }

    /* Exact V0.2.3 68K fallback. This path is unchanged in algorithm and CHIP
     * publication semantics; it runs only when FRF is disabled/unavailable or
     * the async pipeline has been shut down. */
    dest_y=(frf_screen_h-height)/2U;
    xoff=(FRF_PD_W-width)/2U;
    for(y=0;y<height;y++) {
        const UBYTE *srcb=(const UBYTE *)pixels + y*pitch_bytes;
        UWORD *prev=frf_prev_rgb565 + y*FRF_PD_W;
        UWORD packed[FRF_PD_PLANES][FRF_PD_WORDS];
        unsigned plane, dy=dest_y+y;

        memset(line,0,sizeof(line));
        CopyMem((APTR)srcb,(APTR)(line+xoff),(ULONG)width*2UL);

        if(frf_prev_valid && memcmp(line,prev,sizeof(line))==0) {
            frf_reused_lines++;
            continue;
        }
        frf_changed_lines++;
        encode_pack_line(line,packed);

        for(plane=0;plane<FRF_PD_PLANES;plane++) {
            UWORD *shadow=frf_planar_shadow+
                plane*(FRF_PD_H*FRF_PD_WORDS)+dy*FRF_PD_WORDS;
            UWORD *dst=(UWORD *)((UBYTE *)frf_bm->Planes[plane]+dy*frf_row_stride);
            int run=-1;
            unsigned i;
            for(i=0;i<FRF_PD_WORDS;i++) {
                if(packed[plane][i]!=shadow[i]) {
                    if(run<0) run=(int)i;
                    shadow[i]=packed[plane][i];
                } else if(run>=0) {
                    unsigned words=i-(unsigned)run;
                    copy_word_run(dst+run,shadow+run,words);
                    frame_bytes+=(ULONG)words*2UL;
                    frame_runs++;
                    run=-1;
                }
            }
            if(run>=0) {
                unsigned words=FRF_PD_WORDS-(unsigned)run;
                copy_word_run(dst+run,shadow+run,words);
                frame_bytes+=(ULONG)words*2UL;
                frame_runs++;
            }
        }
        CopyMem((APTR)line,(APTR)prev,sizeof(line));
    }
    frf_prev_valid=1;

    frf_frames++;
    frf_chip_bytes+=frame_bytes;
    frf_runs+=frame_runs;
    if((frf_frames%250UL)==0) {
        printf("[FRF PD HAM6] frames=%lu last_chip=%lu avg_chip=%lu runs=%lu changed=%lu reused=%lu path=68K arm_fallbacks=%lu\n",
            (unsigned long)frf_frames,
            (unsigned long)frame_bytes,
            (unsigned long)(frf_chip_bytes/frf_frames),
            (unsigned long)frf_runs,
            (unsigned long)frf_changed_lines,
            (unsigned long)frf_reused_lines,
            (unsigned long)frf_arm_fallbacks);
    }
    return 1;
}

void frf_pd_ham6_wait_vblank(void)
{
    if(frf_screen && GfxBase) WaitTOF();
}

int frf_pd_ham6_active(void) { return frf_screen != NULL; }

void frf_pd_ham6_shutdown(void)
{
    if(frf_frames) {
        printf("[FRF PD HAM6] shutdown frames=%lu chipbytes=%lu avg=%lu runs=%lu cpu3_submitted=%lu cpu3_presented=%lu cpu3_busy=%lu cpu3_stale_drop=%lu dbuf=%u flips=%lu busy=%lu change_fail=%lu full_refresh=%lu\n",
            (unsigned long)frf_frames,
            (unsigned long)frf_chip_bytes,
            (unsigned long)(frf_chip_bytes/frf_frames),
            (unsigned long)frf_runs,
            (unsigned long)frf_arm_pipe_submitted,
            (unsigned long)frf_arm_pipe_presented,
            (unsigned long)frf_arm_pipe_busy,
            (unsigned long)frf_arm_pipe_stale_dropped,
            (unsigned)frf_dbuf_active,
            (unsigned long)frf_dbuf_flips,
            (unsigned long)frf_dbuf_busy,
            (unsigned long)frf_dbuf_change_fail,
            (unsigned long)frf_dbuf_full_refresh);
    }
    if(frf_dbuf_active) {
        if(!dbuf_restore_screen_bitmap())
            printf("[FRF PD DBUF] WARNING: final ownership restore timed out; forcing orderly ScreenBuffer teardown\n");
        dbuf_free_pair();
        frf_bm=frf_screen?frf_screen->RastPort.BitMap:NULL;
    }
    if(frf_screen) {
        ScreenToBack(frf_screen);
        CloseScreen(frf_screen);
        frf_screen=NULL;
        if(IntuitionBase) RethinkDisplay();
    }
    frf_bm=NULL;
    if(frf_direct_pen) { FreeVec(frf_direct_pen); frf_direct_pen=NULL; }
    if(frf_prev_rgb565) { FreeVec(frf_prev_rgb565); frf_prev_rgb565=NULL; }
    if(frf_planar_shadow) { FreeVec(frf_planar_shadow); frf_planar_shadow=NULL; }
    if(frf_own_graphics && GfxBase) { CloseLibrary((struct Library *)GfxBase); GfxBase=NULL; }
    if(frf_own_intuition && IntuitionBase) { CloseLibrary((struct Library *)IntuitionBase); IntuitionBase=NULL; }
    frf_own_graphics=0; frf_own_intuition=0;
    frf_started=0; frf_prev_valid=0; frf_frames=0; frf_chip_bytes=0; frf_runs=0;
    frf_arm_ham6=0; frf_arm_fallbacks=0;
    frf_arm_pipe_submitted=0; frf_arm_pipe_presented=0; frf_arm_pipe_busy=0;
    frf_arm_pipe_stale_dropped=0; frf_arm_pipe_drop_next=0;
    frf_dbuf_active=0; frf_dbuf_attempted=0; frf_dbuf_flips=0; frf_dbuf_busy=0;
    frf_dbuf_change_fail=0; frf_dbuf_full_refresh=0; frf_dbuf_gen_seq=0;
    memset(frf_dbuf_row_gen,0,sizeof(frf_dbuf_row_gen));
    memset(frf_dbuf_applied_gen,0,sizeof(frf_dbuf_applied_gen));
    frf_dbuf_force_full[0]=frf_dbuf_force_full[1]=0;
    frf_screen_h=256U; frf_refresh_hz=50U; frf_monitor_id=PAL_MONITOR_ID;
}

#else /* !FRF_PD_AMIGA */

void frf_pd_ham6_set_double_buffer(int enable) { (void)enable; }
int frf_pd_ham6_init(void) { return 0; }
int frf_pd_ham6_init_for_fps(double fps) { (void)fps; return 0; }
unsigned frf_pd_ham6_refresh_hz(void) { return 50U; }
void frf_pd_ham6_mark_pipeline_stale(void) { }
int frf_pd_ham6_present_rgb565(const void *p,unsigned w,unsigned h,unsigned pitch)
{ (void)p;(void)w;(void)h;(void)pitch; return 0; }
void frf_pd_ham6_wait_vblank(void) { }
void frf_pd_ham6_shutdown(void) { }
int frf_pd_ham6_active(void) { return 0; }

#endif
