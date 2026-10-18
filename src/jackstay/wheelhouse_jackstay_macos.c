// Experimental native macOS consumer. Reuses the common mailbox and deferred
// release path. The Mach service is explicitly supplied by the launch profile.
#pragma push_macro("internal")
#pragma push_macro("global")
#pragma push_macro("FileInfo")
#undef internal
#undef global
#define FileInfo MACOS_SDK_FileInfo
#import <Metal/Metal.h>
#undef FileInfo
#pragma pop_macro("FileInfo")
#pragma pop_macro("global")
#pragma pop_macro("internal")
#if defined(nil)
#undef nil
#endif

static ft_status wh_js_macos_wait_ready(WH_Jackstay *s,
                                        id<MTLSharedEvent> event,
                                        uint64_t value, unsigned request,
                                        uint64_t timeout_us) {
  uint64_t deadline = now_time_us() + timeout_us;
  for (;;) {
    mutex_take(s->mutex);
    bool stop = s->stop || s->request != request || s->import_refused;
    mutex_drop(s->mutex);
    if (stop)
      return FT_STATUS_CLOSED;
    if (event.signaledValue >= value)
      return FT_STATUS_OK;
    if (now_time_us() >= deadline)
      return FT_STATUS_TIMEOUT;
    sleep_ms(1);
  }
}

static ft_status wh_js_macos_run(WH_Jackstay *s, bool input, unsigned request) {
  ft_macos_acquisition_connection *connection = 0;
  ft_acquisition_consumer *consumer = 0;
  ft_acquisition_release_timeline *timeline = 0;
  ft_acquisition_cancellation *cancel = 0;
  ft_affordances_host *affordances = 0;
  ft_input_client *offered = 0;
  ft_status input_status = FT_STATUS_EMPTY;
  ft_status result = s->gpu.device && s->gpu.release_fence
                         ? FT_STATUS_OK
                         : FT_STATUS_UNSUPPORTED;
  WH_JS_Link control = {0, -1};
  if (result == FT_STATUS_OK && s->input_address && s->input_address[0]) {
    result = wh_js_link_open(s->input_address, false, &control);
    if (result == FT_STATUS_OK)
      result = wh_js_link_tracks(&control, false, input, &offered,
                                 &input_status, &affordances);
    wh_js_link_close(&control);
  }
  if (result == FT_STATUS_OK)
    result = ft_acquisition_macos_connect(
        s->media_address, s->token && s->token[0] ? s->token : 0, 3,
        &connection, &consumer);
  if (result == FT_STATUS_OK)
    result = ft_acquisition_macos_register_release(
        connection, consumer, s->gpu.release_fence, &timeline);
  if (result == FT_STATUS_OK)
    result = ft_acquisition_cancellation_create(&cancel);
  mutex_take(s->mutex);
  uint64_t incarnation = ++s->incarnation;
  if (timeline) {
    WH_JS_Timeline *record = calloc(1, sizeof(*record));
    if (record) {
      record->incarnation = incarnation;
      record->timeline = timeline;
      record->next = s->timelines;
      s->timelines = record;
    } else {
      ft_acquisition_release_timeline_destroy(&timeline);
      result = FT_STATUS_ERROR;
    }
  }
  s->state.connecting = false;
  s->state.connected = result == FT_STATUS_OK;
  snprintf(s->state.media_status, 128,
           result == FT_STATUS_OK ? "Live" : "Disconnected (setup %d)", result);
  if (result == FT_STATUS_OK) {
    s->state.path = WH_JS_PATH_IMPORT;
    snprintf(s->state.path_status, sizeof(s->state.path_status),
             "Metal IOSurface import");
    s->state.affordances = affordances != 0;
    s->state.affordance_version++;
    s->presentation_dirty = affordances && s->presented;
    if (offered && !s->input && !s->stop) {
      s->input = offered;
      offered = 0;
      uint64_t controller, epoch;
      ft_input_client_describe(s->input, &s->state.input, &controller, &epoch);
      s->peer_closed = false;
      s->state.control = true;
      s->state.input_epoch++;
      s->state.cleanup_confirmed = false;
      snprintf(s->state.input_status, 128, "Control ready");
    }
  }
  mutex_drop(s->mutex);
  bool ignored = false;
  wh_js_close_input(&offered, &ignored);
  wh_js_wake(s);
  uint64_t cursor = 0, readiness_id = 0;
  id<MTLSharedEvent> readiness_event = 0;
  while (result == FT_STATUS_OK) {
    mutex_take(s->mutex);
    bool stop = s->stop || s->request != request || s->import_refused;
    mutex_drop(s->mutex);
    if (stop)
      break;
    wh_js_aff_pump(s, &affordances);
    ft_acquisition_events observed = {0};
    result = ft_acquisition_snapshot(consumer, &observed);
    if (result != FT_STATUS_OK || observed.closed)
      break;
    ft_acquired_frame *frame = 0;
    ft_acquisition_range range = {0};
    ft_status status = ft_acquisition_acquire(consumer, FT_ACQUIRE_LATEST,
                                              cursor, &frame, &range);
    if (status == FT_STATUS_OK) {
      ft_acquired_frame_descriptor d = {0};
      void *surface = 0, *readiness = 0;
      result = ft_acquired_frame_describe(frame, &d);
      if (result == FT_STATUS_OK)
        result = ft_acquired_frame_macos_resources(frame, &surface, &readiness);
      if (result == FT_STATUS_OK &&
          (!d.width || !d.height || d.width > 16384 || d.height > 16384))
        result = FT_STATUS_ERROR;
      if (result == FT_STATUS_OK &&
          (!readiness_event || readiness_id != d.fence_id)) {
        [readiness_event release];
        readiness_event = [(id<MTLDevice>)s->gpu.device
            newSharedEventWithHandle:(MTLSharedEventHandle *)readiness];
        readiness_id = d.fence_id;
        if (!readiness_event)
          result = FT_STATUS_ERROR;
      }
      // Do not submit an unbounded GPU wait on a producer that can die before
      // signalling. Admit only completed copies; the UI still encodes the
      // descriptor's GPU wait before sampling. Bounded/cancellable on this
      // worker.
      if (result == FT_STATUS_OK) {
        ft_status ready = wh_js_macos_wait_ready(
            s, readiness_event, d.fence_value, request, 1000000);
        if (ready == FT_STATUS_TIMEOUT) {
          // This lease was never submitted to the GPU. Skip just this frame,
          // advancing the cursor so a slow copy cannot force reconnection or
          // repeatedly block input/affordances on the same publication.
          cursor = d.cursor;
          ft_acquired_frame_release(&frame);
          continue;
        }
        result = ready;
      }
      if (result == FT_STATUS_OK) {
        cursor = d.cursor;
        wh_js_post_import(s, &frame, &d, surface, readiness, incarnation);
      }
      ft_acquired_frame_release(&frame);
      continue;
    }
    if (status == FT_STATUS_RECONFIGURATION) {
      result = ft_acquisition_relinquish_configuration(consumer);
      if (result == FT_STATUS_OK) {
        status =
            ft_acquisition_macos_install_configuration(connection, consumer);
        if (status != FT_STATUS_OK && status != FT_STATUS_EMPTY &&
            status != FT_STATUS_STALE)
          result = status;
      }
    } else if (status == FT_STATUS_CLOSED) {
      result = status;
      break;
    } else if (status != FT_STATUS_EMPTY && status != FT_STATUS_MISS &&
               status != FT_STATUS_GAP && status != FT_STATUS_HOLDING_LIMIT) {
      result = status;
      break;
    }
    if (result == FT_STATUS_OK) {
      ft_acquisition_events after = {0};
      status = ft_acquisition_wait(consumer, &observed, FT_WAIT_ALL, cancel,
                                   16000000, &after);
      if (status != FT_STATUS_OK && status != FT_STATUS_TIMEOUT)
        result = status;
    }
  }
  [readiness_event release];
  mutex_take(s->mutex);
  ft_acquired_frame *untaken =
      s->mailbox.incarnation == incarnation ? s->mailbox.native : 0;
  if (untaken)
    memset(&s->mailbox, 0, sizeof(s->mailbox));
  for (WH_JS_Timeline *t = s->timelines; t; t = t->next)
    if (t->incarnation == incarnation)
      t->retired = true;
  wh_js_prune_timelines(s);
  wh_js_aff_forget(s);
  mutex_drop(s->mutex);
  ft_acquired_frame_release(&untaken);
  if (affordances) {
    ft_affordances_host_close(affordances);
    ft_affordances_host_destroy(&affordances);
  }
  ft_acquisition_consumer_destroy(&consumer);
  ft_acquisition_macos_connection_destroy(&connection);
  ft_acquisition_cancellation_destroy(&cancel);
  return result;
}
