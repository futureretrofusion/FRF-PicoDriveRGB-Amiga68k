#ifndef FRF_PD_KEYS_H
#define FRF_PD_KEYS_H
struct Screen;
int frf_pd_keys_attach(struct Screen *screen);
void frf_pd_keys_poll(void);
int frf_pd_keys_state(unsigned id);
int frf_pd_keys_quit(void);
void frf_pd_keys_shutdown(void);
#endif
