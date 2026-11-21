#ifndef FRF_PD_HAM6_H
#define FRF_PD_HAM6_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FRF PicoDriveRGB Emu68 V0.2.3
 *
 * Direct presenter contract:
 *   source: packed RGB565, host-endian 16-bit pixels
 *   maximum source: 320x240
 *   destination: displayed native HAM6 BitMap in CHIP RAM; 60 Hz NTSC for ~60 fps cores, 50 Hz PAL for ~50 fps cores
 *
 * Return value from present:
 *   1 = frame consumed by the native HAM6 presenter
 *   0 = presenter disabled/unavailable; caller should use normal video path
 */
void frf_pd_ham6_set_double_buffer(int enable);
int  frf_pd_ham6_init(void);
int  frf_pd_ham6_init_for_fps(double fps);
unsigned frf_pd_ham6_refresh_hz(void);
int  frf_pd_ham6_present_rgb565(const void *pixels,
                                 unsigned width,
                                 unsigned height,
                                 unsigned pitch_bytes);
void frf_pd_ham6_wait_vblank(void);
void frf_pd_ham6_mark_pipeline_stale(void);
void frf_pd_ham6_shutdown(void);
int  frf_pd_ham6_active(void);

#ifdef __cplusplus
}
#endif

#endif
