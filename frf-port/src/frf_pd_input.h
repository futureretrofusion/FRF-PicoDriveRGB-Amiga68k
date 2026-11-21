#ifndef FRF_PD_INPUT_H
#define FRF_PD_INPUT_H
#include <stdint.h>
/* 0=auto (merged), 1=direct CHIP/CIA, 2=lowlevel, 3=gameport */
void frf_pd_input_config(int mode,int connector,int debug);
int frf_pd_input_init(void);
void frf_pd_input_poll(void);
int16_t frf_pd_input_state(unsigned port,unsigned id);
void frf_pd_input_shutdown(void);
#endif
