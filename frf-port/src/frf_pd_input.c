/* FRF PicoDriveRGB v0.2.21 - lowlevel, gameport and direct joystick hardware */
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

/* Modes: auto merges all usable routes; direct is always available on Amiga. */
#define FRF_INPUT_AUTO 0
#define FRF_INPUT_DIRECT 1
#define FRF_INPUT_LOWLEVEL 2
#define FRF_INPUT_GAMEPORT 3
static int g_input_mode=FRF_INPUT_AUTO;
static int g_input_debug=0;
static int g_input_port=2; /* Default: physical right Amiga joystick connector */
static ULONG g_hw_bits;
static UWORD g_hw_dat;
static UBYTE g_hw_pra;
static ULONG g_hw_last_bits=0xffffffffUL;


struct Library *LowLevelBase __attribute__((weak)) = NULL;
static UBYTE g_own_lowlevel;
static ULONG g_ll_raw0=JP_TYPE_NOTAVAIL;
static ULONG g_ll_raw1=JP_TYPE_NOTAVAIL;
static ULONG g_ll_p1=JP_TYPE_NOTAVAIL;
static ULONG g_ll_p2=JP_TYPE_NOTAVAIL;
static ULONG g_ll_last0=0xffffffffUL,g_ll_last1=0xffffffffUL;
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


/* Direct CHIP/CIA polling: works even if lowlevel.library returns NOTAVAIL
 * or another program owns gameport.device. JOYxDAT bit9=left, bit1=right,
 * bit9 xor bit8=up, bit1 xor bit0=down. CIAA PRA fire is active LOW:
 * bit 7 for joystick port 2, bit 6 for mouse/joystick port 1.
 * Never change the CIA direction registers; only read hardware. */
static ULONG frf_hw_decode(UWORD joy,UBYTE pra,int connector)
{
    ULONG bits=0;
    if(joy&0x0200U) bits|=JPF_JOY_LEFT;
    if(joy&0x0002U) bits|=JPF_JOY_RIGHT;
    if(((joy>>9)^(joy>>8))&1U) bits|=JPF_JOY_UP;
    if(((joy>>1)^joy)&1U) bits|=JPF_JOY_DOWN;
    if(!(pra & (connector==2 ? 0x80U:0x40U))) bits|=JPF_BUTTON_RED;
    return bits;
}

void frf_pd_input_config(int mode,int connector,int debug)
{
    g_input_mode=(mode>=0 && mode<=3)?mode:FRF_INPUT_AUTO;
    g_input_port=(connector==1)?1:2;
    g_input_debug=debug?1:0;
}

static void hw_poll(void)
{
    volatile UWORD *j=(volatile UWORD*)(g_input_port==2 ? 0xDFF00CUL:0xDFF00AUL);
    volatile UBYTE *cia=(volatile UBYTE*)0xBFE001UL;
    g_hw_dat=*j; g_hw_pra=*cia;
    g_hw_bits=frf_hw_decode(g_hw_dat,g_hw_pra,g_input_port);
    if(g_input_debug && g_hw_bits!=g_hw_last_bits) {
        printf("[FRF JOY] connector=%d JOY%uDAT=%04x CIAA=%02x P1bits=%08lx %s\n",
               g_input_port,g_input_port-1,(unsigned)g_hw_dat,(unsigned)g_hw_pra,
               (unsigned long)g_hw_bits,g_hw_bits?"ACTIVE":"released");
        fflush(stdout);
    }
    g_hw_last_bits=g_hw_bits;
}
static int hw_held(unsigned id)
{
    ULONG bit=0;
    switch(id) {
        case RETRO_DEVICE_ID_JOYPAD_UP:bit=JPF_JOY_UP;break;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:bit=JPF_JOY_DOWN;break;
        case RETRO_DEVICE_ID_JOYPAD_LEFT:bit=JPF_JOY_LEFT;break;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:bit=JPF_JOY_RIGHT;break;
        case RETRO_DEVICE_ID_JOYPAD_B:bit=JPF_BUTTON_RED;break;
        default:return 0;
    }
    return (g_hw_bits&bit)!=0;
}

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
    if(g_input_debug && (g_ll_raw0!=g_ll_last0 || g_ll_raw1!=g_ll_last1)) {
        printf("[FRF JOY LL] raw0=%08lx raw1=%08lx\n",
               (unsigned long)g_ll_raw0,(unsigned long)g_ll_raw1);
        fflush(stdout);
        g_ll_last0=g_ll_raw0;g_ll_last1=g_ll_raw1;
    }
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
    if(OpenDevice("gameport.device",(ULONG)(g_input_port-1),(struct IORequest *)g_gp_io,0)!=0)
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
    if(g_input_debug) {
        printf("[FRF JOY GP] code=%04x class=%u x=%d y=%d\n",
               (unsigned)ie->ie_Code,(unsigned)ie->ie_Class,
               (int)ie->ie_X,(int)ie->ie_Y);
        fflush(stdout);
    }
    /* Absolute joystick event coordinates describe the held direction even
     * when the same event reports fire down/up. Preserve both independently. */
    if(ie->ie_Class==IECLASS_RAWMOUSE) {
        g_gp_bits &= ~(JPF_JOY_LEFT|JPF_JOY_RIGHT|JPF_JOY_UP|JPF_JOY_DOWN);
        if(ie->ie_X<0) g_gp_bits|=JPF_JOY_LEFT;
        if(ie->ie_X>0) g_gp_bits|=JPF_JOY_RIGHT;
        if(ie->ie_Y<0) g_gp_bits|=JPF_JOY_UP;
        if(ie->ie_Y>0) g_gp_bits|=JPF_JOY_DOWN;
    }
    switch(ie->ie_Code) {
        case IECODE_NOBUTTON:
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
    g_hw_bits=0;g_hw_last_bits=0xffffffffUL;
    g_gp_active=0;
    if(g_input_mode==FRF_INPUT_AUTO||g_input_mode==FRF_INPUT_LOWLEVEL) {
        if(!LowLevelBase) {
            LowLevelBase=OpenLibrary("lowlevel.library",40);
            if(LowLevelBase) g_own_lowlevel=1;
        }
        ll_poll();
    }

    if(g_input_mode==FRF_INPUT_GAMEPORT ||
       (g_input_mode==FRF_INPUT_AUTO && !ll_valid(g_ll_p1))) {
        if(gp_init()) {
            g_gp_active=1;
            printf("[FRF PD INPUT] gameport.device unit %d claimed for P1\n",g_input_port-1);
        } else {
            gp_shutdown();
            printf("[FRF PD INPUT] gameport unavailable/in-use; direct CHIP path remains available in auto mode\n");
        }
    }
    if(g_input_mode==FRF_INPUT_AUTO||g_input_mode==FRF_INPUT_DIRECT) hw_poll();
    printf("[FRF PD INPUT] mode=%s hardware_port=%d lowlevel=%s gameport=%s direct=%s\n",
        g_input_mode==0?"auto":g_input_mode==1?"direct":g_input_mode==2?"lowlevel":"gameport",
        g_input_port,ll_valid(g_ll_p1)?"P1 detected":"unavailable",
        g_gp_active?"yes":"no",
        (g_input_mode<=FRF_INPUT_DIRECT)?"yes":"no");
    printf("[FRF PD INPUT] joystick fire -> Mega Drive B; CD32 red=B blue=C green=A; CTRL-C/ESC to exit\n");
    return ll_valid(g_ll_p1)||g_gp_active||g_input_mode<=FRF_INPUT_DIRECT;
}

void frf_pd_input_poll(void)
{
    if(g_input_mode==FRF_INPUT_AUTO||g_input_mode==FRF_INPUT_LOWLEVEL)
        ll_poll();
    if(g_gp_active) gp_poll();
    if(g_input_mode<=FRF_INPUT_DIRECT) hw_poll();
}

int16_t frf_pd_input_state(unsigned port,unsigned id)
{
    ULONG j;
    int r=0;
    if(port>1U) return 0;

    /* A standard digital joystick on the selected physical connector is
     * always Player 1, regardless of lowlevel or gameport ownership. */
    if(port==0U && g_input_mode<=FRF_INPUT_DIRECT)
        r|=hw_held(id);

    if(g_gp_active && port==0U) {
        ULONG bits=g_gp_bits,flag=0;
        switch(id) {
            case RETRO_DEVICE_ID_JOYPAD_UP:flag=JPF_JOY_UP;break;
            case RETRO_DEVICE_ID_JOYPAD_DOWN:flag=JPF_JOY_DOWN;break;
            case RETRO_DEVICE_ID_JOYPAD_LEFT:flag=JPF_JOY_LEFT;break;
            case RETRO_DEVICE_ID_JOYPAD_RIGHT:flag=JPF_JOY_RIGHT;break;
            case RETRO_DEVICE_ID_JOYPAD_B:flag=JPF_BUTTON_RED;break;
            case RETRO_DEVICE_ID_JOYPAD_A:flag=JPF_BUTTON_BLUE;break;
            default:break;
        }
        if(flag && (bits&flag)) r=1;
    }

    if(g_input_mode==FRF_INPUT_AUTO||g_input_mode==FRF_INPUT_LOWLEVEL) {
        j=port?g_ll_p2:g_ll_p1;
        if(held(j,id)) r=1;
    }
    return (int16_t)r;
}

void frf_pd_input_shutdown(void)
{
    gp_shutdown();
    g_ll_raw0=g_ll_raw1=g_ll_p1=g_ll_p2=JP_TYPE_NOTAVAIL;
    g_ll_dual=0;
    g_hw_bits=0;
    g_hw_last_bits=0xffffffffUL;
    g_ll_last0=g_ll_last1=0xffffffffUL;
    if(g_own_lowlevel && LowLevelBase) {
        CloseLibrary(LowLevelBase);
        LowLevelBase=NULL;
    }
    g_own_lowlevel=0;
}

#else
void frf_pd_input_config(int mode,int connector,int debug){(void)mode;(void)connector;(void)debug;}
int frf_pd_input_init(void){return 0;}
void frf_pd_input_poll(void){}
int16_t frf_pd_input_state(unsigned p,unsigned id){(void)p;(void)id;return 0;}
void frf_pd_input_shutdown(void){}
#endif
