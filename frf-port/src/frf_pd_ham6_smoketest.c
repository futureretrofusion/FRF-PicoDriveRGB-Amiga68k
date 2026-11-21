#include "frf_pd_ham6.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__amigaos__) || defined(__AMIGA__) || defined(AMIGA) || defined(__amiga__)
#include <exec/types.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#endif

#define W 320
#define H 240

static unsigned short rgb565(unsigned r,unsigned g,unsigned b)
{
    return (unsigned short)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}

int main(void)
{
    unsigned short *fb=(unsigned short *)malloc(W*H*2);
    unsigned frame,x,y;
    if(!fb) return 20;

    if(!frf_pd_ham6_init()) {
        printf("FRF PicoDriveRGB smoke test: HAM6 init failed or non-Amiga build.\n");
        free(fb);
        return 10;
    }

    printf("FRF PicoDriveRGB V0.1.1 smoke test running. Ctrl-C exits early.\n");
    for(frame=0;frame<500;frame++) {
        for(y=0;y<H;y++) {
            for(x=0;x<W;x++) {
                unsigned band=(x+frame*2U)/40U;
                unsigned r=(x+frame)&255U;
                unsigned g=(y*255U)/(H-1U);
                unsigned b=((x^y)+(frame*3U))&255U;
                if((y/30U)&1U) {
                    if((band&3U)==0U){r=255;g>>=2;b>>=2;}
                    else if((band&3U)==1U){g=255;r>>=2;b>>=2;}
                    else if((band&3U)==2U){b=255;r>>=2;g>>=2;}
                }
                fb[y*W+x]=rgb565(r,g,b);
            }
        }
        frf_pd_ham6_present_rgb565(fb,W,H,W*2);
        frf_pd_ham6_wait_vblank();
#if defined(SIGBREAKF_CTRL_C)
        if(SetSignal(0,0)&SIGBREAKF_CTRL_C) break;
#endif
    }
    frf_pd_ham6_shutdown();
    free(fb);
    return 0;
}
