/* Native Amiga AGA/HAM keyboard input through focused Intuition window.
 * Retains existing gameport/lowlevel joystick input independently. */
#include "frf_pd_keys.h"
#include <exec/types.h>
#include <exec/ports.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <libretro.h>
#include <string.h>
#include <stdio.h>

extern struct IntuitionBase *IntuitionBase;
static struct Window *key_window;
static UBYTE held[128];
static int escape_quit;

int frf_pd_keys_attach(struct Screen *screen)
{
    if(key_window) return 0;
    if(!screen || !IntuitionBase) return 0;
    memset(held,0,sizeof(held)); escape_quit=0;
    key_window=OpenWindowTags(NULL,
        WA_CustomScreen,screen, WA_Left,0,WA_Top,0,
        WA_Width,screen->Width,WA_Height,screen->Height,
        WA_Borderless,TRUE, WA_Backdrop,TRUE, WA_Activate,TRUE,
        WA_RMBTrap,TRUE,
        WA_IDCMP,IDCMP_RAWKEY|IDCMP_INACTIVEWINDOW,TAG_DONE);
    if(!key_window) puts("[FRF KEYS] Native keyboard window unavailable (Ctrl+C still exits)");
    else puts("[FRF KEYS] Native RAWKEY events active, ESC exits");
    return key_window!=NULL;
}
void frf_pd_keys_poll(void)
{
    struct IntuiMessage *msg;
    if(!key_window || !key_window->UserPort) return;
    while((msg=(struct IntuiMessage*)GetMsg(key_window->UserPort))) {
        ULONG cls=msg->Class;
        UWORD code=msg->Code;
        if(cls==IDCMP_RAWKEY) {
            held[code&127]=(code&128)?0:1;
            if((code&127)==0x45 && !(code&128)) escape_quit=1;
        } else if(cls==IDCMP_INACTIVEWINDOW) {
            memset(held,0,sizeof(held));
        }
        ReplyMsg((struct Message*)msg);
    }
}
int frf_pd_keys_quit(void){return escape_quit;}
int frf_pd_keys_state(unsigned id)
{
    switch(id) {
        case RETRO_DEVICE_ID_JOYPAD_UP:return held[0x4c];
        case RETRO_DEVICE_ID_JOYPAD_DOWN:return held[0x4d];
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:return held[0x4e];
        case RETRO_DEVICE_ID_JOYPAD_LEFT:return held[0x4f];
        case RETRO_DEVICE_ID_JOYPAD_B:return held[0x61];
        case RETRO_DEVICE_ID_JOYPAD_A:return held[0x64];
        case RETRO_DEVICE_ID_JOYPAD_Y:return held[0x63];
        case RETRO_DEVICE_ID_JOYPAD_START:return held[0x44];
        default:return 0;
    }
}
void frf_pd_keys_shutdown(void)
{
    if(key_window) { CloseWindow(key_window);key_window=NULL; }
    memset(held,0,sizeof(held));
    escape_quit=0;
}
