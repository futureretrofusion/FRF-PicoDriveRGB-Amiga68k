/*
 * FRF PicoDriveRGB Emu68 V0.2.14 - 44.1 kHz gapless AHI + audio-master governor telemetry
 *
 * PicoDrive produces signed 16-bit stereo PCM into an SPSC ring. A dedicated
 * Amiga Process owns ahi.device and uses the documented two-request linked
 * CMD_WRITE sequence (SendIO + ahir_Link + WaitIO) so the next buffer is
 * queued before the current buffer completes. This keeps AHI hardware-paced
 * and removes the stop/start gaps and throughput loss caused by one unlinked
 * synchronous DoIO() per block.
 */
#include "frf_pd_audio.h"

#include <stdio.h>
#include <string.h>

#if defined(__amigaos__) || defined(__AMIGA__) || defined(AMIGA) || defined(__amiga__)
#define FRF_PD_AMIGA 1
#else
#define FRF_PD_AMIGA 0
#endif

#if FRF_PD_AMIGA
#include <exec/types.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <devices/ahi.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define FRF_AHI_CHANNELS       2U
#define FRF_AHI_BYTES_FRAME    4U
#define FRF_AHI_BLOCK_FRAMES   1024U
#define FRF_AHI_PREFILL_BLOCKS 4U
#define FRF_AHI_RING_FRAMES    32768U

static int16_t *g_ring;
static volatile ULONG g_wseq;
static volatile ULONG g_rseq;
static volatile ULONG g_dropped;
static volatile ULONG g_played;
static volatile ULONG g_blocks;
static volatile ULONG g_silence_frames;
static volatile ULONG g_chain_submits;
static volatile ULONG g_io_errors;
static volatile UBYTE g_worker_stop;
static volatile UBYTE g_worker_alive;
static volatile UBYTE g_worker_ready;
static volatile UBYTE g_worker_failed;
static volatile UBYTE g_worker_playing;
static volatile UBYTE g_underrun;
static unsigned g_rate=44100U;
static struct Process *g_worker_process;

static ULONG ring_buffered(void)
{
    ULONG w=g_wseq, r=g_rseq;
    return w-r;
}

static ULONG pull_block_or_silence(int16_t *dst)
{
    ULONG w=g_wseq, r=g_rseq, avail=w-r;
    ULONG take,i;

    if(avail>FRF_AHI_RING_FRAMES) avail=FRF_AHI_RING_FRAMES;
    take=avail;
    if(take>FRF_AHI_BLOCK_FRAMES) take=FRF_AHI_BLOCK_FRAMES;

    __asm__ volatile("" ::: "memory");
    for(i=0;i<take;i++) {
        ULONG idx=(r+i)&(FRF_AHI_RING_FRAMES-1U);
        dst[i*2U+0U]=g_ring[idx*2U+0U];
        dst[i*2U+1U]=g_ring[idx*2U+1U];
    }
    for(;i<FRF_AHI_BLOCK_FRAMES;i++) {
        dst[i*2U+0U]=0;
        dst[i*2U+1U]=0;
    }

    if(take) {
        __asm__ volatile("" ::: "memory");
        g_rseq=r+take;
    }
    if(take<FRF_AHI_BLOCK_FRAMES) {
        g_underrun=1;
        g_silence_frames+=FRF_AHI_BLOCK_FRAMES-take;
    } else {
        g_underrun=0;
    }
    return take;
}

static void setup_write(struct AHIRequest *req,int16_t *out,
                        struct AHIRequest *link)
{
    req->ahir_Std.io_Message.mn_Node.ln_Pri=20;
    req->ahir_Std.io_Command=CMD_WRITE;
    req->ahir_Std.io_Flags=0;
    req->ahir_Std.io_Data=(APTR)out;
    req->ahir_Std.io_Length=FRF_AHI_BLOCK_FRAMES*FRF_AHI_BYTES_FRAME;
    req->ahir_Std.io_Offset=0;
    req->ahir_Frequency=g_rate;
    req->ahir_Type=AHIST_S16S;
    req->ahir_Volume=0x10000;
    req->ahir_Position=0x8000;
    req->ahir_Link=link;
}

static void abort_wait(struct AHIRequest *req,UBYTE *pending)
{
    if(!req || !*pending) return;
    if(!CheckIO((struct IORequest *)req)) AbortIO((struct IORequest *)req);
    WaitIO((struct IORequest *)req);
    *pending=0;
}

static LONG frf_audio_process_entry(void)
{
    struct MsgPort *port=NULL;
    struct AHIRequest *req[2]={NULL,NULL};
    int16_t *out[2]={NULL,NULL};
    UBYTE pending[2]={0,0};
    UBYTE device_open=0;
    unsigned cur=0,next=1;
    ULONG wait_ticks=0;

    g_worker_alive=1;
    g_worker_ready=0;
    g_worker_failed=0;
    g_worker_playing=0;
    g_underrun=0;

    port=CreateMsgPort();
    if(!port) goto fail;

    req[0]=(struct AHIRequest *)CreateIORequest(port,sizeof(struct AHIRequest));
    if(!req[0]) goto fail;
    req[0]->ahir_Version=4;

    if(OpenDevice(AHINAME,AHI_DEFAULT_UNIT,(struct IORequest *)req[0],0)!=0)
        goto fail;
    device_open=1;

    /* AHI's documented playback double-buffer sequence uses a copied second
     * request sharing the opened device/unit and the same reply port. */
    req[1]=(struct AHIRequest *)AllocVec(sizeof(struct AHIRequest),MEMF_PUBLIC|MEMF_CLEAR);
    if(!req[1]) goto fail;
    CopyMem(req[0],req[1],sizeof(struct AHIRequest));

    out[0]=(int16_t *)AllocVec(FRF_AHI_BLOCK_FRAMES*FRF_AHI_BYTES_FRAME,
                               MEMF_PUBLIC|MEMF_CLEAR);
    out[1]=(int16_t *)AllocVec(FRF_AHI_BLOCK_FRAMES*FRF_AHI_BYTES_FRAME,
                               MEMF_PUBLIC|MEMF_CLEAR);
    if(!out[0] || !out[1]) goto fail;

    g_worker_ready=1;

    /* Prime enough PCM that the first two linked writes can be submitted
     * back-to-back. Do not start/stop AHI around every short producer gap. */
    while(!g_worker_stop &&
          ring_buffered()<FRF_AHI_BLOCK_FRAMES*FRF_AHI_PREFILL_BLOCKS)
    {
        Delay(1);
        if(++wait_ticks>250U) {
            /* If the core is slow to start producing, keep waiting rather than
             * failing audio initialization. */
            wait_ticks=250U;
        }
    }
    if(g_worker_stop) goto out;

    pull_block_or_silence(out[cur]);
    setup_write(req[cur],out[cur],NULL);
    SendIO((struct IORequest *)req[cur]);
    pending[cur]=1;
    g_chain_submits++;
    g_worker_playing=1;

    while(!g_worker_stop)
    {
        LONG ioerr;

        /* Queue the next block before waiting for the current one. If the
         * producer is briefly late, zero-pad only the missing tail so the AHI
         * stream remains continuous instead of stopping/restarting. */
        pull_block_or_silence(out[next]);
        setup_write(req[next],out[next],req[cur]);
        SendIO((struct IORequest *)req[next]);
        pending[next]=1;
        g_chain_submits++;

        ioerr=WaitIO((struct IORequest *)req[cur]);
        pending[cur]=0;
        if(ioerr!=0) {
            g_io_errors++;
            g_worker_failed=1;
            break;
        }
        g_played+=FRF_AHI_BLOCK_FRAMES;
        g_blocks++;

        {
            unsigned tmp=cur; cur=next; next=tmp;
        }
    }

out:
    g_worker_playing=0;
    abort_wait(req[0],&pending[0]);
    abort_wait(req[1],&pending[1]);
    if(device_open && req[0]) CloseDevice((struct IORequest *)req[0]);
    if(out[1]) FreeVec(out[1]);
    if(out[0]) FreeVec(out[0]);
    if(req[1]) FreeVec(req[1]);
    if(req[0]) DeleteIORequest((struct IORequest *)req[0]);
    if(port) DeleteMsgPort(port);
    g_worker_ready=0;
    g_worker_alive=0;
    return 0;

fail:
    g_worker_failed=1;
    goto out;
}

int frf_pd_audio_init(unsigned rate_hz)
{
    ULONG wait_ticks=0;

    frf_pd_audio_shutdown();

    if(rate_hz<8000U || rate_hz>60000U)
        rate_hz=44100U;
    g_rate=rate_hz;

    g_ring=(int16_t *)AllocVec(FRF_AHI_RING_FRAMES*FRF_AHI_BYTES_FRAME,
                               MEMF_PUBLIC|MEMF_CLEAR);
    if(!g_ring)
    {
        printf("[FRF PD AHI] ring allocation failed; continuing silent\n");
        return 0;
    }

    g_wseq=g_rseq=0;
    g_dropped=0;
    g_played=0;
    g_blocks=0;
    g_silence_frames=0;
    g_chain_submits=0;
    g_io_errors=0;
    g_worker_stop=0;
    g_worker_alive=0;
    g_worker_ready=0;
    g_worker_failed=0;
    g_worker_playing=0;
    g_underrun=0;

    g_worker_process=(struct Process *)CreateNewProcTags(
        NP_Name,(ULONG)"PicoDriveRGB audio",
        NP_StackSize,65536UL,
        NP_Priority,11,
        NP_Entry,(ULONG)frf_audio_process_entry,
        TAG_DONE);

    if(!g_worker_process)
    {
        printf("[FRF PD AHI] CreateNewProcTags failed; continuing silent\n");
        FreeVec(g_ring); g_ring=NULL;
        return 0;
    }

    while(!g_worker_ready && !g_worker_failed && wait_ticks<100U)
    {
        Delay(1);
        wait_ticks++;
    }

    if(!g_worker_ready)
    {
        printf("[FRF PD AHI] worker/AHI startup failed; continuing silent\n");
        g_worker_stop=1;
        while(g_worker_alive && wait_ticks<200U) { Delay(1); wait_ticks++; }
        if(!g_worker_alive && g_ring) { FreeVec(g_ring); g_ring=NULL; }
        return 0;
    }

    printf("[FRF PD AHI] READY %u Hz stereo signed-16 GAPLESS-DB block=%u ring=%u\n",
           g_rate,FRF_AHI_BLOCK_FRAMES,FRF_AHI_RING_FRAMES);
    return 1;
}

size_t frf_pd_audio_push(const int16_t *src,size_t frames)
{
    size_t n;
    ULONG w,r,used,space,take;

    if(!g_worker_ready || !g_ring || !src) return frames;

    w=g_wseq;
    r=g_rseq;
    used=w-r;
    if(used>FRF_AHI_RING_FRAMES) used=FRF_AHI_RING_FRAMES;
    space=FRF_AHI_RING_FRAMES-used;
    take=(ULONG)frames;
    if(take>space) take=space;

    for(n=0;n<(size_t)take;n++)
    {
        ULONG idx=(w+(ULONG)n)&(FRF_AHI_RING_FRAMES-1U);
        g_ring[idx*2U+0U]=src[n*2U+0U];
        g_ring[idx*2U+1U]=src[n*2U+1U];
    }

    __asm__ volatile("" ::: "memory");
    g_wseq=w+take;
    if(take<(ULONG)frames) g_dropped+=(ULONG)frames-take;
    return frames;
}

void frf_pd_audio_push_sample(int16_t l,int16_t r)
{
    int16_t s[2]; s[0]=l; s[1]=r;
    (void)frf_pd_audio_push(s,1U);
}

void frf_pd_audio_pump(void) { }

unsigned frf_pd_audio_buffered_frames(void)
{
    ULONG buffered=ring_buffered();
    if(buffered>FRF_AHI_RING_FRAMES) buffered=FRF_AHI_RING_FRAMES;
    return (unsigned)buffered;
}

unsigned frf_pd_audio_capacity_frames(void)
{
    return FRF_AHI_RING_FRAMES;
}

unsigned frf_pd_audio_occupancy_percent(void)
{
    ULONG occupied=ring_buffered();
    if(g_worker_playing) occupied+=FRF_AHI_BLOCK_FRAMES*2U;
    if(occupied>FRF_AHI_RING_FRAMES) occupied=FRF_AHI_RING_FRAMES;
    return (unsigned)((occupied*100UL)/FRF_AHI_RING_FRAMES);
}

int frf_pd_audio_underrun_likely(void)
{
    return g_worker_ready ?
        (g_underrun || frf_pd_audio_occupancy_percent()<10U) : 0;
}

int frf_pd_audio_active(void) { return g_worker_ready!=0; }

unsigned long frf_pd_audio_dropped_frames(void)
{
    return (unsigned long)g_dropped;
}

void frf_pd_audio_shutdown(void)
{
    ULONG ticks=0;

    if(g_worker_alive || g_worker_ready)
    {
        g_worker_stop=1;
        while(g_worker_alive && ticks<100U)
        {
            Delay(1);
            ticks++;
        }
    }

    if(g_worker_alive)
    {
        printf("[FRF PD AHI] WARNING worker did not stop; leaving ring allocated\n");
        return;
    }

    printf("[FRF PD AHI] shutdown played=%lu dropped=%lu silence=%lu blocks=%lu chained=%lu ioerr=%lu block_frames=%u\n",
           (unsigned long)g_played,
           (unsigned long)g_dropped,
           (unsigned long)g_silence_frames,
           (unsigned long)g_blocks,
           (unsigned long)g_chain_submits,
           (unsigned long)g_io_errors,
           FRF_AHI_BLOCK_FRAMES);

    if(g_ring) { FreeVec(g_ring); g_ring=NULL; }
    g_worker_process=NULL;
    g_wseq=g_rseq=0;
    g_worker_stop=0;
    g_worker_ready=0;
    g_worker_failed=0;
    g_worker_playing=0;
    g_underrun=0;
}

#else
int frf_pd_audio_init(unsigned r){(void)r;return 0;}
size_t frf_pd_audio_push(const int16_t *p,size_t n){(void)p;return n;}
void frf_pd_audio_push_sample(int16_t l,int16_t r){(void)l;(void)r;}
void frf_pd_audio_pump(void){}
void frf_pd_audio_shutdown(void){}
unsigned frf_pd_audio_occupancy_percent(void){return 100U;}
unsigned frf_pd_audio_buffered_frames(void){return 0U;}
unsigned frf_pd_audio_capacity_frames(void){return 0U;}
int frf_pd_audio_underrun_likely(void){return 0;}
int frf_pd_audio_active(void){return 0;}
unsigned long frf_pd_audio_dropped_frames(void){return 0;}
#endif
