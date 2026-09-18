#include "wheelhouse_jackstay.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define WH_JS_MAX_FRAME (64u * 1024u * 1024u)
#define WH_JS_QUEUE 128
#define WH_JS_TEXT 16384

typedef struct {
  ft_input_event event;
  uint8_t *text;
} WH_JS_Pending;
struct WH_Jackstay {
  pthread_mutex_t mutex;
  pthread_t media_thread, input_thread;
  char *media_path, *input_path;
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
static void wh_js_pause(void) {
  struct timespec delay = {0, 5000000};
  nanosleep(&delay, 0);
}
static uint64_t wh_js_now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}
static void wh_js_clear(WH_Jackstay *s) {
  for (size_t i = 0; i < s->count; ++i)
    free(s->pending[i].text);
  s->count = 0;
  s->text_bytes = 0;
}
static int wh_js_socket(const char *path) {
  struct sockaddr_un address = {.sun_family = AF_UNIX};
  if (!path || path[0] != '/' || strlen(path) >= sizeof(address.sun_path))
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
static void wh_js_close_input(ft_input_client **input, bool *confirmed) {
  if (!*input)
    return;
  *confirmed = false;
  ft_input_client_close(*input);
  uint64_t end = wh_js_now() + 2000000000ull;
  while (wh_js_now() < end) {
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
static void *wh_js_media_worker(void *arg) {
  // Rust executables ignore SIGPIPE, but a C host does not. Confine this to
  // transport workers (including the library workers they spawn).
  sigset_t blocked;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &blocked, 0);
  WH_Jackstay *s = arg;
  for (;;) {
    pthread_mutex_lock(&s->mutex);
    bool stop = s->stop, start = s->connect, input = s->want_input;
    unsigned request = s->request;
    if (start) {
      s->connect = false;
      s->state.connecting = true;
      snprintf(s->state.media_status, 128, "Connecting");
    }
    pthread_mutex_unlock(&s->mutex);
    if (stop)
      break;
    if (!start) {
      wh_js_pause();
      continue;
    }
    wh_js_wake(s);
    int fd = wh_js_socket(s->media_path);
    ft_input_client *offered = 0;
    ft_status input_status = FT_STATUS_EMPTY;
    ft_status result = fd < 0 ? FT_STATUS_ERROR : FT_STATUS_OK;
    if (result == FT_STATUS_OK && s->bootstrap)
      result = ft_source_bootstrap_connect(
          &fd, input ? FT_BOOTSTRAP_INPUT_OPTIONAL : FT_BOOTSTRAP_INPUT_NONE,
          input ? FT_INPUT_MODE_COOPERATIVE : 0, &offered, &input_status);
    // Borrow only for hangup polling; the connection exclusively owns this FD.
    int setup_fd = fd;
    ft_cpu_acquisition_connection *connection = 0;
    ft_acquisition_consumer *consumer = 0;
    if (result == FT_STATUS_OK)
      result = ft_acquisition_cpu_connection_create(&fd, &connection);
    if (fd >= 0)
      close(fd);
    pthread_mutex_lock(&s->mutex);
    s->connection = connection;
    stop = s->stop;
    pthread_mutex_unlock(&s->mutex);
    if (stop && connection)
      ft_acquisition_cpu_connection_cancel(connection);
    if (result == FT_STATUS_OK)
      result = ft_acquisition_cpu_attach(connection, 1, &consumer);
    pthread_mutex_lock(&s->mutex);
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
    pthread_mutex_unlock(&s->mutex);
    bool ignored = false;
    wh_js_close_input(&offered, &ignored);
    wh_js_wake(s);
    uint64_t cursor = 0;
    while (result == FT_STATUS_OK) {
      pthread_mutex_lock(&s->mutex);
      stop = s->stop || s->request != request;
      pthread_mutex_unlock(&s->mutex);
      if (stop)
        break;
      struct pollfd setup_poll = {.fd = setup_fd, .events = POLLIN};
      if (poll(&setup_poll, 1, 0) > 0 &&
          (setup_poll.revents & (POLLHUP | POLLERR | POLLNVAL))) {
        result = FT_STATUS_CLOSED;
        break;
      }
      // As in KS, an EOF peek does not consume grants or retire frame storage.
      // This runs only between serialized setup/configuration operations.
      if (setup_poll.revents & POLLIN) {
        unsigned char byte;
        if (recv(setup_fd, &byte, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
          result = FT_STATUS_CLOSED;
          break;
        }
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
      pthread_mutex_lock(&s->mutex);
      free(s->pixels);
      s->pixels = copy;
      s->state.width = desc.width;
      s->state.height = desc.height;
      s->state.frame_version++;
      pthread_mutex_unlock(&s->mutex);
      wh_js_wake(s);
    }
    pthread_mutex_lock(&s->mutex);
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
    pthread_mutex_unlock(&s->mutex);
    ft_acquisition_consumer_destroy(&consumer);
    ft_acquisition_cpu_connection_destroy(&connection);
    wh_js_wake(s);
    for (int i = 0; i < 200; ++i) {
      pthread_mutex_lock(&s->mutex);
      stop = s->stop;
      pthread_mutex_unlock(&s->mutex);
      if (stop)
        break;
      wh_js_pause();
    }
  }
  pthread_mutex_lock(&s->mutex);
  s->media_done = true;
  pthread_mutex_unlock(&s->mutex);
  wh_js_wake(s);
  return 0;
}
static void *wh_js_input_worker(void *arg) {
  // Rust executables ignore SIGPIPE, but a C host does not. Confine this to
  // transport workers (including the library workers they spawn).
  sigset_t blocked;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &blocked, 0);
  WH_Jackstay *s = arg;
  uint64_t in_flight[32] = {0};
  uint32_t actions[32] = {0};
  size_t outstanding = 0;
  for (;;) {
    pthread_mutex_lock(&s->mutex);
    if (!s->bootstrap && s->want_input && !s->input && !s->stop &&
        s->input_path[0]) {
      s->want_input = false;
      pthread_mutex_unlock(&s->mutex);
      int fd = wh_js_socket(s->input_path);
      ft_input_client *client = 0;
      ft_status result = fd < 0 ? FT_STATUS_ERROR
                                : ft_input_client_connect(
                                      &fd, FT_INPUT_MODE_COOPERATIVE, &client);
      if (fd >= 0)
        close(fd);
      pthread_mutex_lock(&s->mutex);
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
      pthread_mutex_unlock(&s->mutex);
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
      pthread_mutex_unlock(&s->mutex);
      if (peer_closed)
        ft_input_client_destroy(&client);
      else
        wh_js_close_input(&client, &confirmed);
      pthread_mutex_lock(&s->mutex);
      s->state.cleanup_confirmed = confirmed;
      s->state.resetting = false;
      s->state.input_epoch++;
      snprintf(s->state.input_status, 128,
               confirmed ? "Control released; click to resume"
                         : "Control disconnected; cleanup unconfirmed");
      wh_js_wake(s);
      if (s->stop && s->media_done) {
        s->input_done = true;
        pthread_mutex_unlock(&s->mutex);
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
    pthread_mutex_unlock(&s->mutex);
    wh_js_pause();
  }
  wh_js_wake(s);
  return 0;
}
static char *wh_js_copy_string(const char *source) {
  size_t size = strlen(source) + 1;
  char *copy = malloc(size);
  if (copy)
    memcpy(copy, source, size);
  return copy;
}
WH_Jackstay *wh_js_create(WH_JS_Endpoint endpoint, void (*wake)(void *),
                          void *context) {
  if (ft_abi_version() != FT_ABI_VERSION || !endpoint.media_path)
    return 0;
  WH_Jackstay *s = calloc(1, sizeof(*s));
  if (!s)
    return 0;
  s->media_path = wh_js_copy_string(endpoint.media_path);
  s->input_path =
      wh_js_copy_string(endpoint.input_path ? endpoint.input_path : "");
  if (!s->media_path || !s->input_path) {
    free(s->media_path);
    free(s->input_path);
    free(s);
    return 0;
  }
  pthread_mutex_init(&s->mutex, 0);
  s->bootstrap = endpoint.bootstrap;
  s->wake = wake;
  s->wake_context = context;
  snprintf(s->state.media_status, 128, "Not connected");
  snprintf(s->state.input_status, 128, "Observation only");
  if (pthread_create(&s->media_thread, 0, wh_js_media_worker, s) != 0) {
    pthread_mutex_destroy(&s->mutex);
    free(s->media_path);
    free(s->input_path);
    free(s);
    return 0;
  }
  if (pthread_create(&s->input_thread, 0, wh_js_input_worker, s) != 0) {
    pthread_mutex_lock(&s->mutex);
    s->stop = true;
    pthread_mutex_unlock(&s->mutex);
    pthread_join(s->media_thread, 0);
    pthread_mutex_destroy(&s->mutex);
    free(s->media_path);
    free(s->input_path);
    free(s);
    return 0;
  }
  return s;
}
void wh_js_connect(WH_Jackstay *s, bool control) {
  pthread_mutex_lock(&s->mutex);
  s->want_input = control;
  if (!s->state.connected || (s->bootstrap && control && !s->input)) {
    s->request++;
    s->connect = true;
    if (s->connection)
      ft_acquisition_cpu_connection_cancel(s->connection);
  }
  pthread_mutex_unlock(&s->mutex);
  wh_js_wake(s);
}
void wh_js_focus(WH_Jackstay *s, bool focus) {
  pthread_mutex_lock(&s->mutex);
  if (s->focus && !focus) {
    s->reset = true;
    s->state.resetting = true;
    wh_js_clear(s);
  }
  s->focus = focus;
  pthread_mutex_unlock(&s->mutex);
}
bool wh_js_send(WH_Jackstay *s, const ft_input_event *event) {
  pthread_mutex_lock(&s->mutex);
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
  pthread_mutex_unlock(&s->mutex);
  return ok;
}
void wh_js_snapshot(WH_Jackstay *s, WH_JS_State *state, WH_JS_Frame *frame,
                    uint64_t after) {
  pthread_mutex_lock(&s->mutex);
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
  pthread_mutex_unlock(&s->mutex);
}
void wh_js_stop(WH_Jackstay *s) {
  pthread_mutex_lock(&s->mutex);
  s->stop = true;
  s->state.control = false;
  wh_js_clear(s);
  if (s->connection)
    ft_acquisition_cpu_connection_cancel(s->connection);
  pthread_mutex_unlock(&s->mutex);
}
bool wh_js_destroy(WH_Jackstay **owner) {
  WH_Jackstay *s = *owner;
  if (!s)
    return true;
  pthread_mutex_lock(&s->mutex);
  bool done = s->media_done && s->input_done;
  pthread_mutex_unlock(&s->mutex);
  if (!done)
    return false;
  pthread_join(s->media_thread, 0);
  pthread_join(s->input_thread, 0);
  pthread_mutex_destroy(&s->mutex);
  free(s->pixels);
  free(s->media_path);
  free(s->input_path);
  free(s);
  *owner = 0;
  return true;
}
