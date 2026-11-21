/*
 * FRF PicoDriveRGB Emu68 V0.2.12 - pipelined FRF ARM coprocessor client.
 *
 * V0.2.10 keeps the Stage 1 transport proof and adds one real acceleration path:
 * RGB565 -> exact V0.2.3 HAM6 decisions/planar packing on ARM CPU3.
 *
 * Stage 1 transport proof:
 *   - probe the published FRF coprocessor ABI
 *   - detect how shared ARM words are exposed to the 68K guest
 *   - validate control/queue/workspace/worker state
 *   - submit one deterministic MEMFILL8 job to ARM CPU3
 *   - verify job state, worker counters and output byte-for-byte
 *   - fail closed to the normal 68K PicoDrive path
 *
 * If the V0.2 firmware feature/opcode is absent or a frame job fails, the
 * caller immediately falls back to the proven 68K HAM6 implementation.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(__amigaos__) || defined(__AMIGA__) || defined(AMIGA) || defined(__amiga__)
#define FRFCP_AMIGA 1
#include <dos/dos.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>
#else
#define FRFCP_AMIGA 0
#endif

#include "frf_coproc_abi.h"
#include "frf_pd_coproc_client.h"

#define FRFCP_SELFTEST_BYTES       32UL
#define FRFCP_SELFTEST_VALUE       0x3CUL
#define FRFCP_SELFTEST_TICKS       25U
#define FRFCP_SELFTEST_JOB_ID      0x50444350UL /* 'PDCP' */
#define FRFCP_HAM6_W                 320UL
#define FRFCP_HAM6_MAX_H             240UL
#define FRFCP_HAM6_SRC_ROW           (FRFCP_HAM6_W*2UL)
#define FRFCP_HAM6_PACKED_ROW        (6UL*(FRFCP_HAM6_W/8UL))
#define FRFCP_HAM6_LUT_BYTES         65536UL
#define FRFCP_HAM6_FRAME_SPINS       4000000UL
#define FRFCP_HAM6_ASYNC_BUSY_LIMIT    4UL
#define FRFCP_HAM6_JOB_ID_BASE       0x48360000UL /* 'H6' */
#define FRFCP_ALIGN64(v)             (((v)+63UL)&~63UL)
#define FRFCP_HAM6_INPUT_OFF         0UL
#define FRFCP_HAM6_INPUT_MAX         (FRFCP_HAM6_SRC_ROW*FRFCP_HAM6_MAX_H)
#define FRFCP_HAM6_OUTPUT_OFF        FRFCP_ALIGN64(FRFCP_HAM6_INPUT_OFF+FRFCP_HAM6_INPUT_MAX)
#define FRFCP_HAM6_OUTPUT_MAX        (FRFCP_HAM6_PACKED_ROW*FRFCP_HAM6_MAX_H)
#define FRFCP_HAM6_PREV_OFF          FRFCP_ALIGN64(FRFCP_HAM6_OUTPUT_OFF+FRFCP_HAM6_OUTPUT_MAX+FRFCP_HAM6_MAX_H)
#define FRFCP_HAM6_PREV_MAX          FRFCP_HAM6_INPUT_MAX
#define FRFCP_HAM6_LUT_OFF           FRFCP_ALIGN64(FRFCP_HAM6_PREV_OFF+FRFCP_HAM6_PREV_MAX)
#define FRFCP_HAM6_END               (FRFCP_HAM6_LUT_OFF+FRFCP_HAM6_LUT_BYTES)

#define FRFCP_WORDS_UNKNOWN        0
#define FRFCP_WORDS_GUEST32        1
#define FRFCP_WORDS_SWAP32         2

typedef char frfcp_ctrl_size_must_be_128[(sizeof(struct FRFCoprocControl)==128)?1:-1];
typedef char frfcp_job_size_must_be_64[(sizeof(struct FRFCoprocJob)==64)?1:-1];

static volatile uint8_t *g_ctrl;
static volatile uint8_t *g_queue;
static volatile uint8_t *g_ws;
static uint32_t g_queue_entries;
static uint32_t g_workspace_bytes;
static uint32_t g_doorbell;
static uint32_t g_features;
static uint32_t g_counter_hz;
static int g_word_mode;
static int g_available;
static int g_ham6_ready;
static unsigned g_ham6_w,g_ham6_h;
static uint32_t g_ham6_job_seq;
static uint32_t g_ham6_jobs;
static uint32_t g_ham6_fallbacks;
static uint64_t g_ham6_cycles;
static volatile uint8_t *g_ham6_pending_job;
static uint32_t g_ham6_pending_producer;
static uint32_t g_ham6_pending_out_bytes;
static unsigned g_ham6_pending_w,g_ham6_pending_h;
static uint32_t g_ham6_pending_busy;
static uint32_t g_ham6_busy_total;
static uint32_t g_ham6_submitted;
static char g_error[96];

static void cp_compiler_barrier(void)
{
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

static uint32_t cp_bswap32(uint32_t v)
{
    return ((v & 0x000000ffUL) << 24) |
           ((v & 0x0000ff00UL) << 8)  |
           ((v & 0x00ff0000UL) >> 8)  |
           ((v & 0xff000000UL) >> 24);
}

static uint32_t cp_raw32_load(const volatile uint8_t *p)
{
    const volatile uint32_t *q=(const volatile uint32_t *)(const void *)p;
    return *q;
}

static void cp_raw32_store(volatile uint8_t *p,uint32_t v)
{
    volatile uint32_t *q=(volatile uint32_t *)(void *)p;
    *q=v;
}

/* The discovery window is synthetic MMIO and already returns guest-logical
 * 32-bit values. Shared Pi RAM is different: depending on the Emu68 mapping,
 * an ordinary 68K longword load may expose either the logical ARM value or
 * the ARM little-endian byte image. Detect that once from control.magic. */
static uint32_t cp_shared_load(const volatile uint8_t *p)
{
    uint32_t v=cp_raw32_load(p);
    if(g_word_mode==FRFCP_WORDS_SWAP32) return cp_bswap32(v);
    return v;
}

static void cp_shared_store(volatile uint8_t *p,uint32_t v)
{
    if(g_word_mode==FRFCP_WORDS_SWAP32) v=cp_bswap32(v);
    cp_raw32_store(p,v);
}

#define CTRL_LOAD(member) \
    cp_shared_load(g_ctrl + offsetof(struct FRFCoprocControl, member))
#define CTRL_STORE(member,value) \
    cp_shared_store(g_ctrl + offsetof(struct FRFCoprocControl, member),(uint32_t)(value))
#define JOB_LOAD(j,member) \
    cp_shared_load((j) + offsetof(struct FRFCoprocJob, member))
#define JOB_STORE(j,member,value) \
    cp_shared_store((j) + offsetof(struct FRFCoprocJob, member),(uint32_t)(value))

static void cp_set_error(const char *s)
{
    size_t n;
    if(!s) s="unknown error";
    n=strlen(s);
    if(n>=sizeof(g_error)) n=sizeof(g_error)-1;
    memcpy(g_error,s,n);
    g_error[n]='\0';
}

const char *frfcp_last_error(void)
{
    return g_error[0]?g_error:"none";
}

static uint32_t cp_disc32(uint32_t addr)
{
    volatile uint32_t *p=(volatile uint32_t *)(uintptr_t)addr;
    return *p;
}

static void cp_wait_tick(void)
{
#if FRFCP_AMIGA
    Delay(1);
#else
    volatile unsigned long i;
    for(i=0;i<100000UL;i++) cp_compiler_barrier();
#endif
}

static int cp_wait_ctrl_value(size_t off,uint32_t want,unsigned ticks)
{
    unsigned t;
    for(t=0;t<=ticks;t++) {
        cp_compiler_barrier();
        if(cp_shared_load(g_ctrl+off)==want) return 1;
        if(t!=ticks) cp_wait_tick();
    }
    return 0;
}

static int cp_wait_job_terminal(volatile uint8_t *j,unsigned ticks,uint32_t *state_out)
{
    unsigned t;
    uint32_t st=FRFCP_JOB_FREE;
    for(t=0;t<=ticks;t++) {
        cp_compiler_barrier();
        st=JOB_LOAD(j,state);
        if(st==FRFCP_JOB_DONE || st==FRFCP_JOB_ERROR) {
            if(state_out) *state_out=st;
            return 1;
        }
        if(t!=ticks) cp_wait_tick();
    }
    if(state_out) *state_out=st;
    return 0;
}

static int cp_ctrl_check(const char *name,size_t off,uint32_t want)
{
    uint32_t got=cp_shared_load(g_ctrl+off);
    if(got==want) return 1;
    printf("FRF Coprocessor: control mismatch %s got=$%08lX expected=$%08lX\n",
           name,(unsigned long)got,(unsigned long)want);
    cp_set_error("control block field mismatch");
    return 0;
}

void frfcp_shutdown(void)
{
    g_ctrl=NULL;
    g_queue=NULL;
    g_ws=NULL;
    g_queue_entries=0;
    g_workspace_bytes=0;
    g_doorbell=0;
    g_features=0;
    g_counter_hz=0;
    g_word_mode=FRFCP_WORDS_UNKNOWN;
    g_available=0;
    g_ham6_ready=0;
    g_ham6_w=g_ham6_h=0;
    g_ham6_pending_job=NULL;
    g_ham6_pending_producer=0;
    g_ham6_pending_out_bytes=0;
    g_ham6_pending_w=g_ham6_pending_h=0;
    g_ham6_pending_busy=0;
    g_ham6_busy_total=0;
    g_ham6_submitted=0;
}

int frfcp_available(void)
{
    return g_available;
}

int frfcp_has_ham6(void)
{
    return g_available &&
           ((g_features & FRFCP_FEAT_RGB565_TO_HAM6_PLANAR) != 0);
}

int frfcp_init(void)
{
    uint32_t magic,abi,ctrl_addr,queue_addr,entries,ws_addr,ws_bytes;
    uint32_t features,doorbell,worker_core,required_features,raw_magic;

    frfcp_shutdown();
    g_error[0]='\0';

    magic=cp_disc32(FRFCP_DISC_MAGIC);
    if(magic!=FRF_COPROC_MAGIC) {
        cp_set_error("discovery magic mismatch");
        return 0;
    }
    printf("FRF Coprocessor: discovery OK\n");

    abi=cp_disc32(FRFCP_DISC_ABI);
    if(abi!=FRF_COPROC_ABI_VERSION) {
        cp_set_error("ABI mismatch");
        return 0;
    }
    printf("FRF Coprocessor: ABI %lu\n",(unsigned long)abi);

    ctrl_addr=cp_disc32(FRFCP_DISC_CTRL);
    queue_addr=cp_disc32(FRFCP_DISC_QUEUE);
    entries=cp_disc32(FRFCP_DISC_QUEUE_ENTRIES);
    ws_addr=cp_disc32(FRFCP_DISC_WORKSPACE);
    ws_bytes=cp_disc32(FRFCP_DISC_WORKSPACE_BYTES);
    features=cp_disc32(FRFCP_DISC_FEATURES);
    doorbell=cp_disc32(FRFCP_DISC_DOORBELL);
    worker_core=cp_disc32(FRFCP_DISC_WORKER_CORE);

    if(!ctrl_addr || !queue_addr || !ws_addr) {
        cp_set_error("zero shared address");
        return 0;
    }
    if((ctrl_addr&3UL)||(queue_addr&3UL)||(ws_addr&3UL)) {
        cp_set_error("unaligned shared address");
        return 0;
    }
    if(entries!=FRF_COPROC_QUEUE_ENTRIES ||
       (entries&(entries-1UL))!=0 || FRF_COPROC_JOB_BYTES!=FRF_COPROC_DESC_BYTES) {
        cp_set_error("queue layout mismatch");
        return 0;
    }
    if(ws_bytes!=FRF_COPROC_WORKSPACE_BYTES || ws_bytes<FRFCP_SELFTEST_BYTES) {
        cp_set_error("workspace size mismatch");
        return 0;
    }
    if(doorbell!=FRF_COPROC_DOORBELL_ADDR) {
        cp_set_error("doorbell mismatch");
        return 0;
    }
    if(worker_core!=3UL) {
        cp_set_error("discovery worker core mismatch");
        return 0;
    }

    required_features=FRFCP_FEAT_ASYNC_QUEUE|FRFCP_FEAT_ARM_CORE3|
                      FRFCP_FEAT_WORKSPACE|FRFCP_FEAT_MEMFILL|
                      FRFCP_FEAT_FIXED_DISCOVERY;
    if((features&required_features)!=required_features) {
        cp_set_error("required feature missing");
        return 0;
    }

    g_ctrl=(volatile uint8_t *)(uintptr_t)ctrl_addr;
    g_queue=(volatile uint8_t *)(uintptr_t)queue_addr;
    g_ws=(volatile uint8_t *)(uintptr_t)ws_addr;
    g_queue_entries=entries;
    g_workspace_bytes=ws_bytes;
    g_doorbell=doorbell;
    g_features=features;
    printf("FRF Coprocessor: feature mask=$%08lX (HAM6 bit9=%s)\n",
           (unsigned long)features,
           (features&FRFCP_FEAT_RGB565_TO_HAM6_PLANAR)?"present":"MISSING");

    raw_magic=cp_raw32_load(g_ctrl+offsetof(struct FRFCoprocControl,magic));
    if(raw_magic==FRF_COPROC_MAGIC) {
        g_word_mode=FRFCP_WORDS_GUEST32;
        printf("FRF Coprocessor: shared control words 68K-visible\n");
    } else if(cp_bswap32(raw_magic)==FRF_COPROC_MAGIC) {
        g_word_mode=FRFCP_WORDS_SWAP32;
        printf("FRF Coprocessor: shared control words ARM little-endian\n");
    } else {
        printf("FRF Coprocessor: control magic raw=$%08lX\n",(unsigned long)raw_magic);
        cp_set_error("shared control magic unreadable");
        frfcp_shutdown();
        return 0;
    }

    if(!cp_ctrl_check("magic",offsetof(struct FRFCoprocControl,magic),FRF_COPROC_MAGIC) ||
       !cp_ctrl_check("abi",offsetof(struct FRFCoprocControl,abi_version),FRF_COPROC_ABI_VERSION) ||
       !cp_ctrl_check("struct_size",offsetof(struct FRFCoprocControl,struct_size),FRF_COPROC_CTRL_BYTES) ||
       !cp_ctrl_check("queue_base",offsetof(struct FRFCoprocControl,queue_base),queue_addr) ||
       !cp_ctrl_check("queue_entries",offsetof(struct FRFCoprocControl,queue_entries),entries) ||
       !cp_ctrl_check("workspace_base",offsetof(struct FRFCoprocControl,workspace_base),ws_addr) ||
       !cp_ctrl_check("workspace_bytes",offsetof(struct FRFCoprocControl,workspace_bytes),ws_bytes) ||
       !cp_ctrl_check("features",offsetof(struct FRFCoprocControl,features),features) ||
       !cp_ctrl_check("doorbell",offsetof(struct FRFCoprocControl,doorbell_addr),doorbell)) {
        frfcp_shutdown();
        return 0;
    }

    if(!cp_wait_ctrl_value(offsetof(struct FRFCoprocControl,worker_online),1UL,
                           FRFCP_SELFTEST_TICKS)) {
        cp_set_error("worker offline");
        frfcp_shutdown();
        return 0;
    }
    if(CTRL_LOAD(worker_core)!=3UL) {
        cp_set_error("control worker core mismatch");
        frfcp_shutdown();
        return 0;
    }
    printf("FRF Coprocessor: worker online\n");
    printf("FRF Coprocessor: ARM CPU3\n");
    printf("FRF Coprocessor: queue OK (%lu x %lu bytes)\n",
           (unsigned long)entries,(unsigned long)FRF_COPROC_DESC_BYTES);
    g_counter_hz=CTRL_LOAD(counter_hz);
    printf("FRF Coprocessor: workspace OK (%lu bytes)\n",(unsigned long)ws_bytes);

    g_available=1;
    printf("FRFCP: CPU3 transport detected\n");
    printf("FRFCP: worker core=%lu\n",(unsigned long)CTRL_LOAD(worker_core));
    printf("FRFCP: feature RGB565_TO_HAM6_PLANAR=%s\n",
           frfcp_has_ham6()?"yes":"no");
    return 1;
}

int frfcp_selftest(void)
{
    uint8_t saved[FRFCP_SELFTEST_BYTES];
    volatile uint8_t *j;
    volatile uint32_t *bell;
    uint32_t p,c,slot,ws_off,st,status;
    uint32_t jobs_before,done_before,wake_before,jobs_after,done_after,wake_after;
    uint32_t cycles_lo,cycles_hi;
    unsigned i;

    if(!g_available || !g_ctrl || !g_queue || !g_ws) {
        cp_set_error("client not initialized");
        return 0;
    }

    p=CTRL_LOAD(producer);
    c=CTRL_LOAD(consumer);
    if((uint32_t)(p-c)>=g_queue_entries) {
        cp_set_error("queue full");
        return 0;
    }

    jobs_before=CTRL_LOAD(jobs_started);
    done_before=CTRL_LOAD(completed);
    wake_before=CTRL_LOAD(wakeups);

    slot=p&(g_queue_entries-1UL);
    j=g_queue+(slot*FRF_COPROC_DESC_BYTES);
    ws_off=g_workspace_bytes-FRFCP_SELFTEST_BYTES;

    for(i=0;i<FRFCP_SELFTEST_BYTES;i++) {
        saved[i]=g_ws[ws_off+i];
        g_ws[ws_off+i]=(uint8_t)0xA5U;
    }

    /* Clear the descriptor as shared words, not byte-by-byte. That preserves
     * the chosen shared-word representation and avoids publishing torn fields. */
    for(i=0;i<FRF_COPROC_DESC_BYTES;i+=4)
        cp_shared_store(j+i,0UL);

    JOB_STORE(j,job_id,FRFCP_SELFTEST_JOB_ID);
    JOB_STORE(j,opcode,FRFCP_OP_MEMFILL8);
    JOB_STORE(j,status,FRFCP_STATUS_NOT_READY);
    JOB_STORE(j,src_offset,0UL);
    JOB_STORE(j,dst_offset,ws_off);
    JOB_STORE(j,bytes,FRFCP_SELFTEST_BYTES);
    JOB_STORE(j,arg0,FRFCP_SELFTEST_VALUE);
    cp_compiler_barrier();
    JOB_STORE(j,state,FRFCP_JOB_READY);
    cp_compiler_barrier();
    CTRL_STORE(producer,p+1UL);
    cp_compiler_barrier();

    bell=(volatile uint32_t *)(uintptr_t)g_doorbell;
    *bell=FRFCP_SELFTEST_JOB_ID;
    cp_compiler_barrier();

    if(!cp_wait_job_terminal(j,FRFCP_SELFTEST_TICKS,&st)) {
        cp_set_error("self-test timeout (job left owned by CPU3)");
        g_available=0;
        return 0;
    }

    status=JOB_LOAD(j,status);
    if(st!=FRFCP_JOB_DONE || status!=FRFCP_STATUS_OK) {
        printf("FRF Coprocessor: CPU3 job state=%lu status=%lu\n",
               (unsigned long)st,(unsigned long)status);
        cp_set_error("self-test job error");
        for(i=0;i<FRFCP_SELFTEST_BYTES;i++) g_ws[ws_off+i]=saved[i];
        g_available=0;
        return 0;
    }

    for(i=0;i<FRFCP_SELFTEST_BYTES;i++) {
        if(g_ws[ws_off+i]!=(uint8_t)FRFCP_SELFTEST_VALUE) {
            printf("FRF Coprocessor: data mismatch at +%lu got=$%02lX expected=$%02lX\n",
                   (unsigned long)i,(unsigned long)g_ws[ws_off+i],
                   (unsigned long)FRFCP_SELFTEST_VALUE);
            cp_set_error("self-test data mismatch");
            for(i=0;i<FRFCP_SELFTEST_BYTES;i++) g_ws[ws_off+i]=saved[i];
            g_available=0;
            return 0;
        }
    }

    if(!cp_wait_ctrl_value(offsetof(struct FRFCoprocControl,consumer),p+1UL,
                           FRFCP_SELFTEST_TICKS)) {
        cp_set_error("consumer advance timeout");
        for(i=0;i<FRFCP_SELFTEST_BYTES;i++) g_ws[ws_off+i]=saved[i];
        g_available=0;
        return 0;
    }

    jobs_after=CTRL_LOAD(jobs_started);
    done_after=CTRL_LOAD(completed);
    wake_after=CTRL_LOAD(wakeups);
    cycles_lo=JOB_LOAD(j,cycles_lo);
    cycles_hi=JOB_LOAD(j,cycles_hi);

    if(jobs_after==jobs_before || done_after==done_before) {
        cp_set_error("CPU3 worker counters did not advance");
        for(i=0;i<FRFCP_SELFTEST_BYTES;i++) g_ws[ws_off+i]=saved[i];
        g_available=0;
        return 0;
    }

    for(i=0;i<FRFCP_SELFTEST_BYTES;i++) g_ws[ws_off+i]=saved[i];
    cp_compiler_barrier();
    JOB_STORE(j,state,FRFCP_JOB_FREE);
    cp_compiler_barrier();

    printf("FRF Coprocessor: CPU3 executed MEMFILL8, 32 bytes now verified as $3C\n");
    printf("FRF Coprocessor: counters jobs %lu->%lu completed %lu->%lu wakeups %lu->%lu cycles=$%08lX%08lX\n",
           (unsigned long)jobs_before,(unsigned long)jobs_after,
           (unsigned long)done_before,(unsigned long)done_after,
           (unsigned long)wake_before,(unsigned long)wake_after,
           (unsigned long)cycles_hi,(unsigned long)cycles_lo);
    printf("FRF Coprocessor: self-test PASS - real ARM CPU3 job confirmed\n");
    return 1;
}

void frfcp_ham6_disable(const char *reason)
{
    cp_set_error(reason?reason:"HAM6 accelerator disabled");
    if(g_ham6_ready) {
        ++g_ham6_fallbacks;
        printf("[FRF PD FRFCP] ARM HAM6 disabled - 68K fallback (%s)\n",reason?reason:"unknown");
    }
    g_ham6_ready=0;
}

int frfcp_ham6_active(void)
{
    return g_available && g_ham6_ready;
}

int frfcp_ham6_prepare(const uint8_t *direct_pen_65536)
{
    if(!g_available || !g_ws || !direct_pen_65536) return 0;
    if(!frfcp_has_ham6()) {
        printf("FRFCP: firmware has no RGB565->HAM6 opcode; using 68K presenter\n");
        return 0;
    }
    if(g_workspace_bytes<FRFCP_HAM6_END+FRFCP_SELFTEST_BYTES) {
        cp_set_error("workspace too small for HAM6 accelerator");
        return 0;
    }
#if FRFCP_AMIGA
    CopyMem((APTR)direct_pen_65536,(APTR)(g_ws+FRFCP_HAM6_LUT_OFF),FRFCP_HAM6_LUT_BYTES);
#else
    memcpy((void *)(g_ws+FRFCP_HAM6_LUT_OFF),direct_pen_65536,FRFCP_HAM6_LUT_BYTES);
#endif
    memset((void *)(g_ws+FRFCP_HAM6_PREV_OFF),0,FRFCP_HAM6_PREV_MAX);
    memset((void *)(g_ws+FRFCP_HAM6_OUTPUT_OFF),0,FRFCP_HAM6_OUTPUT_MAX+FRFCP_HAM6_MAX_H);
    cp_compiler_barrier();
    g_ham6_w=g_ham6_h=0;
    g_ham6_job_seq=0;
    g_ham6_jobs=0;
    g_ham6_fallbacks=0;
    g_ham6_cycles=0;
    g_ham6_pending_job=NULL;
    g_ham6_pending_producer=0;
    g_ham6_pending_out_bytes=0;
    g_ham6_pending_w=g_ham6_pending_h=0;
    g_ham6_pending_busy=0;
    g_ham6_busy_total=0;
    g_ham6_submitted=0;
    g_ham6_ready=1;
    printf("FRFCP: RGB565->HAM6 planar feature present\n");
    printf("FRFCP: HAM6 CPU3 presenter enabled\n");
    printf("[FRF PD FRFCP] exact V0.2.3 HAM decisions; 68K dirty-UWORD CHIP writer retained\n");
    return 1;
}

static int cp_wait_job_terminal_spins(volatile uint8_t *j,uint32_t *state_out)
{
    uint32_t st=FRFCP_JOB_FREE;
    unsigned long spin;
    for(spin=0;spin<FRFCP_HAM6_FRAME_SPINS;spin++) {
        cp_compiler_barrier();
        st=JOB_LOAD(j,state);
        if(st==FRFCP_JOB_DONE || st==FRFCP_JOB_ERROR) {
            if(state_out) *state_out=st;
            return 1;
        }
    }
    if(state_out) *state_out=st;
    return 0;
}

int frfcp_ham6_encode_rgb565(const void *pixels,
                             unsigned width,
                             unsigned height,
                             unsigned pitch_bytes,
                             const uint8_t **packed_rows,
                             const uint8_t **dirty_lines,
                             uint32_t *changed_lines)
{
    volatile uint8_t *j;
    volatile uint32_t *bell;
    uint32_t p,c,slot,st,status,src_bytes,out_bytes,result_changed,result_rows;
    uint32_t cycles_lo,cycles_hi;
    unsigned y,xoff;

    if(packed_rows) *packed_rows=NULL;
    if(dirty_lines) *dirty_lines=NULL;
    if(changed_lines) *changed_lines=0;
    if(!g_available || !g_ham6_ready || !pixels) return 0;
    if(width==0 || width>FRFCP_HAM6_W || height==0 || height>FRFCP_HAM6_MAX_H || pitch_bytes<width*2U) {
        frfcp_ham6_disable("unsupported frame geometry");
        return 0;
    }

    if(width!=g_ham6_w || height!=g_ham6_h) {
        memset((void *)(g_ws+FRFCP_HAM6_PREV_OFF),0,FRFCP_HAM6_PREV_MAX);
        memset((void *)(g_ws+FRFCP_HAM6_OUTPUT_OFF),0,FRFCP_HAM6_OUTPUT_MAX+FRFCP_HAM6_MAX_H);
        g_ham6_w=width; g_ham6_h=height;
    }

    xoff=(unsigned)((FRFCP_HAM6_W-width)/2UL);
    src_bytes=(uint32_t)(height*FRFCP_HAM6_SRC_ROW);
    out_bytes=(uint32_t)(height*FRFCP_HAM6_PACKED_ROW);

    /* Sonic's normal 320-wide path is one bulk copy. Narrower frames keep the
     * proven presenter semantics by zero-padding/centring to 320 pixels. */
    if(width==FRFCP_HAM6_W && pitch_bytes==FRFCP_HAM6_SRC_ROW) {
#if FRFCP_AMIGA
        CopyMem((APTR)pixels,(APTR)(g_ws+FRFCP_HAM6_INPUT_OFF),src_bytes);
#else
        memcpy((void *)(g_ws+FRFCP_HAM6_INPUT_OFF),pixels,src_bytes);
#endif
    } else {
        for(y=0;y<height;y++) {
            volatile uint8_t *dst=g_ws+FRFCP_HAM6_INPUT_OFF+y*FRFCP_HAM6_SRC_ROW;
            memset((void *)dst,0,FRFCP_HAM6_SRC_ROW);
#if FRFCP_AMIGA
            CopyMem((APTR)((const uint8_t *)pixels+y*pitch_bytes),
                    (APTR)(dst+xoff*2U),(ULONG)width*2UL);
#else
            memcpy((void *)(dst+xoff*2U),(const uint8_t *)pixels+y*pitch_bytes,width*2U);
#endif
        }
    }
    cp_compiler_barrier();

    p=CTRL_LOAD(producer);
    c=CTRL_LOAD(consumer);
    if((uint32_t)(p-c)>=g_queue_entries) {
        frfcp_ham6_disable("queue full");
        return 0;
    }
    slot=p&(g_queue_entries-1UL);
    j=g_queue+(slot*FRF_COPROC_DESC_BYTES);
    for(y=0;y<FRF_COPROC_DESC_BYTES;y+=4U) cp_shared_store(j+y,0UL);

    JOB_STORE(j,job_id,FRFCP_HAM6_JOB_ID_BASE+(++g_ham6_job_seq&0xffffUL));
    JOB_STORE(j,opcode,FRFCP_OP_RGB565_TO_HAM6_PLANAR);
    JOB_STORE(j,status,FRFCP_STATUS_NOT_READY);
    JOB_STORE(j,src_offset,FRFCP_HAM6_INPUT_OFF);
    JOB_STORE(j,dst_offset,FRFCP_HAM6_OUTPUT_OFF);
    JOB_STORE(j,bytes,src_bytes);
    JOB_STORE(j,arg0,FRFCP_HAM6_LUT_OFF);
    JOB_STORE(j,arg1,FRFCP_HAM6_PREV_OFF);
    cp_compiler_barrier();
    JOB_STORE(j,state,FRFCP_JOB_READY);
    cp_compiler_barrier();
    CTRL_STORE(producer,p+1UL);
    cp_compiler_barrier();

    bell=(volatile uint32_t *)(uintptr_t)g_doorbell;
    *bell=FRFCP_HAM6_JOB_ID_BASE|(g_ham6_job_seq&0xffffUL);
    cp_compiler_barrier();

    if(!cp_wait_job_terminal_spins(j,&st)) {
        frfcp_ham6_disable("frame job timeout");
        return 0;
    }
    status=JOB_LOAD(j,status);
    if(st!=FRFCP_JOB_DONE || status!=FRFCP_STATUS_OK) {
        printf("[FRF PD FRFCP] HAM6 job state=%lu status=%lu\n",
               (unsigned long)st,(unsigned long)status);
        frfcp_ham6_disable("frame job error");
        return 0;
    }
    result_changed=JOB_LOAD(j,result0);
    result_rows=JOB_LOAD(j,result1);
    cycles_lo=JOB_LOAD(j,cycles_lo);
    cycles_hi=JOB_LOAD(j,cycles_hi);
    if(result_rows!=height || result_changed>height) {
        frfcp_ham6_disable("frame result mismatch");
        return 0;
    }
    if(CTRL_LOAD(consumer)!=(p+1UL)) {
        frfcp_ham6_disable("consumer did not advance");
        return 0;
    }

    g_ham6_cycles+=((uint64_t)cycles_hi<<32)|cycles_lo;
    ++g_ham6_jobs;
    if((g_ham6_jobs%250UL)==0) {
        uint64_t avg_cycles=g_ham6_cycles/g_ham6_jobs;
        unsigned long avg_us=0;
        if(g_counter_hz) avg_us=(unsigned long)((avg_cycles*1000000ULL)/g_counter_hz);
        printf("[FRF PD FRFCP] HAM6 jobs=%lu changed_last=%lu avg_arm_us=%lu fallbacks=%lu\n",
               (unsigned long)g_ham6_jobs,(unsigned long)result_changed,
               avg_us,(unsigned long)g_ham6_fallbacks);
    }

    if(packed_rows) *packed_rows=(const uint8_t *)(g_ws+FRFCP_HAM6_OUTPUT_OFF);
    if(dirty_lines) *dirty_lines=(const uint8_t *)(g_ws+FRFCP_HAM6_OUTPUT_OFF+out_bytes);
    if(changed_lines) *changed_lines=result_changed;
    cp_compiler_barrier();
    JOB_STORE(j,state,FRFCP_JOB_FREE);
    cp_compiler_barrier();
    return 1;
}


int frfcp_ham6_async_pending(void)
{
    return g_ham6_pending_job != NULL;
}

unsigned long frfcp_ham6_async_busy_polls(void)
{
    return (unsigned long)g_ham6_busy_total;
}

int frfcp_ham6_async_submit_rgb565(const void *pixels,
                                    unsigned width,
                                    unsigned height,
                                    unsigned pitch_bytes)
{
    volatile uint8_t *j;
    volatile uint32_t *bell;
    uint32_t p,c,slot,src_bytes,out_bytes;
    unsigned y,xoff;

    if(!g_available || !g_ham6_ready || !pixels) return -1;
    if(g_ham6_pending_job) return 0;
    if(width==0 || width>FRFCP_HAM6_W || height==0 ||
       height>FRFCP_HAM6_MAX_H || pitch_bytes<width*2U) {
        frfcp_ham6_disable("unsupported async frame geometry");
        return -1;
    }

    if(width!=g_ham6_w || height!=g_ham6_h) {
        memset((void *)(g_ws+FRFCP_HAM6_PREV_OFF),0,FRFCP_HAM6_PREV_MAX);
        memset((void *)(g_ws+FRFCP_HAM6_OUTPUT_OFF),0,
               FRFCP_HAM6_OUTPUT_MAX+FRFCP_HAM6_MAX_H);
        g_ham6_w=width;
        g_ham6_h=height;
    }

    xoff=(unsigned)((FRFCP_HAM6_W-width)/2UL);
    src_bytes=(uint32_t)(height*FRFCP_HAM6_SRC_ROW);
    out_bytes=(uint32_t)(height*FRFCP_HAM6_PACKED_ROW);

    /* The shared input remains owned by CPU3 until async_poll() returns DONE.
     * PicoDrive renders its following frame elsewhere, so this one persistent
     * workspace input is sufficient for a one-frame pipeline. */
    if(width==FRFCP_HAM6_W && pitch_bytes==FRFCP_HAM6_SRC_ROW) {
#if FRFCP_AMIGA
        CopyMem((APTR)pixels,(APTR)(g_ws+FRFCP_HAM6_INPUT_OFF),src_bytes);
#else
        memcpy((void *)(g_ws+FRFCP_HAM6_INPUT_OFF),pixels,src_bytes);
#endif
    } else {
        for(y=0;y<height;y++) {
            volatile uint8_t *dst=g_ws+FRFCP_HAM6_INPUT_OFF+
                                  y*FRFCP_HAM6_SRC_ROW;
            memset((void *)dst,0,FRFCP_HAM6_SRC_ROW);
#if FRFCP_AMIGA
            CopyMem((APTR)((const uint8_t *)pixels+y*pitch_bytes),
                    (APTR)(dst+xoff*2U),(ULONG)width*2UL);
#else
            memcpy((void *)(dst+xoff*2U),
                   (const uint8_t *)pixels+y*pitch_bytes,width*2U);
#endif
        }
    }
    cp_compiler_barrier();

    p=CTRL_LOAD(producer);
    c=CTRL_LOAD(consumer);
    if((uint32_t)(p-c)>=g_queue_entries) {
        frfcp_ham6_disable("async queue full");
        return -1;
    }

    slot=p&(g_queue_entries-1UL);
    j=g_queue+(slot*FRF_COPROC_DESC_BYTES);
    for(y=0;y<FRF_COPROC_DESC_BYTES;y+=4U) cp_shared_store(j+y,0UL);

    JOB_STORE(j,job_id,FRFCP_HAM6_JOB_ID_BASE+(++g_ham6_job_seq&0xffffUL));
    JOB_STORE(j,opcode,FRFCP_OP_RGB565_TO_HAM6_PLANAR);
    JOB_STORE(j,status,FRFCP_STATUS_NOT_READY);
    JOB_STORE(j,src_offset,FRFCP_HAM6_INPUT_OFF);
    JOB_STORE(j,dst_offset,FRFCP_HAM6_OUTPUT_OFF);
    JOB_STORE(j,bytes,src_bytes);
    JOB_STORE(j,arg0,FRFCP_HAM6_LUT_OFF);
    JOB_STORE(j,arg1,FRFCP_HAM6_PREV_OFF);
    cp_compiler_barrier();
    JOB_STORE(j,state,FRFCP_JOB_READY);
    cp_compiler_barrier();
    CTRL_STORE(producer,p+1UL);
    cp_compiler_barrier();

    bell=(volatile uint32_t *)(uintptr_t)g_doorbell;
    *bell=FRFCP_HAM6_JOB_ID_BASE|(g_ham6_job_seq&0xffffUL);
    cp_compiler_barrier();

    g_ham6_pending_job=j;
    g_ham6_pending_producer=p;
    g_ham6_pending_out_bytes=out_bytes;
    g_ham6_pending_w=width;
    g_ham6_pending_h=height;
    g_ham6_pending_busy=0;
    ++g_ham6_submitted;
    return 1;
}

int frfcp_ham6_async_poll(const uint8_t **packed_rows,
                          const uint8_t **dirty_lines,
                          uint32_t *changed_lines,
                          unsigned *width,
                          unsigned *height)
{
    volatile uint8_t *j=g_ham6_pending_job;
    uint32_t st,status,result_changed,result_rows,cycles_lo,cycles_hi;

    if(packed_rows) *packed_rows=NULL;
    if(dirty_lines) *dirty_lines=NULL;
    if(changed_lines) *changed_lines=0;
    if(width) *width=0;
    if(height) *height=0;
    if(!g_available || !g_ham6_ready) return FRFCP_HAM6_POLL_ERROR;
    if(!j) return FRFCP_HAM6_POLL_IDLE;

    cp_compiler_barrier();
    st=JOB_LOAD(j,state);
    if(st==FRFCP_JOB_READY || st==FRFCP_JOB_RUNNING) {
        ++g_ham6_pending_busy;
        ++g_ham6_busy_total;
        if(g_ham6_pending_busy>=FRFCP_HAM6_ASYNC_BUSY_LIMIT) {
            printf("[FRF PD FRFCP] async HAM6 still busy after %lu presenter callbacks; disabling pipeline\n",
                   (unsigned long)g_ham6_pending_busy);
            frfcp_ham6_disable("async frame job stalled");
            return FRFCP_HAM6_POLL_ERROR;
        }
        return FRFCP_HAM6_POLL_BUSY;
    }

    status=JOB_LOAD(j,status);
    if(st!=FRFCP_JOB_DONE || status!=FRFCP_STATUS_OK) {
        printf("[FRF PD FRFCP] async HAM6 job state=%lu status=%lu\n",
               (unsigned long)st,(unsigned long)status);
        if(st==FRFCP_JOB_DONE || st==FRFCP_JOB_ERROR) {
            JOB_STORE(j,state,FRFCP_JOB_FREE);
            cp_compiler_barrier();
            g_ham6_pending_job=NULL;
        }
        frfcp_ham6_disable("async frame job error");
        return FRFCP_HAM6_POLL_ERROR;
    }

    result_changed=JOB_LOAD(j,result0);
    result_rows=JOB_LOAD(j,result1);
    cycles_lo=JOB_LOAD(j,cycles_lo);
    cycles_hi=JOB_LOAD(j,cycles_hi);
    if(result_rows!=g_ham6_pending_h || result_changed>g_ham6_pending_h) {
        JOB_STORE(j,state,FRFCP_JOB_FREE);
        cp_compiler_barrier();
        g_ham6_pending_job=NULL;
        frfcp_ham6_disable("async frame result mismatch");
        return FRFCP_HAM6_POLL_ERROR;
    }
    if(CTRL_LOAD(consumer)!=(g_ham6_pending_producer+1UL)) {
        JOB_STORE(j,state,FRFCP_JOB_FREE);
        cp_compiler_barrier();
        g_ham6_pending_job=NULL;
        frfcp_ham6_disable("async consumer did not advance");
        return FRFCP_HAM6_POLL_ERROR;
    }

    g_ham6_cycles+=((uint64_t)cycles_hi<<32)|cycles_lo;
    ++g_ham6_jobs;
    if((g_ham6_jobs%250UL)==0) {
        uint64_t avg_cycles=g_ham6_cycles/g_ham6_jobs;
        unsigned long avg_us=0;
        if(g_counter_hz)
            avg_us=(unsigned long)((avg_cycles*1000000ULL)/g_counter_hz);
        printf("[FRF PD FRFCP] PIPE jobs=%lu submitted=%lu busy_polls=%lu changed_last=%lu avg_arm_us=%lu fallbacks=%lu\n",
               (unsigned long)g_ham6_jobs,
               (unsigned long)g_ham6_submitted,
               (unsigned long)g_ham6_busy_total,
               (unsigned long)result_changed,
               avg_us,(unsigned long)g_ham6_fallbacks);
    }

    if(packed_rows)
        *packed_rows=(const uint8_t *)(g_ws+FRFCP_HAM6_OUTPUT_OFF);
    if(dirty_lines)
        *dirty_lines=(const uint8_t *)(g_ws+FRFCP_HAM6_OUTPUT_OFF+
                                      g_ham6_pending_out_bytes);
    if(changed_lines) *changed_lines=result_changed;
    if(width) *width=g_ham6_pending_w;
    if(height) *height=g_ham6_pending_h;

    cp_compiler_barrier();
    JOB_STORE(j,state,FRFCP_JOB_FREE);
    cp_compiler_barrier();
    g_ham6_pending_job=NULL;
    g_ham6_pending_producer=0;
    g_ham6_pending_out_bytes=0;
    g_ham6_pending_busy=0;
    return FRFCP_HAM6_POLL_DONE;
}
