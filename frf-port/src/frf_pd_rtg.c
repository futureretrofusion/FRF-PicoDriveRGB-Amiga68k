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
static unsigned direct_w,direct_h,direct_scale;
static int mode720p;
static UBYTE rtg_keys[128];
static int rtg_close_requested=0;
void frf_pd_rtg_config(int s,int l){k=(s>=0&&s<=4)?s:2;scan=(l==25||l==50||l==75)?l:0;}
void frf_pd_rtg_mode_720p(int on){mode720p=(on!=0);}
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
 direct_w=direct_h=direct_scale=0;
 sw=(mode720p||k==0)?1280:(k>=3?1280:(k==2?640:320));
 sh=(mode720p||k==0)?720:(k>=4?1080:(k>=3?720:(k==2?480:240)));
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
   if((code&0x7f)==0x45 && !(code&0x80)) rtg_close_requested=1;
  }else if(cls==IDCMP_CLOSEWINDOW){
   rtg_close_requested=1;
  }else if(cls==IDCMP_INACTIVEWINDOW){
   memset(rtg_keys,0,sizeof(rtg_keys));
  }
  ReplyMsg((struct Message*)m);
 }
}
int frf_pd_rtg_quit_requested(void){return rtg_close_requested;}
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

/* Accelerated native RGB565 integer scaler. This is conservative about window
 * ownership: bypass Intuition clipping only when our window is frontmost. */
static unsigned rtg_scale_for(unsigned w,unsigned h)
{
 int i;
 if(k) return k;
 for(i=4;i>=1;--i) if(w*(unsigned)i<=sw && h*(unsigned)i<=sh) return (unsigned)i;
 return 0;
}
static UWORD rtg_scan_rgb565(UWORD v)
{
 unsigned f=100U-scan;
 return (UWORD)(((((v>>11)&31U)*f/100U)<<11)|
                (((((v>>5)&63U)*f/100U)<<5))|((v&31U)*f/100U));
}
static int rtg_direct565(const void *pixels,unsigned w,unsigned h,unsigned pitch)
{
 APTR lock; APTR addr=0;ULONG fmt=0,bpr=0,bpp=0;
 unsigned scale=rtg_scale_for(w,h),origin_x,origin_y,ox,oy,x,y,i,j;
 LONG wx,wy;
 const UBYTE *src=(const UBYTE*)pixels;
 UBYTE *dst;
 if(!scale || w*scale>sw || h*scale>sh || !win || !scr || !scr->RastPort.BitMap) return 0;
 if(windowed && scr->FirstWindow!=win) {direct_w=0;return 0;}
 wx=(LONG)win->LeftEdge+(windowed?win->BorderLeft:0);
 wy=(LONG)win->TopEdge+(windowed?win->BorderTop:0);
 if(wx<0 || wy<0) {direct_w=0;return 0;}
 origin_x=(unsigned)wx;
 origin_y=(unsigned)wy;
 ox=origin_x+(sw-w*scale)/2U;
 oy=origin_y+(sh-h*scale)/2U;
 if(origin_x+sw>scr->Width || origin_y+sh>scr->Height) return 0;
 lock=LockBitMapTags(scr->RastPort.BitMap,
     LBMI_BASEADDRESS,(ULONG)&addr,LBMI_BYTESPERROW,(ULONG)&bpr,
     LBMI_BYTESPERPIX,(ULONG)&bpp,LBMI_PIXFMT,(ULONG)&fmt,TAG_DONE);
 if(!lock) return 0;
 if(!addr || bpp!=2 || (fmt!=PIXFMT_RGB16 && fmt!=PIXFMT_RGB16PC) ||
    bpr < (origin_x+sw)*2U) {UnLockBitMap(lock);return 0;}
 /* Clear unused letterboxing only at initialization/geometry transitions. */
 if(direct_w!=w || direct_h!=h || direct_scale!=scale) {
     for(y=0;y<sh;y++)
         memset((UBYTE*)addr+(origin_y+y)*bpr+origin_x*2U,0,sw*2U);
     direct_w=w;direct_h=h;direct_scale=scale;
 }
 dst=(UBYTE*)addr+oy*bpr+ox*2U;
 if(scale==1 && !scan && fmt==PIXFMT_RGB16) {
     for(y=0;y<h;y++) memcpy(dst+y*bpr,src+y*pitch,w*2U);
 } else {
     for(y=0;y<h;y++) {
         const UWORD *sr=(const UWORD*)(src+y*pitch);
         for(i=0;i<scale;i++) {
             int dark=scan && (scale>1 ? i==scale-1 : (y&1U)!=0);
             UBYTE *dr=dst+(y*scale+i)*bpr;
             if(i>0 && !dark) {memcpy(dr,dr-bpr,w*scale*2U);continue;}
             for(x=0;x<w;x++) {
                 UWORD v=sr[x];
                 if(dark) v=rtg_scan_rgb565(v);
                 for(j=0;j<scale;j++) {
                     unsigned at=(x*scale+j)*2U;
                     if(fmt==PIXFMT_RGB16) {
                         dr[at]=(UBYTE)(v>>8);dr[at+1]=(UBYTE)v;
                     } else {
                         dr[at]=(UBYTE)v;dr[at+1]=(UBYTE)(v>>8);
                     }
                 }
             }
         }
     }
 }
 UnLockBitMap(lock);
 return 1;
}
int frf_pd_rtg_present(const void *p,unsigned w,unsigned h,unsigned pitch)
{
 unsigned scale=rtg_scale_for(w,h),x,y,i,j,left,top;
 const UBYTE *src=(const UBYTE*)p;
 frf_pd_rtg_poll_events();
 if(rtg_close_requested) {puts("[FRF RTG] quit requested");return 0;}
 if(!p || !buf || !win || !w || !h || !scale || w>512 || h>512 ||
    pitch<w*2U || w*scale>sw || h*scale>sh) return 0;
 if(rtg_direct565(p,w,h,pitch)) return 1;
 left=(sw-w*scale)/2U;top=(sh-h*scale)/2U;
 memset(buf,0,sw*sh*3U);
 for(y=0;y<h;y++) {
     const UWORD *row=(const UWORD*)(src+y*pitch);
     for(i=0;i<scale;i++) {
         UBYTE *dr=buf+((top+y*scale+i)*sw+left)*3U;
         int dark=scan && (scale>1?i==scale-1:(y&1U)!=0);
         for(x=0;x<w;x++) {
             UWORD v=row[x];
             if(dark) v=rtg_scan_rgb565(v);
             for(j=0;j<scale;j++) {
                 unsigned n=(x*scale+j)*3U;
                 dr[n]=(UBYTE)(((v>>11)&31U)*255U/31U);
                 dr[n+1]=(UBYTE)(((v>>5)&63U)*255U/63U);
                 dr[n+2]=(UBYTE)((v&31U)*255U/31U);
             }
         }
     }
 }
 WritePixelArray(buf,0,0,sw*3U,win->RPort,
     windowed?win->BorderLeft:0,windowed?win->BorderTop:0,
     sw,sh,RECTFMT_RGB);
 return 1;
}
