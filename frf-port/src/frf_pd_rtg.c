#include <exec/types.h>
#include <exec/libraries.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>
#include <proto/graphics.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libretro.h>
struct Library *CyberGfxBase;
extern struct IntuitionBase *IntuitionBase;
static int rtg_owns_intuition=0;
static struct Screen *scr;
static struct Window *win;
static UBYTE *buf;
static unsigned sw,sh,k=2,scan=0,windowed=0,owns_screen=0;
static UBYTE rtg_keys[128];
static int rtg_close_requested=0;
void frf_pd_rtg_config(int s,int l){k=(s>=1&&s<=4)?s:2;scan=(l==25||l==50||l==75)?l:0;}
void frf_pd_rtg_window(int on){windowed=on!=0;}
void frf_pd_rtg_shutdown(void){
 memset(rtg_keys,0,sizeof(rtg_keys));
 rtg_close_requested=0;
 if(buf){free(buf);buf=0;}
 if(win){CloseWindow(win);win=0;}
 if(scr){if(owns_screen)CloseScreen(scr);else UnlockPubScreen(NULL,scr);scr=0;}
 owns_screen=0;
 if(CyberGfxBase){CloseLibrary(CyberGfxBase);CyberGfxBase=0;}
 if(rtg_owns_intuition&&IntuitionBase){CloseLibrary((struct Library*)IntuitionBase);IntuitionBase=0;rtg_owns_intuition=0;}
}
int frf_pd_rtg_init(void){
 ULONG mode;
 memset(rtg_keys,0,sizeof(rtg_keys));
 rtg_close_requested=0;
 sw=k>=3?1280:(k==2?640:320);sh=k>=4?1080:(k>=3?720:(k==2?480:240));
 puts("[FRF RTG] opening CyberGraphX");fflush(stdout);
 if(!IntuitionBase){IntuitionBase=(struct IntuitionBase*)OpenLibrary("intuition.library",39);if(IntuitionBase)rtg_owns_intuition=1;}
 printf("[RTG] IntuitionBase=%p\n",(void*)IntuitionBase);fflush(stdout);
 if(!IntuitionBase)return 0;
 CyberGfxBase=OpenLibrary("cybergraphics.library",40);
 if(!CyberGfxBase){frf_pd_rtg_shutdown();return 0;}
 if(windowed){
  scr=LockPubScreen(NULL);
  if(!scr){puts("[FRF RTG] no public screen");goto fail;}
  if(!scr->RastPort.BitMap || !GetCyberMapAttr(scr->RastPort.BitMap,CYBRMATTR_ISCYBERGFX)){
   puts("[FRF RTG] Workbench public screen is not RTG");goto fail;
  }
  /* Fixed-size interior avoids incorrect writes into window borders.
     No screenmode switch or private fullscreen screen. */
  if(sw+24>scr->Width||sh+36>scr->Height){
   puts("[FRF RTG] public screen too small for requested scale");goto fail;
  }
  win=OpenWindowTags(NULL,WA_PubScreen,scr,WA_Left,20,WA_Top,20,
    WA_InnerWidth,sw,WA_InnerHeight,sh,WA_Title,"PicoDrive RTG",
    WA_DragBar,TRUE,WA_CloseGadget,TRUE,WA_DepthGadget,TRUE,
    WA_SizeGadget,FALSE,WA_Activate,TRUE,WA_SmartRefresh,TRUE,
    WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_RAWKEY|IDCMP_INACTIVEWINDOW,TAG_DONE);
 }else{
  mode=BestCModeIDTags(CYBRBIDTG_NominalWidth,sw,
    CYBRBIDTG_NominalHeight,sh,CYBRBIDTG_Depth,16,TAG_DONE);
  if(mode==INVALID_ID){puts("[FRF RTG] no matching screen mode");goto fail;}
  scr=OpenScreenTags(NULL,SA_DisplayID,mode,SA_Width,sw,SA_Height,sh,
    SA_Depth,16,SA_Quiet,TRUE,SA_ShowTitle,FALSE,TAG_DONE);
  if(!scr){puts("[FRF RTG] screen creation failed");goto fail;}
  owns_screen=1;
  if(!scr->RastPort.BitMap || !GetCyberMapAttr(scr->RastPort.BitMap,CYBRMATTR_ISCYBERGFX)){
   puts("[FRF RTG] not a chunky RTG screen");goto fail;
  }
  win=OpenWindowTags(NULL,WA_CustomScreen,scr,WA_Left,0,WA_Top,0,
    WA_Width,sw,WA_Height,sh,WA_Borderless,TRUE,WA_Backdrop,TRUE,
    WA_Activate,TRUE,WA_IDCMP,IDCMP_RAWKEY|IDCMP_INACTIVEWINDOW,TAG_DONE);
 }
 if(!win){puts("[FRF RTG] cannot open Intuition window");goto fail;}
 buf=(UBYTE*)malloc(sw*sh*3);
 if(!buf){puts("[FRF RTG] framebuffer allocation failed");goto fail;}
 printf("[FRF RTG] %s %ux%u scale=%u scanlines=%u\n",
   windowed?"Workbench window":"fullscreen",sw,sh,k,scan);fflush(stdout);
 return 1;
fail:frf_pd_rtg_shutdown();return 0;
}

/* Intuition RAWKEY codes: arrows 4C-4F, Return 44, modifiers 61/63/64.
 * Up events have bit 7 set. All keyboard state belongs to our RTG window.
 * Call before retro input queries, not only when a video frame is drawn. */
void frf_pd_rtg_poll_events(void){
 struct IntuiMessage *m;
 if(!win || !win->UserPort) return;
 while((m=(struct IntuiMessage*)GetMsg(win->UserPort))){
  ULONG cls=m->Class;
  UWORD code=m->Code;
  if(cls==IDCMP_RAWKEY){
   rtg_keys[code&0x7f]=(code&0x80)?0:1;
  }else if(cls==IDCMP_CLOSEWINDOW){
   rtg_close_requested=1;
  }else if(cls==IDCMP_INACTIVEWINDOW){
   memset(rtg_keys,0,sizeof(rtg_keys));
  }
  ReplyMsg((struct Message*)m);
 }
}
int frf_pd_rtg_key_state(unsigned id){
 switch(id){
 case RETRO_DEVICE_ID_JOYPAD_UP: return rtg_keys[0x4c];
 case RETRO_DEVICE_ID_JOYPAD_DOWN: return rtg_keys[0x4d];
 case RETRO_DEVICE_ID_JOYPAD_RIGHT: return rtg_keys[0x4e];
 case RETRO_DEVICE_ID_JOYPAD_LEFT: return rtg_keys[0x4f];
 case RETRO_DEVICE_ID_JOYPAD_B: return rtg_keys[0x61]; /* Right Shift */
 case RETRO_DEVICE_ID_JOYPAD_A: return rtg_keys[0x64]; /* Left Alt -> MD C */
 case RETRO_DEVICE_ID_JOYPAD_X: return 0;
 case RETRO_DEVICE_ID_JOYPAD_Y: return rtg_keys[0x63]; /* Ctrl -> MD A */
 case RETRO_DEVICE_ID_JOYPAD_L: return 0;
 case RETRO_DEVICE_ID_JOYPAD_R: return 0;
 case RETRO_DEVICE_ID_JOYPAD_START: return rtg_keys[0x44]; /* Enter */
 /* Mega Drive C is RetroPad A; see mapping in input.c */
 default: return 0;
 }
}

static int rtg_direct565(const void *pixels,unsigned w,unsigned h,unsigned pitch){
 APTR lock; APTR addr=0; ULONG fmt=0,bpr=0,bpp=0;
 unsigned x,y,ox,oy; const UBYTE *src=(const UBYTE*)pixels;
 UBYTE *dst;
 if(k!=1 || scan || !win || !scr || !scr->RastPort.BitMap)return 0;
 /* Direct screen writes bypass window layer clipping: permit only top window. */
 if(windowed && scr->FirstWindow!=win)return 0;
 ox=(unsigned)(win->LeftEdge+(windowed?win->BorderLeft:0)+(sw-w)/2);
 oy=(unsigned)(win->TopEdge+(windowed?win->BorderTop:0)+(sh-h)/2);
 if(ox+w>scr->Width||oy+h>scr->Height)return 0;
 lock=LockBitMapTags(scr->RastPort.BitMap,LBMI_BASEADDRESS,(ULONG)&addr,
   LBMI_BYTESPERROW,(ULONG)&bpr,LBMI_BYTESPERPIX,(ULONG)&bpp,
   LBMI_PIXFMT,(ULONG)&fmt,TAG_DONE);
 if(!lock)return 0;
 if(!addr||bpp!=2||(fmt!=PIXFMT_RGB16&&fmt!=PIXFMT_RGB16PC)||
    bpr<(ox+w)*2){UnLockBitMap(lock);return 0;}
 dst=(UBYTE*)addr+oy*bpr+ox*2;
 if(fmt==PIXFMT_RGB16){
  for(y=0;y<h;y++)memcpy(dst+y*bpr,src+y*pitch,w*2);
 }else{
  for(y=0;y<h;y++){
   const UBYTE *s=src+y*pitch;
   UBYTE *d=dst+y*bpr;
   for(x=0;x<w;x++){d[x*2]=s[x*2+1];d[x*2+1]=s[x*2];}
  }
 }
 UnLockBitMap(lock);
 return 1;
}
int frf_pd_rtg_present(const void *p,unsigned w,unsigned h,unsigned pitch){
 unsigned x,y,i,j,left,top;
 const UBYTE *src=(const UBYTE*)p;
 frf_pd_rtg_poll_events();
 if(rtg_close_requested){puts("[FRF RTG] close requested");return 0;}
 if(!p||!buf||!win||!w||!h||w>512||h>512||
    pitch<w*2||w*k>sw||h*k>sh)return 0;
 if(rtg_direct565(p,w,h,pitch))return 1;
 left=(sw-w*k)/2;top=(sh-h*k)/2;
 memset(buf,0,sw*sh*3);
 for(y=0;y<h;y++){
  const UWORD *row=(const UWORD*)(src+y*pitch);
  for(i=0;i<k;i++){
   UBYTE *dst=buf+((top+y*k+i)*sw+left)*3;
   for(x=0;x<w;x++){
    unsigned v=row[x];
    if(scan&&i==k-1){unsigned f=100-scan;
      v=((((v>>11)&31)*f/100)<<11)|
        ((((v>>5)&63)*f/100)<<5)|((v&31)*f/100);
    }
    for(j=0;j<k;j++){
     unsigned n=(x*k+j)*3;
     dst[n]=(UBYTE)(((v>>11)&31)*255/31);
     dst[n+1]=(UBYTE)(((v>>5)&63)*255/63);
     dst[n+2]=(UBYTE)((v&31)*255/31);
    }
   }
  }
 }
 WritePixelArray(buf,0,0,sw*3,win->RPort,
   windowed?win->BorderLeft:0,windowed?win->BorderTop:0,
   sw,sh,RECTFMT_RGB);
 return 1;
}
