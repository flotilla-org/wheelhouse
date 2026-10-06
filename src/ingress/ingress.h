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
// Optional recorder; NULL recording path disables it. Limits include the active file.
extern WheelhouseIngress *wheelhouse_ingress_start_recorded(const uint8_t *, size_t, void (*)(void),
  const uint8_t *, size_t, uint64_t, size_t, uint8_t *, size_t);
extern void wheelhouse_ingress_poll(WheelhouseIngress *, uint32_t (*)(void *, const uint8_t *, size_t), void *);
typedef struct { const uint8_t *data; size_t len; } WheelhouseIngressText;
typedef struct {
  uint64_t workspace_id, view_id;
  WheelhouseIngressText entity_kind, entity_id, cwd, live_cwd;
} WheelhouseWorkdir;
typedef void (*WheelhouseWorkdirEmit)(void *, const WheelhouseWorkdir *);
// observe runs on the UI thread and synchronously emits borrowed records.
// Return 1 for a complete snapshot, 0 when temporarily unavailable. Empty text
// is encoded as null; records with neither directory are omitted. poll without
// an observer answers reads with 503, as do invalid UTF-8 records. Adjacent
// reads in a drain share one snapshot; intervening patches invalidate it.
// No host pointers cross to the worker.
extern void wheelhouse_ingress_poll_observed(WheelhouseIngress *,
  uint32_t (*)(void *, const uint8_t *, size_t),
  uint32_t (*)(void *, WheelhouseWorkdirEmit, void *), void *);
extern void wheelhouse_ingress_stop(WheelhouseIngress *);
extern uint64_t wheelhouse_ingress_now_ms(void);
#endif
