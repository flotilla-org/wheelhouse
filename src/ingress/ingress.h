#ifndef UISHELL_INGRESS_H
#define UISHELL_INGRESS_H
#include <stddef.h>
#include <stdint.h>

// start borrows a UTF-8 path and writes a NUL-terminated error on failure.
// NULL/empty paths fail. Other pointers must be valid for their lengths;
// the error buffer may be NULL only when its capacity is zero.
// wake runs on the transport thread; it must only signal the UI event loop.
// poll and stop run exclusively on the UI thread. poll borrows patch bytes
// for each callback; return 1=applied, 0=invalid, 2=temporarily unavailable.
// stop joins the worker and invalidates the handle. NULL is accepted.
typedef struct WheelhouseIngress WheelhouseIngress;
extern WheelhouseIngress *wheelhouse_ingress_start(const uint8_t *, size_t, void (*)(void), uint8_t *, size_t);
extern void wheelhouse_ingress_poll(WheelhouseIngress *, uint32_t (*)(void *, const uint8_t *, size_t), void *);
extern void wheelhouse_ingress_stop(WheelhouseIngress *);
extern uint64_t wheelhouse_ingress_now_ms(void);
#endif
