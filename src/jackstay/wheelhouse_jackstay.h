#ifndef WHEELHOUSE_JACKSTAY_H
#define WHEELHOUSE_JACKSTAY_H
#include <jackstay_bootstrap.h>
#include <stdbool.h>
/* Thread-safe session owner. No shell configuration lives here, and the only
   GPU objects are the ones a D3D11 source needs (Windows).
   Returned pixels are caller-owned. CPU leases stay on the worker; a D3D11
   frame for import moves to the caller, who hands it back with
   wh_js_frame_release. Built on RAD's base layer: include base_inc.h before
   this header. */
typedef struct WH_Jackstay WH_Jackstay;
/* What an address publishes. */
typedef enum {
  WH_JS_MEDIA_CPU,      /* Jackstay CPU setup (combined or separate endpoints) */
  WH_JS_MEDIA_D3D11,    /* Jackstay D3D11 setup (Windows) */
  WH_JS_MEDIA_PORTHOLE, /* a Porthole native capture session: attach token,
                           then the D3D11 or CPU setup the reply names */
} WH_JS_Media;
/* Addresses are Local Endpoint names (ADR 0011), optionally prefixed
   "session:" for a session-scoped endpoint ("user:", the default scope, is
   also accepted). A Porthole endpoint is always session-scoped. On POSIX an
   absolute socket path is also accepted for existing path-bound CPU
   publications. session/token identify a Porthole capture session. */
typedef struct {
  const char *media, *input;
  bool bootstrap;
  WH_JS_Media kind;
  const char *session, *token;
} WH_JS_Endpoint;
/* The renderer that samples frames. D3D11 frames are imported on the GUI
   thread only when the producer's adapter is this adapter and the renderer
   has a shared release fence; otherwise the worker reads them back on the
   producer's adapter and they take the CPU path. Zeroed: CPU path only. */
typedef struct {
  uint64_t adapter;    /* LUID, (HighPart << 32) | LowPart */
  void *device;        /* borrowed ID3D11Device* */
  void *release_fence; /* borrowed shared ID3D11Fence*, signalled after use */
} WH_JS_Gpu;
typedef enum {
  WH_JS_PATH_NONE,
  WH_JS_PATH_CPU,      /* CPU publication */
  WH_JS_PATH_IMPORT,   /* D3D11 textures imported on the renderer's device */
  WH_JS_PATH_READBACK, /* D3D11 frames read back to the CPU */
} WH_JS_Path;
typedef struct {
  bool connected, connecting, control, resetting, stopped;
  bool cleanup_confirmed;
  ft_input_config input;
  char media_status[128], input_status[128];
  /* How frames arrive and the producer's adapter, e.g. "D3D11 import on
     00000000:00009fe5 AMD Radeon(TM) Graphics". Empty before setup. */
  char path_status[256];
  WH_JS_Path path;
  uint64_t producer_adapter;
  uint64_t frame_version, input_epoch, pointer_epoch;
  uint32_t width, height;
} WH_JS_State;
typedef struct {
  /* CPU frames: packed RGBA, caller-owned. */
  uint8_t *pixels;
  uint32_t width, height;
  uint64_t version;
  /* Import frames (pixels == 0): an owned frame whose texture and readiness
     fence handles are borrowed until wh_js_frame_release. Import them, check
     wh_js_readiness_alive, queue a GPU wait for fence_value, sample, then
     signal the release fence and release with that value. Cache imports by
     (incarnation, pool_id, slot_id) and (incarnation, fence_id). */
  ft_acquired_frame *native;
  void *texture, *readiness;
  uint64_t incarnation, pool_id, fence_id, fence_value;
  uint32_t slot_id, pixel_format;
} WH_JS_Frame;
WH_Jackstay *wh_js_create(WH_JS_Endpoint endpoint, WH_JS_Gpu gpu,
                          void (*wake)(void *), void *context);
void wh_js_connect(WH_Jackstay *session, bool request_control);
void wh_js_focus(WH_Jackstay *session, bool focused);
bool wh_js_send(WH_Jackstay *session, const ft_input_event *event);
void wh_js_snapshot(WH_Jackstay *session, WH_JS_State *state,
                    WH_JS_Frame *frame, uint64_t after);
/* Return an import frame. release_value names the caller's release fence
   value signalled after every GPU use of it; 0 means no GPU work used it. */
void wh_js_frame_release(WH_Jackstay *session, WH_JS_Frame *frame,
                         uint64_t release_value);
/* False when the frame's readiness fence was abandoned: its producer died
   and the copy may never have run. Discard such a frame. */
bool wh_js_readiness_alive(void *readiness_fence);
/* The caller could not import this producer's frames: reconnect and read
   them back instead. */
void wh_js_refuse_import(WH_Jackstay *session, const char *reason);
void wh_js_stop(WH_Jackstay *session);
/* Only succeeds after both workers finish; never blocks a GUI thread. */
bool wh_js_destroy(WH_Jackstay **session);
#endif
