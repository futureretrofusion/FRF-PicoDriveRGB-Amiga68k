#ifndef FRF_PD_TIMER_H
#define FRF_PD_TIMER_H
#ifdef __cplusplus
extern "C" {
#endif
int frf_pd_timer_init(unsigned hz);
void frf_pd_timer_wait_next(void);
void frf_pd_timer_rebase_now(void);
unsigned long frf_pd_timer_late_frames(void);
void frf_pd_timer_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
