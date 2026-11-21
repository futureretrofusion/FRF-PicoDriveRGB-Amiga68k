#ifndef FRF_PD_AUDIO_H
#define FRF_PD_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int frf_pd_audio_init(unsigned rate_hz);
size_t frf_pd_audio_push(const int16_t *stereo_frames,size_t frames);
void frf_pd_audio_push_sample(int16_t left,int16_t right);
void frf_pd_audio_pump(void);
void frf_pd_audio_shutdown(void);
unsigned frf_pd_audio_occupancy_percent(void);
unsigned frf_pd_audio_buffered_frames(void);
unsigned frf_pd_audio_capacity_frames(void);
int frf_pd_audio_underrun_likely(void);
int frf_pd_audio_active(void);
unsigned long frf_pd_audio_dropped_frames(void);
#ifdef __cplusplus
}
#endif
#endif
