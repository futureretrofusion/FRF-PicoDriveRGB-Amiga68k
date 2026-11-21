#ifndef FRF_PD_AGA_H
#define FRF_PD_AGA_H
/* AGA native custom-screen modes; 0 = HAM8, 64/128/256 = indexed palette. */
void frf_pd_aga_config(int colors);
int frf_pd_aga_init_for_fps(double fps);
int frf_pd_aga_present_rgb565(const void *pixels,unsigned width,unsigned height,unsigned pitch);
unsigned frf_pd_aga_refresh_hz(void);
void frf_pd_aga_wait_vblank(void);
void frf_pd_aga_shutdown(void);
#endif
