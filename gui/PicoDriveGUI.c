/* FRF PicoDriveGUI v0.1 - separate native AmigaOS 3.x GadTools launcher.
 * Build: m68k-amigaos-gcc -mcrt=clib2 -m68020 -O2 ... -o PicoDriveGUI
 * No ROMs distributed. Preferences are saved beside launcher if writable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/dos.h>

#define W 590
#define H 225
#define MAXCMD 2048
enum {ID_PAGE=1, ID_ROM, ID_BROWSE, ID_EXE, ID_EXEBROWSE,
      ID_OPT0=20, ID_OPT1, ID_OPT2, ID_OPT3, ID_OPT4, ID_OPT5,
      ID_RUN=80, ID_SAVE, ID_DEFAULT, ID_QUIT};
static const char *pages[]={"Video","Performance","Input","Audio",NULL};
static const char *out[]={"HAM6","HAM8","AGA 64","AGA 128","AGA 256","RTG",NULL};
static const char *rtgmode[]={"Native","1280x720",NULL};
static const char *scale[]={"Auto","1x","2x","3x","4x",NULL};
static const char *scan[]={"Off","25%","50%","75%",NULL};
static const char *yesno[]={"Off","On",NULL};
static const char *renderer[]={"Accurate","Good","Fast",NULL};
static const char *frameskip[]={"Off","Auto","Manual",NULL};
static const char *threshold[]={"20","25","33","40","50","60",NULL};
static const char *pad[]={"3 buttons","6 buttons",NULL};
static const char *joymode[]={"Auto","Direct","Lowlevel","Gameport",NULL};
static const char *joyport[]={"Port 2","Port 1",NULL};
static const char *audiorate[]={"44100 Hz","22050 Hz",NULL};
static const char *page_labels[4][6]={
 {"Output mode","RTG resolution","Integer scale","Scanlines","RTG window","HAM6 double buffer"},
 {"Renderer","Frameskip","Threshold","ARM coprocessor",NULL,NULL},
 {"Mega Drive pad","Joystick backend","Amiga port","Joystick debug",NULL,NULL},
 {"Sound","Sample rate",NULL,NULL,NULL,NULL}
};
static const char **choices[4][6]={
 {out,rtgmode,scale,scan,yesno,yesno},
 {renderer,frameskip,threshold,yesno,NULL,NULL},
 {pad,joymode,joyport,yesno,NULL,NULL},
 {yesno,audiorate,NULL,NULL,NULL,NULL}
};
static int values[4][6]={{0,0,2,0,0,0},{0,0,2,1,0,0},{0,0,0,0,0,0},{1,0,0,0,0,0}};
static int page=0,done=0;
static char rom[512]="",exe[512]="PicoDriveRGB_v0.2.21_JOYFIX";
static struct Screen *screen;
static struct Window *win;
static struct Gadget *gadgets,*g[100];
static APTR visual;
struct Library *GadToolsBase,*AslBase;
struct IntuitionBase *IntuitionBase;
static struct NewGadget ng;

static void defaults(void) {
 static const int initial[4][6]={{0,0,2,0,0,0},{0,0,2,1,0,0},{0,0,0,0,0,0},{1,0,0,0,0,0}};
 memcpy(values,initial,sizeof(values));
}
static void get_string(unsigned id,char *dst,size_t cap) {
 if(g[id] && ((struct StringInfo *)g[id]->SpecialInfo)) {
    const char *s=(const char *)((struct StringInfo *)g[id]->SpecialInfo)->Buffer;
    if(s) {strncpy(dst,s,cap-1);dst[cap-1]=0;}
 }
}
static void capture_paths(void) {
 get_string(ID_ROM,rom,sizeof(rom));
 get_string(ID_EXE,exe,sizeof(exe));
}
static void close_window(void) {
 if(win) {CloseWindow(win);win=NULL;}
 if(gadgets) {FreeGadgets(gadgets);gadgets=NULL;}
 memset(g,0,sizeof(g));
}
static struct Gadget *add(unsigned kind,unsigned id,int x,int y,int w,int h,
                          const char *label,const char **items,int active,const char *initial,
                          struct Gadget *prev) {
 memset(&ng,0,sizeof(ng));
 ng.ng_LeftEdge=x;ng.ng_TopEdge=y;ng.ng_Width=w;ng.ng_Height=h;
 ng.ng_GadgetText=(UBYTE *)label;ng.ng_TextAttr=screen->Font;
 ng.ng_GadgetID=id;ng.ng_Flags=PLACETEXT_LEFT;ng.ng_VisualInfo=visual;
 if(kind==CYCLE_KIND)
     g[id]=CreateGadget(kind,prev,&ng,GTCY_Labels,items,GTCY_Active,active,TAG_DONE);
 else if(kind==STRING_KIND)
     g[id]=CreateGadget(kind,prev,&ng,GTST_String,initial,GTST_MaxChars,511,TAG_DONE);
 else
     g[id]=CreateGadget(kind,prev,&ng,TAG_DONE);
 return g[id];
}
static int open_window(void) {
 int i,n=0;
 struct Gadget *tail;
 memset(g,0,sizeof(g));
 gadgets=CreateContext(&tail);
 if(!gadgets) return 0;
 /* GadTools coordinates are screen-relative; reserve the title bar. */
 tail=add(CYCLE_KIND,ID_PAGE,120,21,220,15,"Settings page",pages,page,NULL,tail);
 if(!tail)goto fail;
 tail=add(STRING_KIND,ID_ROM,120,44,350,14,"Game ROM",NULL,0,rom,tail);
 if(!tail)goto fail;
 tail=add(BUTTON_KIND,ID_BROWSE,482,44,90,15,"Choose ROM",NULL,0,NULL,tail);
 if(!tail)goto fail;
 tail=add(STRING_KIND,ID_EXE,120,65,350,14,"Emulator",NULL,0,exe,tail);
 if(!tail)goto fail;
 tail=add(BUTTON_KIND,ID_EXEBROWSE,482,65,90,15,"Choose EXE",NULL,0,NULL,tail);
 if(!tail)goto fail;
 for(i=0;i<6;i++) {
   if(!choices[page][i]) continue;
   tail=add(CYCLE_KIND,ID_OPT0+i,210,88+i*18,280,15,
            page_labels[page][i],choices[page][i],values[page][i],NULL,tail);
   if(!tail)goto fail;
   n++;
 }
 tail=add(BUTTON_KIND,ID_RUN,20,207,108,16,"Launch game",NULL,0,NULL,tail);
 if(!tail)goto fail;
 tail=add(BUTTON_KIND,ID_SAVE,149,207,108,16,"Save prefs",NULL,0,NULL,tail);
 if(!tail)goto fail;
 tail=add(BUTTON_KIND,ID_DEFAULT,278,207,108,16,"Defaults",NULL,0,NULL,tail);
 if(!tail)goto fail;
 tail=add(BUTTON_KIND,ID_QUIT,407,207,108,16,"Quit",NULL,0,NULL,tail);
 if(!tail)goto fail;
 win=OpenWindowTags(NULL,WA_Left,20,WA_Top,3,WA_Width,W,WA_Height,H+20,
        WA_Title,"FRF PicoDriveGUI v0.1 - Amiga Mega Drive",
        WA_PubScreen,screen,WA_Gadgets,gadgets,
        WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_GADGETUP|IDCMP_REFRESHWINDOW,
        WA_Flags,WFLG_CLOSEGADGET|WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_ACTIVATE|
                 WFLG_SMART_REFRESH,TAG_DONE);
 if(!win)goto fail;
 GT_RefreshWindow(win,NULL);
 (void)n;
 return 1;
fail:
 close_window();return 0;
}
static void refresh(void) {capture_paths();close_window();open_window();}
static void browse(int id) {
 struct FileRequester *req;
 char path[512];
 const char *cur=(id==ID_ROM)?rom:exe;
 const char *slash=strrchr(cur,'/');
 const char *colon=strrchr(cur,':');
 const char *last=slash;
 if(colon && (!last || colon>last))last=colon;
 size_t len=last?(size_t)(last-cur+1):0;
 char drawer[512];
 if(len>=sizeof(drawer))len=sizeof(drawer)-1;
 memcpy(drawer,cur,len);drawer[len]=0;
 req=(struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,TAG_DONE);
 if(!req)return;
 if(AslRequestTags(req,ASLFR_TitleText,id==ID_ROM?"Select Mega Drive ROM":"Select PicoDriveRGB executable",
          ASLFR_InitialDrawer,drawer,ASLFR_InitialFile,last?last+1:cur,
          ASLFR_DoPatterns,FALSE,TAG_DONE)) {
   const char *d=(const char *)req->fr_Drawer,*f=(const char *)req->fr_File;
   size_t n=strlen(d);
   snprintf(path,sizeof(path),"%s%s%s",d,(n && d[n-1]!=':' && d[n-1]!='/')?"/":"",f);
   if(id==ID_ROM) {strncpy(rom,path,sizeof(rom)-1);rom[sizeof(rom)-1]=0;}
   else {strncpy(exe,path,sizeof(exe)-1);exe[sizeof(exe)-1]=0;}
   close_window();if(!open_window())done=1;
 }
 FreeAslRequest(req);
}
static const char *getoptname(int p,int o) {
 static const char *names[4][6]={
  {"output","rtg-mode","scale","scanlines","window","dbuf"},
  {"renderer","frameskip","threshold","frfcp",NULL,NULL},
  {"pad","joyinput","joyport","joydebug",NULL,NULL},
  {"sound","audio-rate",NULL,NULL,NULL,NULL}};
 return names[p][o];
}
static const char *getoptvalue(int p,int o) {
 static const char *outs[]={"ham6","ham8","aga64","aga128","aga256","rtg"};
 static const char *modes[]={"native","720p"};
 static const char *scales[]={"auto","1","2","3","4"};
 static const char *scans[]={"0","25","50","75"};
 static const char *yn[]={"off","on"};
 static const char *renders[]={"accurate","good","fast"};
 static const char *skips[]={"off","auto","manual"};
 static const char *thres[]={"20","25","33","40","50","60"};
 static const char *pads[]={"3","6"};
 static const char *joys[]={"auto","direct","lowlevel","gameport"};
 static const char *ports[]={"2","1"};
 static const char *rates[]={"44100","22050"};
 int v=values[p][o];
 if(p==0) {switch(o){case 0:return outs[v];case 1:return modes[v];case 2:return scales[v];case 3:return scans[v];default:return yn[v];}}
 if(p==1) {switch(o){case 0:return renders[v];case 1:return skips[v];case 2:return thres[v];default:return yn[v];}}
 if(p==2) {switch(o){case 0:return pads[v];case 1:return joys[v];case 2:return ports[v];default:return yn[v];}}
 return o==0?yn[v]:rates[v];
}
static int append(char *cmd,size_t cap,const char *s) {
 size_t n=strlen(cmd),m=strlen(s);
 if(n+m+1>=cap)return 0;
 memcpy(cmd+n,s,m+1);return 1;
}
static int quote(char *cmd,size_t cap,const char *arg) {
 const unsigned char *p=(const unsigned char *)arg;
 if(!append(cmd,cap,"\""))return 0;
 while(*p) {
   char a[4]={0};
   /* AmigaDOS quoted strings: '*' escapes quotes and '*' itself. */
   if(*p=='"'||*p=='*'){a[0]='*';a[1]=*p++;}else {a[0]=*p++;}
   if(!append(cmd,cap,a))return 0;
 }
 return append(cmd,cap,"\"");
}
static void message(const char *msg) {
 struct EasyStruct es={sizeof(struct EasyStruct),0,(STRPTR)"PicoDriveGUI",(STRPTR)msg,(STRPTR)"OK"};
 EasyRequestArgs(win,&es,NULL,NULL);
}
static void launch(void) {
 char cmd[MAXCMD],tmp[96];
 int p,o;
 capture_paths();
 if(!rom[0]||!exe[0]){message("Choose a Mega Drive ROM and PicoDriveRGB executable.");return;}
 cmd[0]=0;
 if(!quote(cmd,sizeof(cmd),exe)){message("Emulator path too long.");return;}
 for(p=0;p<4;p++)for(o=0;o<6;o++) {
    if(!getoptname(p,o))continue;
    if((p==0&&o==4)||(p==2&&o==3)||(p==3&&o==0)) {
       if((p==0&&o==4&&values[p][o])||(p==2&&o==3&&values[p][o])||
          (p==3&&o==0&&!values[p][o])) {
          const char *flag=p==0?" --window":p==2?" --joydebug":" --nosound";
          if(!append(cmd,sizeof(cmd),flag)){message("Command too long.");return;}
       }
    } else {
       snprintf(tmp,sizeof(tmp)," --%s=%s",getoptname(p,o),getoptvalue(p,o));
       if(!append(cmd,sizeof(cmd),tmp)){message("Command too long.");return;}
    }
 }
 if(!append(cmd,sizeof(cmd)," ")||!quote(cmd,sizeof(cmd),rom)){message("ROM path too long.");return;}
 /* Shut the launcher window before opening a full-screen Amiga display.
  * The child returns to the launcher when it exits (ESC / Ctrl+C). */
 close_window();
 printf("[PicoDriveGUI] launching: %s\n",cmd);
 fflush(stdout);
 {int result=system(cmd);printf("[PicoDriveGUI] emulator exit status: %d\n",result);}
 if(!open_window())done=1;
}
static const char *prefs="PicoDriveGUI.prefs";
static void save(void) {
 FILE *f;int p,o;
 capture_paths();
 f=fopen(prefs,"w");
 if(!f){message("Cannot write PicoDriveGUI.prefs in current directory.");return;}
 fprintf(f,"PicoDriveGUI 1\n%s\n%s\n",rom,exe);
 for(p=0;p<4;p++){for(o=0;o<6;o++)fprintf(f,"%d ",values[p][o]);fputc('\n',f);}
 fclose(f);message("Preferences saved to PicoDriveGUI.prefs");
}
static void load(void) {
 FILE *f=fopen(prefs,"r");char line[512];int p,o;
 if(!f)return;
 if(!fgets(line,sizeof(line),f)||strncmp(line,"PicoDriveGUI 1",14)){fclose(f);return;}
 if(fgets(rom,sizeof(rom),f))rom[strcspn(rom,"\r\n")]=0;
 if(fgets(exe,sizeof(exe),f))exe[strcspn(exe,"\r\n")]=0;
 for(p=0;p<4;p++) {
   if(!fgets(line,sizeof(line),f))break;
   {char *s=line;for(o=0;o<6;o++){int v;char *end;
       v=(int)strtol(s,&end,10);if(s==end)break;
       if(v>=0 && choices[p][o]) {
          int max=0;while(choices[p][o][max])max++;
          if(v<max)values[p][o]=v;
       }
       s=end;
   }}
 }
 fclose(f);
}
int main(void) {
 ULONG signal;
 struct IntuiMessage *im;
 int i;
 if(!(IntuitionBase=(struct IntuitionBase *)OpenLibrary("intuition.library",37)))return 20;
 if(!(GadToolsBase=OpenLibrary("gadtools.library",37))){CloseLibrary((struct Library *)IntuitionBase);return 20;}
 if(!(AslBase=OpenLibrary("asl.library",37))){CloseLibrary(GadToolsBase);CloseLibrary((struct Library *)IntuitionBase);return 20;}
 screen=LockPubScreen(NULL);
 if(!screen){CloseLibrary(AslBase);CloseLibrary(GadToolsBase);CloseLibrary((struct Library *)IntuitionBase);return 20;}
 visual=GetVisualInfoA(screen,NULL);
 if(!visual){UnlockPubScreen(NULL,screen);CloseLibrary(AslBase);CloseLibrary(GadToolsBase);CloseLibrary((struct Library *)IntuitionBase);return 20;}
 load();
 if(!open_window()){done=1;}
 while(!done && win) {
   signal=Wait(1UL<<win->UserPort->mp_SigBit);
   (void)signal;
   while(win && (im=GT_GetIMsg(win->UserPort))) {
      ULONG cls=im->Class;
      UWORD code=im->Code;
      UWORD id=im->IAddress?((struct Gadget *)im->IAddress)->GadgetID:0;
      GT_ReplyIMsg(im);
      if(cls==IDCMP_CLOSEWINDOW){done=1;break;}
      if(cls==IDCMP_REFRESHWINDOW){GT_RefreshWindow(win,NULL);continue;}
      if(cls!=IDCMP_GADGETUP)continue;
      if(id==ID_PAGE) {
         capture_paths();page=code;
         if(page<0||page>3)page=0;
         refresh();break;
      }
      if(id>=ID_OPT0&&id<=ID_OPT5) {
         i=(int)id-ID_OPT0;
         if(choices[page][i])values[page][i]=(int)code;
      }
      if(id==ID_BROWSE||id==ID_EXEBROWSE){capture_paths();browse(id);break;}
      if(id==ID_RUN){launch();break;}
      if(id==ID_SAVE)save();
      if(id==ID_DEFAULT){capture_paths();defaults();refresh();break;}
      if(id==ID_QUIT){done=1;break;}
   }
 }
 close_window();
 FreeVisualInfo(visual);
 UnlockPubScreen(NULL,screen);
 CloseLibrary(AslBase);
 CloseLibrary(GadToolsBase);
 CloseLibrary((struct Library *)IntuitionBase);
 return 0;
}
