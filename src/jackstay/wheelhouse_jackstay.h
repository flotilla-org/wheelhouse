#ifndef WHEELHOUSE_JACKSTAY_H
#define WHEELHOUSE_JACKSTAY_H
#include <jackstay_bootstrap.h>
#include <stdbool.h>
/* Thread-safe session owner. No GPU objects or shell configuration live here.
   Returned pixels are caller-owned; all Jackstay leases stay on the worker.
   Built on RAD's base layer: include base_inc.h before this header. */
typedef struct WH_Jackstay WH_Jackstay;
/* Addresses are Local Endpoint names (ADR 0011), optionally prefixed
   "session:" for a session-scoped endpoint ("user:", the default scope, is
   also accepted). On POSIX an absolute socket path
   is also accepted for existing path-bound publications. */
typedef struct {
  const char *media, *input;
  bool bootstrap;
} WH_JS_Endpoint;
typedef struct {
  bool connected, connecting, control, resetting, stopped;
  bool cleanup_confirmed;
  ft_input_config input;
  char media_status[128], input_status[128];
  uint64_t frame_version, input_epoch, pointer_epoch;
  uint32_t width, height;
} WH_JS_State;
typedef struct {
  uint8_t *pixels;
  uint32_t width, height;
  uint64_t version;
} WH_JS_Frame;
WH_Jackstay *wh_js_create(WH_JS_Endpoint endpoint, void (*wake)(void *),
                          void *context);
void wh_js_connect(WH_Jackstay *session, bool request_control);
void wh_js_focus(WH_Jackstay *session, bool focused);
bool wh_js_send(WH_Jackstay *session, const ft_input_event *event);
void wh_js_snapshot(WH_Jackstay *session, WH_JS_State *state,
                    WH_JS_Frame *frame, uint64_t after);
void wh_js_stop(WH_Jackstay *session);
/* Only succeeds after both workers finish; never blocks a GUI thread. */
bool wh_js_destroy(WH_Jackstay **session);
#endif
