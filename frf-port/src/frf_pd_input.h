#ifndef FRF_PD_INPUT_H
#define FRF_PD_INPUT_H
#include <stdint.h>
int  frf_pd_input_init(void);
void frf_pd_input_poll(void);
int16_t frf_pd_input_state(unsigned port,unsigned id);
void frf_pd_input_shutdown(void);
#endif
