// Requires RAD's base layer (base_inc.h/.c) earlier in the translation unit:
// workers use base threads, mutexes, time and sleep on every platform.
#include "wheelhouse_jackstay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if OS_LINUX || OS_MAC
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#define WH_JS_MAX_FRAME (64u * 1024u * 1024u)
#define WH_JS_QUEUE 128
#define WH_JS_TEXT 16384

typedef struct {
  ft_input_event event;
  uint8_t *text;
} WH_JS_Pending;
struct WH_Jackstay {
  Mutex mutex;
  Thread media_thread, input_thread;
  char *media_address, *input_address;
  bool bootstrap, stop, media_done, input_done, connect, want_input, focus,
      reset;
  bool input_close, peer_closed;
  unsigned request;
  ft_cpu_acquisition_connection *connection;
  ft_input_client *input;
  WH_JS_State state;
  uint8_t *pixels;
  WH_JS_Pending pending[WH_JS_QUEUE];
  size_t count, text_bytes;
  void (*wake)(void *);
  void *wake_context;
};
static void wh_js_wake(WH_Jackstay *s) {
  if (s->wake)
    s->wake(s->wake_context);
}
static void wh_js_pause(void) { sleep_ms(5); }
static void wh_js_clear(WH_Jackstay *s) {
  for (size_t i = 0; i < s->count; ++i)
    free(s->pending[i].text);
  s->count = 0;
  s->text_bytes = 0;
}

// An open setup connection. Jackstay connects Local Endpoints itself and
// verifies the server (ADR 0011). POSIX also accepts an absolute socket path,
// the form existing path-bound publications (e.g. Porthole's) still use.
// Jackstay's setup calls null `local` (or set `fd` to -1) whenever they consume
// it, on success or failure; bootstrap success hands it back unchanged for CPU
// setup. wh_js_link_close therefore releases only what no call consumed.
typedef struct {
  ft_local_connection *local;
  int fd;
} WH_JS_Link;
static bool wh_js_parse_endpoint(const char *address,
                                 ft_local_endpoint *endpoint) {
  endpoint->scope = FT_ENDPOINT_SCOPE_USER;
  endpoint->transport = FT_ENDPOINT_TRANSPORT_LOCAL_STREAM;
  endpoint->name = address;
  // Names cannot contain ':', so the scope prefix is unambiguous.
  if (!strncmp(address, "session:", 8)) {
    endpoint->scope = FT_ENDPOINT_SCOPE_SESSION;
    endpoint->name = address + 8;
  } else if (!strncmp(address, "user:", 5))
    endpoint->name = address + 5;
  return endpoint->name[0] != 0;
}
#if OS_LINUX || OS_MAC
static int wh_js_socket(const char *path) {
  struct sockaddr_un address = {.sun_family = AF_UNIX};
  if (strlen(path) >= sizeof(address.sun_path))
    return -1;
  memcpy(address.sun_path, path, strlen(path) + 1);
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  int result = connect(fd, (struct sockaddr *)&address, sizeof(address));
  if (result < 0 && errno == EINPROGRESS) {
    struct pollfd p = {.fd = fd, .events = POLLOUT};
    int error = 0;
    socklen_t size = sizeof(error);
    result = poll(&p, 1, 1000) > 0 &&
                     getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 &&
                     !error
                 ? 0
                 : -1;
  }
  if (result < 0) {
    close(fd);
    return -1;
  }
  fcntl(fd, F_SETFL, flags);
  return fd;
}
#endif
static ft_status wh_js_link_open(const char *address, WH_JS_Link *link) {
  link->local = 0;
  link->fd = -1;
  if (!address || !address[0])
    return FT_STATUS_INVALID_ARGUMENT;
#if OS_LINUX || OS_MAC
  if (address[0] == '/') {
    link->fd = wh_js_socket(address);
    return link->fd < 0 ? FT_STATUS_ERROR : FT_STATUS_OK;
  }
#endif
  ft_local_endpoint endpoint;
  if (!wh_js_parse_endpoint(address, &endpoint))
    return FT_STATUS_INVALID_ARGUMENT;
  return ft_local_connect(&endpoint, &link->local);
}
static void wh_js_link_close(WH_JS_Link *link) {
  ft_local_connection_destroy(&link->local);
#if OS_LINUX || OS_MAC
  if (link->fd >= 0)
    close(link->fd);
#endif
  link->fd = -1;
}
// Each setup call consumes the link on failure and on success, except
// bootstrap, which hands it back for CPU setup.
static ft_status wh_js_link_bootstrap(WH_JS_Link *link, bool input,
                                      ft_input_client **offered,
                                      ft_status *input_status) {
  uint32_t request = input ? FT_BOOTSTRAP_INPUT_OPTIONAL : FT_BOOTSTRAP_INPUT_NONE;
  uint32_t mode = input ? FT_INPUT_MODE_COOPERATIVE : 0;
  if (link->local)
    return ft_source_bootstrap_connect_local(&link->local, request, mode,
                                             offered, input_status);
#if OS_LINUX || OS_MAC
  return ft_source_bootstrap_connect(&link->fd, request, mode, offered,
                                     input_status);
#else
  return FT_STATUS_INVALID_ARGUMENT;
#endif
}
static ft_status wh_js_link_media(WH_JS_Link *link,
                                  ft_cpu_acquisition_connection **out) {
  if (link->local)
    return ft_acquisition_cpu_connection_create_local(&link->local, out);
#if OS_LINUX || OS_MAC
  return ft_acquisition_cpu_connection_create(&link->fd, out);
#else
  return FT_STATUS_INVALID_ARGUMENT;
#endif
}
static ft_status wh_js_link_input(WH_JS_Link *link, ft_input_client **out) {
  if (link->local)
    return ft_input_client_connect_local(&link->local,
                                         FT_INPUT_MODE_COOPERATIVE, out);
#if OS_LINUX || OS_MAC
  return ft_input_client_connect(&link->fd, FT_INPUT_MODE_COOPERATIVE, out);
#else
  return FT_STATUS_INVALID_ARGUMENT;
#endif
}

static void wh_js_close_input(ft_input_client **input, bool *confirmed) {
  if (!*input)
    return;
  *confirmed = false;
  ft_input_client_close(*input);
  uint64_t end = now_time_us() + 2000000ull;
  while (now_time_us() < end) {
    ft_input_status event = {0};
    ft_status result = ft_input_client_poll(*input, &event);
    if (result == FT_STATUS_OK && event.kind == FT_INPUT_CLOSED) {
      *confirmed = event.clean != 0;
      break;
    }
    if (result != FT_STATUS_OK && result != FT_STATUS_EMPTY)
      break;
    wh_js_pause();
  }
  ft_input_client_destroy(input);
}
static void wh_js_media_worker(void *arg) {
  WH_Jackstay *s = arg;
  for (;;) {
    mutex_take(s->mutex);
    bool stop = s->stop, start = s->connect, input = s->want_input;
    unsigned request = s->request;
    if (start) {
      s->connect = false;
      s->state.connecting = true;
      snprintf(s->state.media_status, 128, "Connecting");
    }
    mutex_drop(s->mutex);
    if (stop)
      break;
    if (!start) {
      wh_js_pause();
      continue;
    }
    wh_js_wake(s);
    WH_JS_Link link;
    ft_input_client *offered = 0;
    ft_status input_status = FT_STATUS_EMPTY;
    ft_status result = wh_js_link_open(s->media_address, &link);
    if (result == FT_STATUS_OK && s->bootstrap)
      result = wh_js_link_bootstrap(&link, input, &offered, &input_status);
    ft_cpu_acquisition_connection *connection = 0;
    ft_acquisition_consumer *consumer = 0;
    if (result == FT_STATUS_OK)
      result = wh_js_link_media(&link, &connection);
    wh_js_link_close(&link);
    mutex_take(s->mutex);
    s->connection = connection;
    stop = s->stop;
    mutex_drop(s->mutex);
    if (stop && connection)
      ft_acquisition_cpu_connection_cancel(connection);
    if (result == FT_STATUS_OK)
      result = ft_acquisition_cpu_attach(connection, 1, &consumer);
    mutex_take(s->mutex);
    s->state.connecting = false;
    s->state.connected = result == FT_STATUS_OK;
    snprintf(s->state.media_status, 128,
             result == FT_STATUS_OK ? "Live" : "Disconnected (setup %d)",
             result);
    if (offered && !s->input && !s->stop && result == FT_STATUS_OK) {
      s->input = offered;
      offered = 0;
      uint64_t controller, epoch;
      ft_input_client_describe(s->input, &s->state.input, &controller, &epoch);
      s->peer_closed = false;
      s->state.control = true;
      s->state.input_epoch++;
      s->state.cleanup_confirmed = false;
      snprintf(s->state.input_status, 128, "Control ready");
    } else if (input && s->bootstrap && !s->input) {
      snprintf(s->state.input_status, 128,
               "Control unavailable (%d); retry explicitly", input_status);
    }
    mutex_drop(s->mutex);
    bool ignored = false;
    wh_js_close_input(&offered, &ignored);
    wh_js_wake(s);
    uint64_t cursor = 0;
    while (result == FT_STATUS_OK) {
      mutex_take(s->mutex);
      stop = s->stop || s->request != request;
      mutex_drop(s->mutex);
      if (stop)
        break;
      // A vanished producer closes setup; Jackstay reports that without
      // consuming setup bytes, grants or frame storage.
      if (ft_acquisition_cpu_connection_alive(connection) != FT_STATUS_OK) {
        result = FT_STATUS_CLOSED;
        break;
      }
      ft_acquisition_events observed = {0};
      result = ft_acquisition_snapshot(consumer, &observed);
      if (result != FT_STATUS_OK || observed.closed)
        break;
      ft_acquired_frame *frame = 0;
      ft_acquisition_range range = {0};
      result = ft_acquisition_acquire(consumer, FT_ACQUIRE_LATEST, cursor,
                                      &frame, &range);
      if (result == FT_STATUS_RECONFIGURATION) {
        result = ft_acquisition_cpu_install_configuration(connection, consumer);
        continue;
      }
      if (result == FT_STATUS_EMPTY || result == FT_STATUS_MISS ||
          result == FT_STATUS_GAP) {
        result = FT_STATUS_OK;
        wh_js_pause();
        continue;
      }
      if (result != FT_STATUS_OK)
        break;
      ft_acquired_frame_descriptor desc = {0};
      const uint8_t *bytes = 0;
      size_t length = 0;
      result = ft_acquired_frame_describe(frame, &desc);
      if (result == FT_STATUS_OK)
        result = ft_acquired_frame_bytes(frame, &bytes, &length);
      size_t row = (size_t)desc.width * 4, size = row * desc.height;
      bool valid = result == FT_STATUS_OK && desc.width && desc.height &&
                   desc.width <= 16384 && desc.height <= 16384 &&
                   size <= WH_JS_MAX_FRAME && desc.stride >= row &&
                   (uint64_t)(desc.height - 1) * desc.stride + row <= length &&
                   (desc.pixel_format == FT_PIXEL_FORMAT_RGBA8_UNORM ||
                    desc.pixel_format == FT_PIXEL_FORMAT_BGRA8_UNORM);
      uint8_t *copy = valid ? malloc(size) : 0;
      if (copy)
        for (uint32_t y = 0; y < desc.height; ++y) {
          memcpy(copy + y * row, bytes + (size_t)y * desc.stride, row);
          if (desc.pixel_format == FT_PIXEL_FORMAT_BGRA8_UNORM)
            for (size_t x = 0; x < row; x += 4) {
              uint8_t r = copy[y * row + x];
              copy[y * row + x] = copy[y * row + x + 2];
              copy[y * row + x + 2] = r;
            }
        }
      cursor = desc.cursor;
      ft_acquired_frame_release(&frame);
      if (!copy) {
        result = FT_STATUS_ERROR;
        break;
      }
      mutex_take(s->mutex);
      free(s->pixels);
      s->pixels = copy;
      s->state.width = desc.width;
      s->state.height = desc.height;
      s->state.frame_version++;
      mutex_drop(s->mutex);
      wh_js_wake(s);
    }
    mutex_take(s->mutex);
    s->connection = 0;
    s->state.connected = false;
    if (!s->stop) {
      snprintf(s->state.media_status, 128, "Disconnected; reconnecting video");
      if (s->input)
        s->input_close = true;
      /* Recovery never reacquires control without a new explicit request. */
      if (s->request == request) {
        s->want_input = false;
        s->connect = true;
      }
    }
    mutex_drop(s->mutex);
    ft_acquisition_consumer_destroy(&consumer);
    ft_acquisition_cpu_connection_destroy(&connection);
    wh_js_wake(s);
    for (int i = 0; i < 200; ++i) {
      mutex_take(s->mutex);
      stop = s->stop;
      mutex_drop(s->mutex);
      if (stop)
        break;
      wh_js_pause();
    }
  }
  mutex_take(s->mutex);
  s->media_done = true;
  mutex_drop(s->mutex);
  wh_js_wake(s);
}
static void wh_js_input_worker(void *arg) {
  WH_Jackstay *s = arg;
  uint64_t in_flight[32] = {0};
  uint32_t actions[32] = {0};
  size_t outstanding = 0;
  for (;;) {
    mutex_take(s->mutex);
    if (!s->bootstrap && s->want_input && !s->input && !s->stop &&
        s->input_address[0]) {
      s->want_input = false;
      mutex_drop(s->mutex);
      WH_JS_Link link;
      ft_input_client *client = 0;
      ft_status result = wh_js_link_open(s->input_address, &link);
      if (result == FT_STATUS_OK)
        result = wh_js_link_input(&link, &client);
      wh_js_link_close(&link);
      mutex_take(s->mutex);
      s->peer_closed = false;
      s->input = client;
      s->state.control = client != 0;
      s->state.input_epoch++;
      s->state.cleanup_confirmed = false;
      if (client) {
        uint64_t controller, epoch;
        ft_input_client_describe(client, &s->state.input, &controller, &epoch);
      }
      snprintf(s->state.input_status, 128,
               client ? "Control ready"
                      : "Control unavailable (%d); retry explicitly",
               result);
      wh_js_wake(s);
    }
    if (s->stop && s->media_done && !s->input) {
      s->input_done = true;
      mutex_drop(s->mutex);
      break;
    }
    if ((s->stop && s->input) || s->input_close) {
      s->input_close = false;
      s->want_input = false;
      s->state.control = false;
      wh_js_clear(s);
      ft_input_client *client = s->input;
      s->input = 0;
      outstanding = 0;
      bool confirmed = s->state.cleanup_confirmed, peer_closed = s->peer_closed;
      s->peer_closed = false;
      mutex_drop(s->mutex);
      if (peer_closed)
        ft_input_client_destroy(&client);
      else
        wh_js_close_input(&client, &confirmed);
      mutex_take(s->mutex);
      s->state.cleanup_confirmed = confirmed;
      s->state.resetting = false;
      s->state.input_epoch++;
      snprintf(s->state.input_status, 128,
               confirmed ? "Control released; click to resume"
                         : "Control disconnected; cleanup unconfirmed");
      wh_js_wake(s);
      if (s->stop && s->media_done) {
        s->input_done = true;
        mutex_drop(s->mutex);
        break;
      }
    }
    if (s->input && s->reset) {
      s->reset = false;
      wh_js_clear(s);
      s->state.resetting = true;
      if (ft_input_client_reset(s->input) != FT_STATUS_OK)
        s->input_close = true;
    }
    if (s->input) {
      ft_input_status event = {0};
      ft_status poll_status;
      while ((poll_status = ft_input_client_poll(s->input, &event)) ==
             FT_STATUS_OK) {
        if (event.kind == FT_INPUT_RESET) {
          s->state.input.geometry = event.geometry;
          if (event.reason == FT_INPUT_REASON_GEOMETRY) {
            // Only pointer work belongs to the old mapping; key bindings
            // survive.
            s->state.pointer_epoch++;
            size_t kept = 0;
            for (size_t i = 0; i < s->count; ++i) {
              if (s->pending[i].event.kind == FT_INPUT_KEY ||
                  s->pending[i].event.kind == FT_INPUT_TEXT)
                s->pending[kept++] = s->pending[i];
              else
                free(s->pending[i].text);
            }
            s->count = kept;
          } else {
            s->state.input_epoch++;
            wh_js_clear(s);
            outstanding = 0;
          }
          s->state.resetting = false;
        } else if (event.kind == FT_INPUT_CLOSED) {
          s->peer_closed = true;
          s->state.cleanup_confirmed = event.clean != 0;
          s->input_close = true;
          break;
        } else if (event.kind == FT_INPUT_COMPLETED ||
                   event.kind == FT_INPUT_REFUSED) {
          for (size_t i = 0; i < outstanding; ++i)
            if (in_flight[i] == event.sequence) {
              if (event.result == FT_INPUT_PARTIAL ||
                  event.result == FT_INPUT_UNCERTAIN ||
                  (event.result != FT_INPUT_EXECUTED &&
                   actions[i] == FT_INPUT_UP))
                s->input_close = true;
              if (event.result != FT_INPUT_EXECUTED)
                snprintf(s->state.input_status, 128, "Input rejected (%d)",
                         event.result);
              in_flight[i] = in_flight[--outstanding];
              actions[i] = actions[outstanding];
              break;
            }
        }
        wh_js_wake(s);
      }
      if (poll_status != FT_STATUS_EMPTY && poll_status != FT_STATUS_OK)
        s->input_close = true;
      if (!s->input_close && !s->state.resetting && s->focus &&
          outstanding < 32 && s->count) {
        WH_JS_Pending pending = s->pending[0];
        memmove(s->pending, s->pending + 1,
                (--s->count) * sizeof(s->pending[0]));
        s->text_bytes -= pending.event.text_len;
        uint64_t sequence = 0;
        ft_status result =
            ft_input_client_send(s->input, &pending.event, &sequence);
        if (result == FT_STATUS_OK) {
          in_flight[outstanding] = sequence;
          actions[outstanding++] = pending.event.action;
        } else {
          s->input_close = true;
          snprintf(s->state.input_status, 128, "Input send failed (%d)",
                   result);
        }
        free(pending.text);
      }
    }
    mutex_drop(s->mutex);
    wh_js_pause();
  }
  wh_js_wake(s);
}
static char *wh_js_copy_string(const char *source) {
  size_t size = strlen(source) + 1;
  char *copy = malloc(size);
  if (copy)
    memcpy(copy, source, size);
  return copy;
}
static void wh_js_free(WH_Jackstay *s) {
  if (!MemoryIsZeroStruct(&s->mutex))
    mutex_release(s->mutex);
  free(s->pixels);
  free(s->media_address);
  free(s->input_address);
  free(s);
}
WH_Jackstay *wh_js_create(WH_JS_Endpoint endpoint, void (*wake)(void *),
                          void *context) {
  if (ft_abi_version() != FT_ABI_VERSION || !endpoint.media)
    return 0;
  WH_Jackstay *s = calloc(1, sizeof(*s));
  if (!s)
    return 0;
  s->mutex = mutex_alloc();
  s->media_address = wh_js_copy_string(endpoint.media);
  s->input_address = wh_js_copy_string(endpoint.input ? endpoint.input : "");
  if (!s->media_address || !s->input_address ||
      MemoryIsZeroStruct(&s->mutex)) {
    wh_js_free(s);
    return 0;
  }
  s->bootstrap = endpoint.bootstrap;
  s->wake = wake;
  s->wake_context = context;
  snprintf(s->state.media_status, 128, "Not connected");
  snprintf(s->state.input_status, 128, "Observation only");
  s->media_thread = thread_launch(wh_js_media_worker, s);
  if (MemoryIsZeroStruct(&s->media_thread)) {
    wh_js_free(s);
    return 0;
  }
  s->input_thread = thread_launch(wh_js_input_worker, s);
  if (MemoryIsZeroStruct(&s->input_thread)) {
    mutex_take(s->mutex);
    s->stop = true;
    mutex_drop(s->mutex);
    thread_join(s->media_thread, max_U64);
    wh_js_free(s);
    return 0;
  }
  return s;
}
void wh_js_connect(WH_Jackstay *s, bool control) {
  mutex_take(s->mutex);
  s->want_input = control;
  if (!s->state.connected || (s->bootstrap && control && !s->input)) {
    s->request++;
    s->connect = true;
    if (s->connection)
      ft_acquisition_cpu_connection_cancel(s->connection);
  }
  mutex_drop(s->mutex);
  wh_js_wake(s);
}
void wh_js_focus(WH_Jackstay *s, bool focus) {
  mutex_take(s->mutex);
  if (s->focus && !focus) {
    s->reset = true;
    s->state.resetting = true;
    wh_js_clear(s);
  }
  s->focus = focus;
  mutex_drop(s->mutex);
}
bool wh_js_send(WH_Jackstay *s, const ft_input_event *event) {
  mutex_take(s->mutex);
  bool ok = s->input && s->state.control && !s->state.resetting &&
            !s->input_close && s->focus && !s->stop;
  if (ok && (s->count == WH_JS_QUEUE || event->text_len > WH_JS_TEXT ||
             s->text_bytes + event->text_len > 65536)) {
    s->input_close = true;
    ok = false;
  }
  if (ok) {
    WH_JS_Pending pending = {.event = *event};
    if (event->text_len) {
      pending.text = malloc(event->text_len);
      if (!pending.text) {
        s->input_close = true;
        ok = false;
      } else
        memcpy(pending.text, event->text, event->text_len);
    }
    if (ok) {
      pending.event.text = pending.text;
      s->pending[s->count++] = pending;
      s->text_bytes += event->text_len;
    }
  }
  mutex_drop(s->mutex);
  return ok;
}
void wh_js_snapshot(WH_Jackstay *s, WH_JS_State *state, WH_JS_Frame *frame,
                    uint64_t after) {
  mutex_take(s->mutex);
  *state = s->state;
  state->stopped = s->media_done && s->input_done;
  if (frame) {
    memset(frame, 0, sizeof(*frame));
    if (s->pixels && s->state.frame_version != after) {
      size_t size = (size_t)s->state.width * s->state.height * 4;
      frame->pixels = malloc(size);
      if (frame->pixels) {
        memcpy(frame->pixels, s->pixels, size);
        frame->width = s->state.width;
        frame->height = s->state.height;
        frame->version = s->state.frame_version;
      }
    }
  }
  mutex_drop(s->mutex);
}
void wh_js_stop(WH_Jackstay *s) {
  mutex_take(s->mutex);
  s->stop = true;
  s->state.control = false;
  wh_js_clear(s);
  if (s->connection)
    ft_acquisition_cpu_connection_cancel(s->connection);
  mutex_drop(s->mutex);
}
bool wh_js_destroy(WH_Jackstay **owner) {
  WH_Jackstay *s = *owner;
  if (!s)
    return true;
  mutex_take(s->mutex);
  bool done = s->media_done && s->input_done;
  mutex_drop(s->mutex);
  if (!done)
    return false;
  thread_join(s->media_thread, max_U64);
  thread_join(s->input_thread, max_U64);
  wh_js_free(s);
  *owner = 0;
  return true;
}
