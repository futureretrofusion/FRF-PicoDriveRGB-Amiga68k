#ifndef FRF_PD_RTG_H
#define FRF_PD_RTG_H
void frf_pd_rtg_config(int scale,int scanlines);
void frf_pd_rtg_window(int on);
void frf_pd_rtg_mode_720p(int on);
int frf_pd_rtg_quit_requested(void);
int frf_pd_rtg_init(void);
int frf_pd_rtg_present(const void*,unsigned,unsigned,unsigned);
void frf_pd_rtg_shutdown(void);
#endif
