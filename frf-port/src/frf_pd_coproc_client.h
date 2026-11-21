#ifndef FRF_PD_COPROC_CLIENT_H
#define FRF_PD_COPROC_CLIENT_H

#include <stdint.h>

int frfcp_init(void);
int frfcp_available(void);
int frfcp_has_ham6(void);
int frfcp_selftest(void);
void frfcp_shutdown(void);
const char *frfcp_last_error(void);

/* CPU3 HAM6 accelerator. The synchronous call is retained for the startup
 * equivalence proof. Gameplay should use the pipelined submit/poll API so
 * CPU3 can overlap with emulation of the following frame. */
int frfcp_ham6_prepare(const uint8_t *direct_pen_65536);
int frfcp_ham6_active(void);
int frfcp_ham6_encode_rgb565(const void *pixels,
                             unsigned width,
                             unsigned height,
                             unsigned pitch_bytes,
                             const uint8_t **packed_rows,
                             const uint8_t **dirty_lines,
                             uint32_t *changed_lines);
void frfcp_ham6_disable(const char *reason);

#define FRFCP_HAM6_POLL_ERROR (-1)
#define FRFCP_HAM6_POLL_IDLE   0
#define FRFCP_HAM6_POLL_DONE   1
#define FRFCP_HAM6_POLL_BUSY   2

int frfcp_ham6_async_submit_rgb565(const void *pixels,
                                    unsigned width,
                                    unsigned height,
                                    unsigned pitch_bytes);
int frfcp_ham6_async_poll(const uint8_t **packed_rows,
                          const uint8_t **dirty_lines,
                          uint32_t *changed_lines,
                          unsigned *width,
                          unsigned *height);
int frfcp_ham6_async_pending(void);
unsigned long frfcp_ham6_async_busy_polls(void);

#endif
