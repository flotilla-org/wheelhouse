// Integration probe for the same C ABI consumed by Wheelhouse.
// Usage: probe inprocess|daemon 'producer command' [existing-session-id]
// Daemon mode uses CLEAT_RUNTIME_DIR and daemon name wh-image-baseline.
#include <cleat_provider.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct Counts {
  size_t updates, resources, placements, lookups, bytes;
  uint64_t max_offset;
  uint32_t viewport;
} Counts;
static bool bytes(void *ctx, const uint8_t *data, size_t len) {
  ((Counts *)ctx)->bytes += len;
  return true;
}
static void poll_view(cleat_session *session, Counts *counts) {
  cleat_session_poll(session);
  cleat_render_update update = {0};
  if (cleat_session_render_update(session, &update)) {
    counts->updates++;
    counts->viewport = update.viewport_kind;
    if (update.scrollback_offset_rows > counts->max_offset)
      counts->max_offset = update.scrollback_offset_rows;
    counts->resources += update.image_resource_count;
    counts->placements += update.image_placement_count;
    for (size_t i = 0; i < update.image_resource_count; i++) {
      const cleat_image_resource *resource = &update.image_resources[i];
      counts->lookups += cleat_session_with_image_resource_data(
          session, resource->image_id, resource->generation, bytes, counts);
    }
    cleat_session_mark_observed(session, update.render_generation);
    cleat_session_release_render_update(session, &update);
  }
}
static void report(const char *name, cleat_session *session, Counts counts) {
  printf("%s state=%u role=%u updates=%zu resources=%zu "
         "placements=%zu successful_lookups=%zu bytes=%zu viewport=%u "
         "max_offset=%llu\n",
         name, cleat_session_connection_state(session),
         cleat_session_role(session), counts.updates, counts.resources,
         counts.placements, counts.lookups, counts.bytes, counts.viewport,
         (unsigned long long)counts.max_offset);
}
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  int daemon = !strcmp(argv[1], "daemon");
  cleat_provider_desc provider_desc = {
      .abi_version = CLEAT_PROVIDER_ABI_VERSION,
      .requested_features = CLEAT_PROVIDER_FEATURE_RENDER_UPDATES |
                            CLEAT_PROVIDER_FEATURE_IMAGE_STATE,
      .backend = daemon ? CLEAT_PROVIDER_BACKEND_DAEMON
                        : CLEAT_PROVIDER_BACKEND_IN_PROCESS,
      .daemon_name = (const uint8_t *)"wh-image-baseline",
      .daemon_name_len = 17};
  cleat_provider *provider = cleat_provider_open(&provider_desc);
  if (!provider)
    return 3;
  cleat_session_desc session_desc = {.cols = 100,
                                     .rows = 40,
                                     .cell_width_px = 10,
                                     .cell_height_px = 20,
                                     .vt_engine = CLEAT_PROVIDER_VT_GHOSTTY,
                                     .command = (const uint8_t *)argv[2],
                                     .command_len = strlen(argv[2]),
                                     .record = true};
  if (argc > 3) {
    session_desc.id = (const uint8_t *)argv[3];
    session_desc.id_len = strlen(argv[3]);
  }
  cleat_session *session = argc > 3
                               ? cleat_session_attach(provider, &session_desc)
                               : cleat_session_create(provider, &session_desc);
  if (!session)
    return 4;
  Counts primary_counts = {0}, second_counts = {0}, reconnected = {0},
         history = {0};
  int saw_streaming = 0, saw_disconnect = 0, saw_recovery = 0;
  cleat_provider *second_provider = 0;
  cleat_session *second_session = 0;
  for (int i = 0; i < 800; i++) {
    poll_view(session, &primary_counts);
    uint32_t connection = cleat_session_connection_state(session);
    if (connection == CLEAT_SESSION_STREAMING) {
      if (saw_disconnect)
        saw_recovery = 1;
      saw_streaming = 1;
    }
    if (saw_streaming && connection == CLEAT_SESSION_DISCONNECTED)
      saw_disconnect = 1;
    if (i == 200 && daemon) {
      cleat_str id = {0};
      cleat_session_id(session, &id);
      session_desc.id = id.ptr;
      session_desc.id_len = id.len;
      second_provider = cleat_provider_open(&provider_desc);
      second_session = cleat_session_attach(second_provider, &session_desc);
    }
    if (second_session)
      poll_view(second_session, i < 400 ? &second_counts : &reconnected);
    if (i == 399 && second_session) {
      report("second-viewer", second_session, second_counts);
      cleat_session_destroy(second_session);
      cleat_provider_close(second_provider);
      second_provider = cleat_provider_open(&provider_desc);
      second_session = cleat_session_attach(second_provider, &session_desc);
    }
    usleep(10000);
  }
  report(argv[1], session, primary_counts);
  printf("transport disconnected=%d recovered=%d\n", saw_disconnect,
         saw_recovery);
  if (second_session)
    report("reattached-viewer", second_session, reconnected);
  cleat_viewport_command scroll_command = {.kind = CLEAT_VIEWPORT_COMMAND_TOP};
  cleat_viewport_command_result result = {0};
  printf("scroll accepted=%d ",
         cleat_session_scroll_viewport(session, &scroll_command, &result));
  printf("outcome=%u\n", result.outcome);
  for (int i = 0; i < 100; i++) {
    poll_view(session, &history);
    usleep(10000);
  }
  report("history/top", session, history);
  int ok = primary_counts.updates > 0 &&
           cleat_session_connection_state(session) == CLEAT_SESSION_STREAMING &&
           (!daemon || (second_counts.updates > 0 && reconnected.updates > 0 &&
                        second_session &&
                        cleat_session_connection_state(second_session) ==
                            CLEAT_SESSION_STREAMING));
  if (getenv("WH_EXPECT_RECONNECT"))
    ok = ok && saw_disconnect && saw_recovery;
  if (second_session) {
    cleat_session_destroy(second_session);
    cleat_provider_close(second_provider);
  }
  cleat_session_destroy(session);
  cleat_provider_close(provider);
  return ok ? 0 : 5;
}
