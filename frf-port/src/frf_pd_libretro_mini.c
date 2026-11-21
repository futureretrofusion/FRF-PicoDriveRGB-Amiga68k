/*
 * FRF PicoDriveRGB Emu68 V0.2.16C - DBUF dynamic-geometry repair
 *
 * Real-ROM milestone:
 *   PicoDrive -> RGB565 -> HAM6 -> CHIP -> Denise RGB
 *   PicoDrive PCM -> AHI signed-16 stereo
 *   lowlevel.library/gameport.device -> PicoDrive RetroPad input
 *   audio-master low/high watermark governor skips presentation only
 *   explicit + atexit custom-screen teardown
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <proto/exec.h>

#include <libretro.h>

#define FRF_PROJECT_NAME "Future Retro Fusion PicoDriveRGB"
#define FRF_PROJECT_VERSION "0.2.17"
#define FRF_PROJECT_TARGET "AmigaOS 68k / HAM6 or CyberGraphX RTG"

#include "frf_pd_ham6.h"
#include "frf_pd_rtg.h"
#include "frf_pd_audio.h"
#include "frf_pd_input.h"
#include "frf_pd_timer.h"
#include "frf_pd_coproc_client.h"

static volatile int g_quit;
static unsigned long g_presented;
static unsigned long g_skipped;
static unsigned g_last_w,g_last_h;
static int g_warned_format;
static int g_nosound;
static const char *g_frameskip="disabled";
static const char *g_threshold="33";
static const char *g_pad="3 button pad";
static const char *g_renderer="accurate";
static const char *g_audio_rate="44100";
static int g_frfcp_enabled=1;
static int g_dbuf_enabled=0;
static int g_backend_rtg=0, g_rtg_scale=2, g_rtg_scan=0, g_rtg_window=0;
static int g_timer_ok;
static void (*g_audio_status_cb)(bool,unsigned,bool);
static int g_audio_status_started;
static int g_cleanup_done;
static double g_core_fps=60.0;
static unsigned g_display_hz=50U;
static double g_rate_accum=0.0;
static unsigned long g_rate_skipped;

#define FRF_AUDIO_GUARD_CRITICAL_FRAMES 1536U
#define FRF_AUDIO_GUARD_LOW_FRAMES      4096U
#define FRF_AUDIO_GUARD_HIGH_FRAMES     8192U
static int g_audio_refill_active;
static unsigned g_audio_guard_phase;
static unsigned g_audio_guard_critical_run;
static int g_audio_guard_unslept_this_frame;
static unsigned long g_audio_guard_skipped;
static unsigned long g_audio_guard_presented;
static unsigned long g_audio_guard_critical_skipped;
static unsigned long g_audio_refill_entries;
static unsigned long g_audio_refill_core_frames;
static unsigned g_audio_min_buffered=0xffffffffU;

static void audio_guard_update(void)
{
    unsigned buffered;

    if(g_nosound || !frf_pd_audio_active()) {
        g_audio_refill_active=0;
        return;
    }

    buffered=frf_pd_audio_buffered_frames();
    if(buffered<g_audio_min_buffered) g_audio_min_buffered=buffered;

    if(!g_audio_refill_active) {
        if(buffered<FRF_AUDIO_GUARD_LOW_FRAMES) {
            g_audio_refill_active=1;
            g_audio_guard_phase=0;
            g_audio_guard_critical_run=0;
            ++g_audio_refill_entries;
            if(g_audio_refill_entries<=4UL || (g_audio_refill_entries%25UL)==0UL)
                printf("[FRF PD AUDIO-GUARD] MICRO enter buffered=%u critical=%u low=%u high=%u; interleaved recovery\n",
                       buffered,FRF_AUDIO_GUARD_CRITICAL_FRAMES,
                       FRF_AUDIO_GUARD_LOW_FRAMES,FRF_AUDIO_GUARD_HIGH_FRAMES);
        }
    } else if(buffered>=FRF_AUDIO_GUARD_HIGH_FRAMES) {
        g_audio_refill_active=0;
        g_audio_guard_phase=0;
        g_audio_guard_critical_run=0;
        if(g_audio_refill_entries<=4UL || (g_audio_refill_entries%25UL)==0UL)
            printf("[FRF PD AUDIO-GUARD] MICRO exit buffered=%u; normal cadence resumed\n",buffered);
    }
}

static void frf_cleanup(void)
{
    if(g_cleanup_done) return;
    g_cleanup_done=1;
    if(g_audio_status_cb) g_audio_status_cb(false,0,false);
    g_audio_status_cb=NULL;
    /* V0.2.6 safety order: stop worker-owned devices and timer before
     * releasing shared memory/libraries, then restore the custom screen. */
    frf_pd_audio_shutdown();
    frf_pd_timer_shutdown();
    frfcp_shutdown();
    frf_pd_input_shutdown();
    if(g_backend_rtg) frf_pd_rtg_shutdown();
    else frf_pd_ham6_shutdown();
}

static void frf_log(enum retro_log_level level,const char *fmt,...)
{
    va_list ap;
    const char *pfx="INFO";
    if(level==RETRO_LOG_WARN) pfx="WARN";
    else if(level==RETRO_LOG_ERROR) pfx="ERROR";
    else if(level==RETRO_LOG_DEBUG) pfx="DEBUG";
    printf("[PicoDrive %s] ",pfx);
    va_start(ap,fmt); vprintf(fmt,ap); va_end(ap);
}

static void notify_audio_status(void)
{
    unsigned occ;
    bool under;
    if(!g_audio_status_cb || !frf_pd_audio_active()) return;
    occ=frf_pd_audio_occupancy_percent();
    under=frf_pd_audio_underrun_likely()?true:false;
    /* Prime a little audio first. Avoid telling AUTO frameskip that the empty
     * startup ring is an emergency before PicoDrive has produced any PCM. */
    if(!g_audio_status_started) {
        if(occ<8U) return;
        g_audio_status_started=1;
    }
    g_audio_status_cb(true,occ,under);
}

static bool frf_env(unsigned cmd,void *data)
{
    switch(cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        {
            enum retro_pixel_format fmt=*(enum retro_pixel_format *)data;
            if(fmt!=RETRO_PIXEL_FORMAT_RGB565) {
                if(!g_warned_format) {
                    printf("FRF PicoDriveRGB: unsupported pixel format %u; RGB565 required\n",(unsigned)fmt);
                    g_warned_format=1;
                }
                return false;
            }
            return true;
        }
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *(bool *)data=true; return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *(const char **)data="."; return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            struct retro_variable *v=(struct retro_variable *)data;
            if(!v || !v->key) return false;
            if(!strcmp(v->key,"picodrive_frameskip")) { v->value=g_frameskip; return true; }
            if(!strcmp(v->key,"picodrive_frameskip_threshold")) { v->value=g_threshold; return true; }
            if(!strcmp(v->key,"picodrive_sound_rate")) { v->value=g_audio_rate; return true; }
            if(!strcmp(v->key,"picodrive_renderer")) { v->value=g_renderer; return true; }
            if(!strcmp(v->key,"picodrive_input1")) { v->value=g_pad; return true; }
            if(!strcmp(v->key,"picodrive_input2")) { v->value=g_pad; return true; }
            v->value=NULL;
            return false; /* use core default for everything else */
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *(bool *)data=false; return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            ((struct retro_log_callback *)data)->log=frf_log; return true;
        case RETRO_ENVIRONMENT_SHUTDOWN:
            g_quit=1; return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE:
        {
            const struct retro_message *m=(const struct retro_message *)data;
            if(m&&m->msg) printf("[PicoDrive] %s\n",m->msg);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
        {
            struct retro_audio_buffer_status_callback *cb=(struct retro_audio_buffer_status_callback *)data;
            g_audio_status_cb=cb?cb->callback:NULL;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
            return true;
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
            return true;
        default:
            return false;
    }
}

static void frf_video(const void *data,unsigned width,unsigned height,size_t pitch)
{
    int rate_drop=0;
    /* PicoDrive's own frameskip path deliberately calls video_cb(NULL,...).
     * V0.1.2 still WaitTOF()'d here, nullifying the speed gain. V0.2.6 returns
     * immediately: emulation/audio/input still execute, only presentation is skipped. */
    if(data==NULL) { ++g_skipped; return; }

    /* Keep the 60->50 cadence phase advancing on every real core frame, even
     * when the audio governor later discards presentation work. */
    if(g_core_fps > (double)g_display_hz + 0.5) {
        g_rate_accum += (double)g_display_hz;
        if(g_rate_accum + 0.0001 < g_core_fps)
            rate_drop=1;
        else
            g_rate_accum -= g_core_fps;
    }

    /* V0.2.15: audio remains the real-time master, but recovery is bounded.
     * V0.2.14 skipped every presentation until the ring reached HIGH, creating
     * visible 10-20-frame freezes. Here a recovery frame is interleaved with a
     * presented frame. Below CRITICAL we permit at most two consecutive shed
     * frames; otherwise no more than one. The main loop removes timer sleep
     * only for the specific shed frame, so recovery accumulates gradually. */
    g_audio_guard_unslept_this_frame=0;
    audio_guard_update();

    if(g_audio_refill_active) {
        unsigned buffered=frf_pd_audio_buffered_frames();
        int shed=0;

        /* A normal 60->50 cadence drop is already a free recovery slot. */
        if(rate_drop) {
            ++g_skipped;
            ++g_rate_skipped;
            ++g_audio_refill_core_frames;
            g_audio_guard_unslept_this_frame=1;
            g_audio_guard_phase=0;
            g_audio_guard_critical_run=0;
            return;
        }

        if(buffered<FRF_AUDIO_GUARD_CRITICAL_FRAMES) {
            if(g_audio_guard_critical_run<2U) {
                shed=1;
                ++g_audio_guard_critical_run;
            } else {
                g_audio_guard_critical_run=0;
            }
        } else {
            g_audio_guard_critical_run=0;
            shed=(g_audio_guard_phase==0U);
            g_audio_guard_phase^=1U;
        }

        if(shed) {
            ++g_skipped;
            ++g_audio_guard_skipped;
            if(buffered<FRF_AUDIO_GUARD_CRITICAL_FRAMES)
                ++g_audio_guard_critical_skipped;
            ++g_audio_refill_core_frames;
            g_audio_guard_unslept_this_frame=1;
            return;
        }

        ++g_audio_guard_presented;
    } else if(rate_drop) {
        ++g_skipped;
        ++g_rate_skipped;
        return;
    }

    if(width!=g_last_w || height!=g_last_h) {
        printf("FRF PicoDriveRGB: video %ux%u pitch=%lu RGB565\n",
               width,height,(unsigned long)pitch);
        g_last_w=width; g_last_h=height;
    }
    if(!(g_backend_rtg?frf_pd_rtg_present(data,width,height,(unsigned)pitch):frf_pd_ham6_present_rgb565(data,width,height,(unsigned)pitch))) {
        printf("FRF PicoDriveRGB: DirectChip presenter unavailable; aborting\n");
        g_quit=1; return;
    }
    /* Deadline timer owns game speed. WaitTOF is only an emergency fallback
     * if timer.device could not be opened. */
    if(!g_timer_ok && !g_backend_rtg) frf_pd_ham6_wait_vblank();
    ++g_presented;
}

static void frf_audio_sample(int16_t left,int16_t right)
{
    if(!g_nosound) frf_pd_audio_push_sample(left,right);
    notify_audio_status();
}

static size_t frf_audio_batch(const int16_t *data,size_t frames)
{
    size_t n=frames;
    if(!g_nosound) n=frf_pd_audio_push(data,frames);
    notify_audio_status();
    return n;
}

extern void frf_pd_rtg_poll_events(void);
extern int frf_pd_rtg_key_state(unsigned id);
static void frf_input_poll(void) {
    if(g_backend_rtg) frf_pd_rtg_poll_events();
    frf_pd_input_poll();
}

static int16_t frf_input_state(unsigned port,unsigned device,unsigned index,unsigned id)
{
    if(device!=RETRO_DEVICE_JOYPAD || index!=0U) return 0;
    return (int16_t)(frf_pd_input_state(port,id) || (g_backend_rtg && port==0U && frf_pd_rtg_key_state(id)));
}

static int ctrl_c_pressed(void)
{
    ULONG sig=SetSignal(0,0);
    if(sig&SIGBREAKF_CTRL_C) {
        SetSignal(0,SIGBREAKF_CTRL_C);
        return 1;
    }
    return 0;
}

static const char *opt_value(int *i,int argc,char **argv,const char *arg,const char *name)
{
    size_t n=strlen(name);
    if(!strncmp(arg,name,n) && arg[n]=='=') return arg+n+1;
    if(!strcmp(arg,name) && *i+1<argc) return argv[++(*i)];
    return NULL;
}

int main(int argc,char **argv)
{
    struct retro_system_info si;
    struct retro_system_av_info av;
    struct retro_game_info game;
    const char *rom=NULL;
    int i,core_inited=0,game_loaded=0;

    atexit(frf_cleanup);

    for(i=1;i<argc;i++) {
        const char *a=argv[i],*v;
        if(!strcmp(a,"--version")) {
            printf("%s v%s\n",FRF_PROJECT_NAME,FRF_PROJECT_VERSION);
            printf("Target: %s\n",FRF_PROJECT_TARGET);
            printf("Port and native-output work: Future Retro Fusion\n");
            return 0;
        }
        if(!strcmp(a,"--help") || !strcmp(a,"-h")) {
            printf("%s v%s\n",FRF_PROJECT_NAME,FRF_PROJECT_VERSION);
            printf("Future Retro Fusion AmigaOS 68k native RGB/HAM6 port\n");
            printf("Usage: %s [--output=ham6|rtg] [--window] [--scale=1|2|3|4] [--scanlines=0|25|50|75] [--frfcp=on|off] [--dbuf=on|off] [--renderer=fast|good|accurate] [--audio-rate=22050|44100] [--frameskip=auto|off|manual] [--threshold=33] [--pad=3|6] [--nosound] game.md\n",argv[0]);
            printf("Defaults: --frfcp=on --dbuf=off --renderer=accurate --audio-rate=44100 --frameskip=off --pad=3\n");
            return 0;
        }
        if(!strcmp(a,"--nosound")) { g_nosound=1; continue; }
        if(!strcmp(a,"--window")) { g_backend_rtg=1; g_rtg_window=1; continue; }
        if((v=opt_value(&i,argc,argv,a,"--output"))!=NULL) { if(!strcmp(v,"rtg")) g_backend_rtg=1; else if(!strcmp(v,"ham6")) g_backend_rtg=0; else {puts("Bad output (rtg|ham6)");return 5;} continue; }
        if((v=opt_value(&i,argc,argv,a,"--scale"))!=NULL) {g_rtg_scale=atoi(v); if(g_rtg_scale<1||g_rtg_scale>4){puts("Bad scale (1..4)");return 5;} continue;}
        if((v=opt_value(&i,argc,argv,a,"--scanlines"))!=NULL) {g_rtg_scan=atoi(v); if(g_rtg_scan!=0&&g_rtg_scan!=25&&g_rtg_scan!=50&&g_rtg_scan!=75){puts("Bad scanlines (0|25|50|75)");return 5;} continue;}
        if((v=opt_value(&i,argc,argv,a,"--frfcp"))!=NULL) {
            if(!strcmp(v,"on")||!strcmp(v,"1")) g_frfcp_enabled=1;
            else if(!strcmp(v,"off")||!strcmp(v,"0")) g_frfcp_enabled=0;
            else { printf("Bad --frfcp value: %s (on|off)\n",v); return 5; }
            continue;
        }
        if((v=opt_value(&i,argc,argv,a,"--dbuf"))!=NULL) {
            if(!strcmp(v,"on")||!strcmp(v,"1")) g_dbuf_enabled=1;
            else if(!strcmp(v,"off")||!strcmp(v,"0")) g_dbuf_enabled=0;
            else { printf("Bad --dbuf value: %s (on|off)\n",v); return 5; }
            continue;
        }
        if((v=opt_value(&i,argc,argv,a,"--audio-rate"))!=NULL) {
            if(!strcmp(v,"22050")) { g_audio_rate="22050"; }
            else if(!strcmp(v,"44100")) { g_audio_rate="44100"; }
            else { printf("Bad --audio-rate value: %s (22050|44100)\n",v); return 5; }
            continue;
        }
        if((v=opt_value(&i,argc,argv,a,"--renderer"))!=NULL) {
            if(!strcmp(v,"fast")) g_renderer="fast";
            else if(!strcmp(v,"good")) g_renderer="good";
            else if(!strcmp(v,"accurate")) g_renderer="accurate";
            else { printf("Bad --renderer value: %s (fast|good|accurate)\n",v); return 5; }
            continue;
        }
        if((v=opt_value(&i,argc,argv,a,"--frameskip"))!=NULL) {
            if(!strcmp(v,"auto")) g_frameskip="auto";
            else if(!strcmp(v,"off")||!strcmp(v,"disabled")||!strcmp(v,"0")) g_frameskip="disabled";
            else if(!strcmp(v,"manual")) g_frameskip="manual";
            else { printf("Bad --frameskip value: %s (auto|off|manual)\n",v); return 5; }
            continue;
        }
        if((v=opt_value(&i,argc,argv,a,"--threshold"))!=NULL) { g_threshold=v; continue; }
        if((v=opt_value(&i,argc,argv,a,"--pad"))!=NULL) {
            if(!strcmp(v,"6")) g_pad="6 button pad";
            else if(!strcmp(v,"3")) g_pad="3 button pad";
            else { printf("Bad --pad value: %s (3|6)\n",v); return 5; }
            continue;
        }
        if(a[0]=='-') { printf("Unknown option: %s\n",a); return 5; }
        if(rom) { printf("Only one ROM may be specified\n"); return 5; }
        rom=a;
    }

    if(!rom) {
        printf("%s v%s\n",FRF_PROJECT_NAME,FRF_PROJECT_VERSION);
        printf("Future Retro Fusion AmigaOS 68k native RGB/HAM6 port\n");
        printf("Target: %s\n",FRF_PROJECT_TARGET);
        printf("Usage: %s [--output=ham6|rtg] [--window] [--scale=1|2|3|4] [--scanlines=0|25|50|75] [--frfcp=on|off] [--dbuf=on|off] [--renderer=fast|good|accurate] [--audio-rate=22050|44100] [--frameskip=auto|off|manual] [--threshold=33] [--pad=3|6] [--nosound] game.md\n",argv[0]);
        printf("Defaults: --frfcp=on --dbuf=off --renderer=accurate --audio-rate=44100 --frameskip=off --pad=3\n");
        return 5;
    }

    memset(&si,0,sizeof(si)); memset(&av,0,sizeof(av)); memset(&game,0,sizeof(game));

    printf("%s v%s\n",FRF_PROJECT_NAME,FRF_PROJECT_VERSION);
    printf("Future Retro Fusion AmigaOS 68k native RGB/HAM6 port\n");
    printf("Direct CHIP presenter + optional Emu68/ARM coprocessor client\n");

    retro_set_environment(frf_env);
    retro_set_video_refresh(frf_video);
    retro_set_audio_sample(frf_audio_sample);
    retro_set_audio_sample_batch(frf_audio_batch);
    retro_set_input_poll(frf_input_poll);
    retro_set_input_state(frf_input_state);

    retro_init(); core_inited=1;
    retro_get_system_info(&si);
    printf("Future Retro Fusion PicoDriveRGB: core=%s version=%s\n",si.library_name?si.library_name:"?",si.library_version?si.library_version:"?");

    /* V0.2.11: prove transport, then allow HAM6 to claim the new optional
     * RGB565->HAM6 opcode during screen initialization. --frfcp=off gives
     * an exact A/B control using the unchanged 68K encoder. */
    if(g_frfcp_enabled) {
        if(frfcp_init()) {
            if(!frfcp_selftest()) {
                printf("FRF Coprocessor: unavailable - using normal 68K path (%s)\n",frfcp_last_error());
                frfcp_shutdown();
            }
        } else {
            printf("FRF Coprocessor: unavailable - using normal 68K path (%s)\n",frfcp_last_error());
        }
    } else {
        printf("FRF Coprocessor: disabled by --frfcp=off; using normal 68K HAM6 path\n");
    }

    printf("Future Retro Fusion PicoDriveRGB: ROM=%s\n",rom);
    printf("Future Retro Fusion PicoDriveRGB: renderer=%s frameskip=%s threshold=%s pad=%s sound=%s%s frfcp=%s dbuf=%s\n",
           g_renderer,g_frameskip,g_threshold,g_pad,g_nosound?"OFF":"AHI ",
           g_nosound?"":g_audio_rate,g_frfcp_enabled?"on":"off",
           g_dbuf_enabled?"on":"off");
    if(!strcmp(g_renderer,"fast"))
        printf("[FRF PD WARNING] fast renderer is experimental and may corrupt games using mid-frame changes; accurate is recommended\n");

    game.path=rom;
    if(!retro_load_game(&game)) { printf("Future Retro Fusion PicoDriveRGB: retro_load_game failed\n"); goto out; }
    game_loaded=1;

    retro_get_system_av_info(&av);
    g_core_fps=av.timing.fps>1.0?av.timing.fps:60.0;
    printf("Future Retro Fusion PicoDriveRGB: core timing fps=%.3f audio=%.1f Hz base=%ux%u max=%ux%u\n",
           av.timing.fps,av.timing.sample_rate,av.geometry.base_width,av.geometry.base_height,
           av.geometry.max_width,av.geometry.max_height);

    retro_set_controller_port_device(0,RETRO_DEVICE_JOYPAD);
    retro_set_controller_port_device(1,RETRO_DEVICE_JOYPAD);

    /* V0.2.16: optional ScreenBuffer page flipping is requested before native
     * screen initialisation. Default OFF preserves V0.2.15 exactly. */
    if(!g_backend_rtg) frf_pd_ham6_set_double_buffer(g_dbuf_enabled);

    /* V0.2.16B INPUT OWNERSHIP REPAIR
     *
     * Claim gameport/lowlevel input BEFORE opening the custom HAM6 screen.
     * This matches the hardware-proven PocketSNESRGB order (sal_Init/input,
     * then sal_VideoInit) and avoids making joystick acquisition depend on
     * Intuition/custom-screen ownership.  AHI remains after video so the
     * recovered V0.2.6 CHIP-allocation safety is otherwise preserved. */
    if(!frf_pd_input_init())
        printf("[FRF PD INPUT] WARNING: no controller backend acquired before video init\n");
    else
        printf("[FRF PD INPUT] ownership acquired BEFORE HAM6 screen init\n");

    if(g_backend_rtg) {frf_pd_rtg_config(g_rtg_scale,g_rtg_scan); frf_pd_rtg_window(g_rtg_window);}
    if(!(g_backend_rtg?frf_pd_rtg_init():frf_pd_ham6_init_for_fps(g_core_fps))) {
        printf("Future Retro Fusion PicoDriveRGB: DirectChip screen init failed after input claim; aborting\n");
        goto out;
    }
    g_display_hz=g_backend_rtg?(unsigned)(g_core_fps+0.5):frf_pd_ham6_refresh_hz();
    printf("Future Retro Fusion PicoDriveRGB: pacing core=%.3fHz display=%uHz%s\n",
           g_core_fps,g_display_hz,
           (g_core_fps>(double)g_display_hz+0.5)?" deterministic presentation ratio active":" display cadence matched");

    if(!g_nosound) frf_pd_audio_init((unsigned)(av.timing.sample_rate+0.5));
    g_audio_refill_active=0;
    g_audio_guard_phase=0;
    g_audio_guard_critical_run=0;
    g_audio_guard_unslept_this_frame=0;
    g_audio_guard_skipped=0;
    g_audio_guard_presented=0;
    g_audio_guard_critical_skipped=0;
    g_audio_refill_entries=0;
    g_audio_refill_core_frames=0;
    g_audio_min_buffered=0xffffffffU;
    if(!g_nosound && frf_pd_audio_active())
        printf("[FRF PD AUDIO-GUARD] INTERLEAVED critical=%u low=%u high=%u ring=%u; max normal shed=1, critical shed=2\n",
               FRF_AUDIO_GUARD_CRITICAL_FRAMES,FRF_AUDIO_GUARD_LOW_FRAMES,
               FRF_AUDIO_GUARD_HIGH_FRAMES,frf_pd_audio_capacity_frames());
    g_timer_ok=frf_pd_timer_init((unsigned)(g_core_fps+0.5));
    if(!g_timer_ok)
        printf("[FRF PD TIMER] unavailable; falling back to display vblank pacing\n");

    while(!g_quit) {
        if(ctrl_c_pressed()) { printf("Future Retro Fusion PicoDriveRGB: Ctrl-C\n"); break; }
        retro_run();
        notify_audio_status();
        audio_guard_update();
        if(g_timer_ok) {
            if(g_audio_guard_unslept_this_frame) {
                /* Only the deliberately shed frame runs without deadline sleep.
                 * A presented recovery frame is paced normally, preventing the
                 * long V0.2.14 video freeze while still earning PCM headroom. */
                frf_pd_timer_rebase_now();
            } else {
                frf_pd_timer_wait_next();
            }
        }
    }

out:
    {
        unsigned long late=frf_pd_timer_late_frames();
        unsigned long dropped=frf_pd_audio_dropped_frames();
        unsigned minbuf=(g_audio_min_buffered==0xffffffffU)?0U:g_audio_min_buffered;
        printf("[FRF PD AUDIO-GUARD] shutdown entries=%lu recovery_slots=%lu video_skipped=%lu recovery_presented=%lu critical_skipped=%lu min_buffered=%u active=%d\n",
               g_audio_refill_entries,g_audio_refill_core_frames,g_audio_guard_skipped,
               g_audio_guard_presented,g_audio_guard_critical_skipped,minbuf,g_audio_refill_active);
        frf_cleanup();
        if(game_loaded) retro_unload_game();
        if(core_inited) retro_deinit();
        printf("Future Retro Fusion PicoDriveRGB: exit presented=%lu skipped=%lu rate_skipped=%lu audio_guard_skipped=%lu late=%lu audio_dropped=%lu\n",
               g_presented,g_skipped,g_rate_skipped,g_audio_guard_skipped,late,dropped);
    }
    return game_loaded?0:10;
}
