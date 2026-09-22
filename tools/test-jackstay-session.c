// Separate-process acceptance against Jackstay's independent reference source.
#include "jackstay/wheelhouse_jackstay.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static WH_JS_State wait_state(WH_Jackstay *session, int mode) {
  WH_JS_State state = {0};
  for (int i = 0; i < 2000; ++i) {
    wh_js_snapshot(session, &state, 0, 0);
    if ((mode == 0 && state.connected && state.frame_version &&
         state.control) ||
        (mode == 1 && !state.resetting) || (mode == 2 && state.stopped))
      return state;
    usleep(5000);
  }
  fprintf(stderr, "timeout: %s / %s mode=%d\n", state.media_status,
          state.input_status, mode);
  abort();
}
// The harness acknowledges the independent source's state report. Queueing an
// event locally does not prove it reached the source before reset or shutdown.
static void wait_source(const char *marker) {
  puts(marker);
  fflush(stdout);
  assert(getchar() == '\n');
}
int main(int argc, char **argv) {
  assert(argc >= 2);
  const char *mode = argc > 2 ? argv[2] : "input";
  WH_Jackstay *session =
      wh_js_create((WH_JS_Endpoint){argv[1], "", true}, 0, 0);
  assert(session);
  WH_JS_State state;
  wh_js_snapshot(session, &state, 0, 0);
  assert(!state.connected && !state.control);
  if (!strcmp(mode, "missing")) {
    wh_js_connect(session, true);
    usleep(200000);
    wh_js_stop(session);
    wait_state(session, 2);
    assert(wh_js_destroy(&session));
    puts("Missing endpoint closes cleanly");
    return 0;
  }
  if (!strcmp(mode, "observe") || !strcmp(mode, "refused")) {
    wh_js_connect(session, !strcmp(mode, "refused"));
    WH_JS_Frame frame = {0};
    for (int i = 0; i < 2000 && !frame.pixels; ++i) {
      wh_js_snapshot(session, &state, &frame, 0);
      usleep(5000);
    }
    assert(frame.pixels && !state.control);
    free(frame.pixels);
    wh_js_stop(session);
    wait_state(session, 2);
    assert(wh_js_destroy(&session));
    puts("Observation passed");
    return 0;
  }
  wh_js_connect(session, true);
  state = wait_state(session, 0);
  assert(state.input.geometry.width > 0);
  if (!strcmp(mode, "recovery")) {
    uint64_t version = state.frame_version;
    puts("restart-source");
    fflush(stdout);
    int disconnected = 0, recovered = 0;
    for (int i = 0; i < 3000; ++i) {
      wh_js_snapshot(session, &state, 0, 0);
      if (!state.connected)
        disconnected = 1;
      if (disconnected && state.connected && state.frame_version > version) {
        recovered = 1;
        break;
      }
      usleep(5000);
    }
    assert(recovered && !state.control);
    wh_js_stop(session);
    wait_state(session, 2);
    assert(wh_js_destroy(&session));
    puts("Video recovery without control passed");
    return 0;
  }

  WH_JS_Frame frame = {0};
  wh_js_snapshot(session, &state, &frame, 0);
  assert(frame.pixels && frame.width && frame.height);
  free(frame.pixels);
  ft_input_event key = {.kind = FT_INPUT_KEY,
                        .action = FT_INPUT_DOWN,
                        .key_kind = FT_INPUT_PHYSICAL_KEY,
                        .press = 1};
  strcpy(key.key, "KeyA");
  assert(!wh_js_send(session, &key)); // observation cannot emit input
  wh_js_focus(session, true);
  assert(wh_js_send(session, &key));
  key.action = FT_INPUT_REPEAT;
  assert(wh_js_send(session, &key));
  key.action = FT_INPUT_UP;
  assert(wh_js_send(session, &key));
  ft_input_event text = {.kind = FT_INPUT_TEXT,
                         .text = (const uint8_t *)"Hello \xc3\xa9",
                         .text_len = 8};
  assert(wh_js_send(session, &text));
  ft_input_event button = {.kind = FT_INPUT_BUTTON,
                           .action = FT_INPUT_DOWN,
                           .button = 1,
                           .geometry_revision = state.input.geometry.revision,
                           .x = 12,
                           .y = 14};
  assert(wh_js_send(session, &button));
  wait_source("await-button-down");
  wh_js_focus(session, false);
  state = wait_state(session, 1);
  assert(state.control); // reset retains assignment
  assert(!wh_js_send(session, &text));
  wh_js_focus(session, true);
  key.action = FT_INPUT_DOWN;
  key.press = 2;
  assert(wh_js_send(session, &key));
  wait_source("await-key-down");
  wh_js_stop(session);
  state = wait_state(session, 2);
  assert(state.cleanup_confirmed);
  assert(wh_js_destroy(&session) && !session);
  puts("Jackstay media/input/reset/cleanup passed");
  return 0;
}
