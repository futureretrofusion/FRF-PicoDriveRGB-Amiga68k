/* FRF PicoDriveRGB Emu68 V0.2.14 - absolute deadline pacing + audio-refill rebase */
#include "frf_pd_timer.h"
#include <stdio.h>
#if defined(__amigaos__) || defined(__AMIGA__) || defined(AMIGA) || defined(__amiga__)
#define FRF_PD_AMIGA 1
#else
#define FRF_PD_AMIGA 0
#endif
#if FRF_PD_AMIGA
#include <exec/types.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
struct Device *TimerBase __attribute__((weak)) = NULL;
static struct MsgPort *g_port;
static struct timerequest *g_req;
static UBYTE g_own_timer;
static unsigned g_hz=60U;
static ULONG g_base_us=16666U,g_rem_us=40U,g_rem_acc;
static unsigned long long g_deadline;
static unsigned long g_late_frames;
static unsigned long long now_us(void)
{
    struct timeval tv;
    GetSysTime(&tv);
    return (unsigned long long)tv.tv_secs*1000000ULL+(unsigned long long)tv.tv_micro;
}
static void sleep_us(ULONG usec)
{
    if(!usec||!g_req) return;
    g_req->tr_node.io_Command=TR_ADDREQUEST;
    g_req->tr_time.tv_secs=usec/1000000UL;
    g_req->tr_time.tv_micro=usec%1000000UL;
    DoIO((struct IORequest *)g_req);
}
int frf_pd_timer_init(unsigned hz)
{
    frf_pd_timer_shutdown();
    if(hz<1U) hz=60U;
    g_hz=hz; g_base_us=1000000UL/hz; g_rem_us=1000000UL%hz;
    g_rem_acc=0; g_late_frames=0;
    g_port=CreateMsgPort(); if(!g_port) return 0;
    g_req=(struct timerequest *)CreateIORequest(g_port,sizeof(*g_req));
    if(!g_req){frf_pd_timer_shutdown();return 0;}
    if(OpenDevice(TIMERNAME,UNIT_MICROHZ,(struct IORequest *)g_req,0)!=0)
    {frf_pd_timer_shutdown();return 0;}
    g_own_timer=1; TimerBase=g_req->tr_node.io_Device; g_deadline=now_us();
    printf("[FRF PD TIMER] deadline pacing %u Hz base=%luus remainder=%lu/%u\n",
           g_hz,(unsigned long)g_base_us,(unsigned long)g_rem_us,g_hz);
    return 1;
}
void frf_pd_timer_wait_next(void)
{
    unsigned long long now;
    ULONG step=g_base_us;
    if(!g_req) return;
    g_rem_acc+=g_rem_us;
    if(g_rem_acc>=g_hz){step++;g_rem_acc-=g_hz;}
    g_deadline+=(unsigned long long)step;
    now=now_us();
    if(now<g_deadline)
    {
        unsigned long long d=g_deadline-now;
        if(d>0xffffffffULL)d=0xffffffffULL;
        sleep_us((ULONG)d);
    }
    else
    {
        unsigned long long late=now-g_deadline;
        /* RECOVERY RULE: never repay lateness by running future frames early.
         * Count the miss, then reset the schedule to 'now'. This prevents the
         * slow -> catch-up -> fast oscillation that made Sonic gameplay wrong. */
        g_late_frames++;
        if(late>(unsigned long long)step)
            g_late_frames+=(unsigned long)(late/(unsigned long long)step);
        g_deadline=now;
        g_rem_acc=0;
    }
}
void frf_pd_timer_rebase_now(void)
{
    if(!g_req) return;
    g_deadline=now_us();
    g_rem_acc=0;
}
unsigned long frf_pd_timer_late_frames(void){return g_late_frames;}
void frf_pd_timer_shutdown(void)
{
    if(g_req)
    {
        if(g_own_timer&&g_req->tr_node.io_Device)CloseDevice((struct IORequest *)g_req);
        DeleteIORequest((struct IORequest *)g_req); g_req=NULL;
    }
    if(g_port){DeleteMsgPort(g_port);g_port=NULL;}
    TimerBase=NULL;g_own_timer=0;g_deadline=0;g_rem_acc=0;
}
#else
int frf_pd_timer_init(unsigned hz){(void)hz;return 0;}
void frf_pd_timer_wait_next(void){}
void frf_pd_timer_rebase_now(void){}
unsigned long frf_pd_timer_late_frames(void){return 0;}
void frf_pd_timer_shutdown(void){}
#endif
