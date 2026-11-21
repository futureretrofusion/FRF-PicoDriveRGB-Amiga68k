/* FRF PicoDriveRGB Emu68 V0.2.6 - lowlevel + classic gameport fallback */
#include "frf_pd_input.h"
#include <stdio.h>
#include <string.h>
#include <libretro.h>

#if defined(__amigaos__) || defined(__AMIGA__) || defined(AMIGA) || defined(__amiga__)
#define FRF_PD_AMIGA 1
#else
#define FRF_PD_AMIGA 0
#endif

#if FRF_PD_AMIGA
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <libraries/lowlevel.h>
#include <devices/gameport.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/lowlevel.h>

struct Library *LowLevelBase __attribute__((weak)) = NULL;
static UBYTE g_own_lowlevel;
static ULONG g_ll_raw0=JP_TYPE_NOTAVAIL;
static ULONG g_ll_raw1=JP_TYPE_NOTAVAIL;
static ULONG g_ll_p1=JP_TYPE_NOTAVAIL;
static ULONG g_ll_p2=JP_TYPE_NOTAVAIL;
static UBYTE g_ll_dual;

/* Proven classic-Amiga fallback from the PocketSNESRGB direct-input path.
 * gameport.device unit 1 is the physical joystick connector normally used
 * for Player 1 on an Amiga. It remains useful when lowlevel.library opens
 * successfully but ReadJoyPort() reports JP_TYPE_NOTAVAIL, as seen in the
 * V0.2.0 PicoDrive log. */
static struct MsgPort *g_gp_port;
static struct IOStdReq *g_gp_io;
static struct InputEvent g_gp_event;
static UBYTE g_gp_open;
static UBYTE g_gp_claimed;
static UBYTE g_gp_pending;
static UBYTE g_gp_active;
static ULONG g_gp_bits;

static int ll_valid(ULONG j)
{
    ULONG t=j&JP_TYPE_MASK;
    return t==JP_TYPE_GAMECTLR || t==JP_TYPE_JOYSTK;
}

static void ll_poll(void)
{
    int v0,v1;
    g_ll_raw0=g_ll_raw1=JP_TYPE_NOTAVAIL;
    g_ll_p1=g_ll_p2=JP_TYPE_NOTAVAIL;
    g_ll_dual=0;
    if(!LowLevelBase) return;

    g_ll_raw0=ReadJoyPort(0);
    g_ll_raw1=ReadJoyPort(1);
    v0=ll_valid(g_ll_raw0);
    v1=ll_valid(g_ll_raw1);

    /* Important physical-port rule: with only one detected controller, it is
     * Player 1 regardless of whether lowlevel exposes it as port 0 or port 1.
     * Two controllers retain independent P1/P2 routing. */
    if(v0 && v1) {
        g_ll_p1=g_ll_raw0;
        g_ll_p2=g_ll_raw1;
        g_ll_dual=1;
    } else if(v0) {
        g_ll_p1=g_ll_raw0;
    } else if(v1) {
        g_ll_p1=g_ll_raw1;
    }
}

static void gp_send_read(void)
{
    if(!g_gp_open || !g_gp_claimed || !g_gp_io || g_gp_pending) return;
    memset(&g_gp_event,0,sizeof(g_gp_event));
    g_gp_io->io_Command=GPD_READEVENT;
    g_gp_io->io_Flags=0;
    g_gp_io->io_Data=(APTR)&g_gp_event;
    g_gp_io->io_Length=sizeof(g_gp_event);
    SendIO((struct IORequest *)g_gp_io);
    g_gp_pending=1;
}

static int gp_init(void)
{
    BYTE ctype=GPCT_NOCONTROLLER;
    struct GamePortTrigger trig;

    g_gp_port=CreateMsgPort();
    if(!g_gp_port) return 0;
    g_gp_io=(struct IOStdReq *)CreateIORequest(g_gp_port,sizeof(struct IOStdReq));
    if(!g_gp_io) return 0;
    g_gp_io->io_Message.mn_Node.ln_Type=NT_UNKNOWN;

    /* Unit 1 = physical Amiga controller PORT 2, the normal P1 joystick port. */
    if(OpenDevice("gameport.device",1,(struct IORequest *)g_gp_io,0)!=0)
        return 0;
    g_gp_open=1;

    Forbid();
    g_gp_io->io_Command=GPD_ASKCTYPE;
    g_gp_io->io_Flags=IOF_QUICK;
    g_gp_io->io_Data=(APTR)&ctype;
    g_gp_io->io_Length=1;
    DoIO((struct IORequest *)g_gp_io);
    if(ctype==GPCT_NOCONTROLLER) {
        ctype=GPCT_ABSJOYSTICK;
        g_gp_io->io_Command=GPD_SETCTYPE;
        g_gp_io->io_Flags=IOF_QUICK;
        g_gp_io->io_Data=(APTR)&ctype;
        g_gp_io->io_Length=1;
        DoIO((struct IORequest *)g_gp_io);
        if(g_gp_io->io_Error==0) g_gp_claimed=1;
    }
    Permit();
    if(!g_gp_claimed) return 0;

    memset(&trig,0,sizeof(trig));
    trig.gpt_Keys=GPTF_UPKEYS|GPTF_DOWNKEYS;
    trig.gpt_XDelta=1;
    trig.gpt_YDelta=1;
    trig.gpt_Timeout=0;
    g_gp_io->io_Command=GPD_SETTRIGGER;
    g_gp_io->io_Flags=IOF_QUICK;
    g_gp_io->io_Data=(APTR)&trig;
    g_gp_io->io_Length=sizeof(trig);
    if(DoIO((struct IORequest *)g_gp_io)!=0 || g_gp_io->io_Error!=0)
        return 0;

    g_gp_io->io_Command=CMD_CLEAR;
    g_gp_io->io_Flags=IOF_QUICK;
    g_gp_io->io_Data=NULL;
    g_gp_io->io_Length=0;
    DoIO((struct IORequest *)g_gp_io);

    g_gp_bits=0;
    gp_send_read();
    return 1;
}

static void gp_process(const struct InputEvent *ie)
{
    switch(ie->ie_Code) {
        case IECODE_NOBUTTON:
            g_gp_bits &= ~(JPF_JOY_LEFT|JPF_JOY_RIGHT|JPF_JOY_UP|JPF_JOY_DOWN);
            if(ie->ie_X<0) g_gp_bits|=JPF_JOY_LEFT;
            if(ie->ie_X>0) g_gp_bits|=JPF_JOY_RIGHT;
            if(ie->ie_Y<0) g_gp_bits|=JPF_JOY_UP;
            if(ie->ie_Y>0) g_gp_bits|=JPF_JOY_DOWN;
            break;
        case IECODE_LBUTTON:
            g_gp_bits|=JPF_BUTTON_RED; /* RetroPad B -> Mega Drive B */
            break;
        case (IECODE_LBUTTON|IECODE_UP_PREFIX):
            g_gp_bits&=~JPF_BUTTON_RED;
            break;
        case IECODE_RBUTTON:
            g_gp_bits|=JPF_BUTTON_BLUE; /* RetroPad A -> Mega Drive C */
            break;
        case (IECODE_RBUTTON|IECODE_UP_PREFIX):
            g_gp_bits&=~JPF_BUTTON_BLUE;
            break;
        default:
            break;
    }
}

static void gp_poll(void)
{
    if(!g_gp_active || !g_gp_io) return;
    if(g_gp_pending && CheckIO((struct IORequest *)g_gp_io)) {
        WaitIO((struct IORequest *)g_gp_io);
        g_gp_pending=0;
        if(g_gp_io->io_Error==0) gp_process(&g_gp_event);
        gp_send_read();
    }
}

static void gp_shutdown(void)
{
    BYTE ctype=GPCT_NOCONTROLLER;
    if(g_gp_io && g_gp_pending) {
        if(!CheckIO((struct IORequest *)g_gp_io)) AbortIO((struct IORequest *)g_gp_io);
        WaitIO((struct IORequest *)g_gp_io);
        g_gp_pending=0;
    }
    if(g_gp_open && g_gp_io && g_gp_claimed) {
        g_gp_io->io_Command=GPD_SETCTYPE;
        g_gp_io->io_Flags=IOF_QUICK;
        g_gp_io->io_Data=(APTR)&ctype;
        g_gp_io->io_Length=1;
        DoIO((struct IORequest *)g_gp_io);
        g_gp_claimed=0;
    }
    if(g_gp_open && g_gp_io) {
        CloseDevice((struct IORequest *)g_gp_io);
        g_gp_open=0;
    }
    if(g_gp_io) { DeleteIORequest((struct IORequest *)g_gp_io); g_gp_io=NULL; }
    if(g_gp_port) { DeleteMsgPort(g_gp_port); g_gp_port=NULL; }
    g_gp_active=0;
    g_gp_bits=0;
}

static int held(ULONG j,unsigned id)
{
    if(!ll_valid(j)) return 0;
    switch(id) {
        case RETRO_DEVICE_ID_JOYPAD_UP:    return (j&JPF_JOY_UP)!=0;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:  return (j&JPF_JOY_DOWN)!=0;
        case RETRO_DEVICE_ID_JOYPAD_LEFT:  return (j&JPF_JOY_LEFT)!=0;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT: return (j&JPF_JOY_RIGHT)!=0;
        case RETRO_DEVICE_ID_JOYPAD_B:     return (j&JPF_BUTTON_RED)!=0;
        case RETRO_DEVICE_ID_JOYPAD_A:     return (j&JPF_BUTTON_BLUE)!=0;
        case RETRO_DEVICE_ID_JOYPAD_Y:     return (j&JPF_BUTTON_GREEN)!=0;
        case RETRO_DEVICE_ID_JOYPAD_X:     return (j&JPF_BUTTON_YELLOW)!=0;
        case RETRO_DEVICE_ID_JOYPAD_L:     return (j&JPF_BUTTON_REVERSE)!=0;
        case RETRO_DEVICE_ID_JOYPAD_R:     return (j&JPF_BUTTON_FORWARD)!=0;
        case RETRO_DEVICE_ID_JOYPAD_START: return (j&JPF_BUTTON_PLAY)!=0;
        case RETRO_DEVICE_ID_JOYPAD_SELECT:
            return (j&(JPF_BUTTON_FORWARD|JPF_BUTTON_REVERSE))==
                   (JPF_BUTTON_FORWARD|JPF_BUTTON_REVERSE);
        default: return 0;
    }
}

int frf_pd_input_init(void)
{
    if(!LowLevelBase) {
        LowLevelBase=OpenLibrary("lowlevel.library",40);
        if(LowLevelBase) g_own_lowlevel=1;
    }
    ll_poll();

    if(ll_valid(g_ll_p1)) {
        printf("[FRF PD INPUT] lowlevel P1=yes P2=%s%s\n",
               ll_valid(g_ll_p2)?"yes":"no",g_ll_dual?" dual":"");
    } else {
        if(gp_init()) {
            g_gp_active=1;
            printf("[FRF PD INPUT] lowlevel found no pad; gameport.device unit 1 fallback ACTIVE for P1\n");
            printf("[FRF PD INPUT] classic joystick: directions + FIRE=B, second fire=C\n");
        } else {
            gp_shutdown();
            printf("[FRF PD INPUT] no lowlevel pad and gameport.device fallback unavailable; Ctrl-C exit only\n");
        }
    }
    printf("[FRF PD INPUT] CD32: RED=B BLUE=C GREEN=A YELLOW=Y REVERSE=X FORWARD=Z PLAY=START\n");
    return ll_valid(g_ll_p1) || g_gp_active;
}

void frf_pd_input_poll(void)
{
    if(g_gp_active) gp_poll();
    else ll_poll();
}

int16_t frf_pd_input_state(unsigned port,unsigned id)
{
    ULONG j;
    if(port>1U) return 0;
    if(g_gp_active) {
        if(port!=0U) return 0;
        j=g_gp_bits;
        /* gameport bits are already JPF-compatible, but have no type bits */
        switch(id) {
            case RETRO_DEVICE_ID_JOYPAD_UP:    return (j&JPF_JOY_UP)?1:0;
            case RETRO_DEVICE_ID_JOYPAD_DOWN:  return (j&JPF_JOY_DOWN)?1:0;
            case RETRO_DEVICE_ID_JOYPAD_LEFT:  return (j&JPF_JOY_LEFT)?1:0;
            case RETRO_DEVICE_ID_JOYPAD_RIGHT: return (j&JPF_JOY_RIGHT)?1:0;
            case RETRO_DEVICE_ID_JOYPAD_B:     return (j&JPF_BUTTON_RED)?1:0;
            case RETRO_DEVICE_ID_JOYPAD_A:     return (j&JPF_BUTTON_BLUE)?1:0;
            default: return 0;
        }
    }
    j=port?g_ll_p2:g_ll_p1;
    return held(j,id)?1:0;
}

void frf_pd_input_shutdown(void)
{
    gp_shutdown();
    g_ll_raw0=g_ll_raw1=g_ll_p1=g_ll_p2=JP_TYPE_NOTAVAIL;
    g_ll_dual=0;
    if(g_own_lowlevel && LowLevelBase) {
        CloseLibrary(LowLevelBase);
        LowLevelBase=NULL;
    }
    g_own_lowlevel=0;
}

#else
int frf_pd_input_init(void){return 0;}
void frf_pd_input_poll(void){}
int16_t frf_pd_input_state(unsigned p,unsigned id){(void)p;(void)id;return 0;}
void frf_pd_input_shutdown(void){}
#endif
