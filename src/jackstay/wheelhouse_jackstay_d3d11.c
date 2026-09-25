// Jackstay D3D11 frames (Windows, C ABI 0.10+), included by
// wheelhouse_jackstay.c. Follows Jackstay's reference consumer
// (tools/capture-viewer-d3d11): describe the producer's adapter, attach with a
// device on it, import pool textures and the readiness fence, GPU-wait before
// sampling and release through the consumer's own fence.
//
// Two paths share the setup and acquisition loop:
// - Import: the renderer's device is on the producer's adapter and has a
//   shared release fence. Frames move to the GUI thread, which imports and
//   samples them on that device (see WH_JS_Frame) and releases them to its
//   fence value signalled after the draw.
// - Read back: any other case (another adapter, no shared fences, or an
//   import the renderer refused). This worker creates its own device on the
//   producer's adapter, GPU-waits and copies each frame into a staging
//   texture, maps it and posts the pixels to the CPU path, then releases the
//   frame at once: the map proves the copy finished. No cross-adapter GPU copy.
// initguid: define the interface IDs here when no renderer did first.
#include <initguid.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")

#define WH_JS_D3D11_TEXTURES 16

#define WH_JS_RELEASE(object)                                                  \
  do {                                                                         \
    if (object) {                                                              \
      (object)->lpVtbl->Release(object);                                       \
      (object) = 0;                                                            \
    }                                                                          \
  } while (0)

typedef struct {
  ID3D11Device *device;
  ID3D11Device1 *device1;
  ID3D11Device5 *device5;
  ID3D11DeviceContext *context;
  ID3D11DeviceContext4 *context4;
  struct {
    uint64_t pool_id;
    uint32_t slot_id;
    ID3D11Texture2D *texture;
  } textures[WH_JS_D3D11_TEXTURES];
  uint64_t pool_id, readiness_id;
  ID3D11Fence *readiness;
  ID3D11Texture2D *staging;
  D3D11_TEXTURE2D_DESC staging_desc;
} WH_JS_Reader;

bool wh_js_readiness_alive(void *readiness_fence) {
  return readiness_fence && ft_d3d11_fence_alive(readiness_fence) != FT_STATUS_CLOSED;
}
static void wh_js_d3d11_cancel(void *connection) {
  if (connection)
    ft_acquisition_d3d11_connection_cancel(
        (ft_d3d11_acquisition_connection *)connection);
}
static void wh_js_luid_text(char *out, size_t size, uint64_t luid) {
  snprintf(out, size, "%08x:%08x", (unsigned)(luid >> 32), (unsigned)luid);
}

static void wh_js_reader_close(WH_JS_Reader *r) {
  for (int i = 0; i < WH_JS_D3D11_TEXTURES; ++i)
    WH_JS_RELEASE(r->textures[i].texture);
  WH_JS_RELEASE(r->readiness);
  WH_JS_RELEASE(r->staging);
  WH_JS_RELEASE(r->context4);
  WH_JS_RELEASE(r->context);
  WH_JS_RELEASE(r->device5);
  WH_JS_RELEASE(r->device1);
  WH_JS_RELEASE(r->device);
}
// A private device on exactly the producer's adapter.
static bool wh_js_reader_open(WH_JS_Reader *r, uint64_t luid) {
  memset(r, 0, sizeof(*r));
  IDXGIFactory1 *factory = 0;
  IDXGIAdapter1 *chosen = 0;
  if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory)))
    return false;
  for (UINT index = 0; !chosen; ++index) {
    IDXGIAdapter1 *adapter = 0;
    if (FAILED(factory->lpVtbl->EnumAdapters1(factory, index, &adapter)))
      break;
    DXGI_ADAPTER_DESC1 desc;
    if (SUCCEEDED(adapter->lpVtbl->GetDesc1(adapter, &desc)) &&
        (((uint64_t)(uint32_t)desc.AdapterLuid.HighPart << 32) |
         desc.AdapterLuid.LowPart) == luid)
      chosen = adapter;
    else
      adapter->lpVtbl->Release(adapter);
  }
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  bool ok =
      chosen &&
      SUCCEEDED(D3D11CreateDevice((IDXGIAdapter *)chosen, D3D_DRIVER_TYPE_UNKNOWN,
                                  0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                  2, D3D11_SDK_VERSION, &r->device, 0,
                                  &r->context)) &&
      SUCCEEDED(r->device->lpVtbl->QueryInterface(r->device, &IID_ID3D11Device1,
                                                  (void **)&r->device1)) &&
      SUCCEEDED(r->device->lpVtbl->QueryInterface(r->device, &IID_ID3D11Device5,
                                                  (void **)&r->device5)) &&
      SUCCEEDED(r->context->lpVtbl->QueryInterface(
          r->context, &IID_ID3D11DeviceContext4, (void **)&r->context4));
  WH_JS_RELEASE(chosen);
  WH_JS_RELEASE(factory);
  if (!ok)
    wh_js_reader_close(r);
  return ok;
}
// Read one held frame back into the CPU path. CLOSED: the producer is lost
// (abandoned readiness fence); the frame's pixels must not be shown.
static ft_status wh_js_reader_read(WH_Jackstay *s, WH_JS_Reader *r,
                                   const ft_acquired_frame_descriptor *d,
                                   void *texture_handle,
                                   void *readiness_handle) {
  // Frames arrive in cursor order and none is held across iterations, so the
  // first frame of a new pool ends every use of older pools' imports.
  if (d->pool_id != r->pool_id) {
    for (int i = 0; i < WH_JS_D3D11_TEXTURES; ++i)
      WH_JS_RELEASE(r->textures[i].texture);
    r->pool_id = d->pool_id;
  }
  ID3D11Texture2D *texture = 0;
  int free_entry = -1;
  for (int i = 0; i < WH_JS_D3D11_TEXTURES && !texture; ++i) {
    if (r->textures[i].texture && r->textures[i].slot_id == d->slot_id)
      texture = r->textures[i].texture;
    else if (!r->textures[i].texture && free_entry < 0)
      free_entry = i;
  }
  if (!texture) {
    if (free_entry < 0 ||
        FAILED(r->device1->lpVtbl->OpenSharedResource1(
            r->device1, (HANDLE)texture_handle, &IID_ID3D11Texture2D,
            (void **)&r->textures[free_entry].texture)))
      return FT_STATUS_ERROR;
    r->textures[free_entry].pool_id = d->pool_id;
    r->textures[free_entry].slot_id = d->slot_id;
    texture = r->textures[free_entry].texture;
  }
  if (!r->readiness || r->readiness_id != d->fence_id) {
    WH_JS_RELEASE(r->readiness);
    if (FAILED(r->device5->lpVtbl->OpenSharedFence(
            r->device5, (HANDLE)readiness_handle, &IID_ID3D11Fence,
            (void **)&r->readiness)))
      return FT_STATUS_ERROR;
    r->readiness_id = d->fence_id;
  }
  if (!wh_js_readiness_alive(r->readiness))
    return FT_STATUS_CLOSED;
  D3D11_TEXTURE2D_DESC source;
  texture->lpVtbl->GetDesc(texture, &source);
  if (d->width > source.Width || d->height > source.Height)
    return FT_STATUS_ERROR;
  if (!r->staging || r->staging_desc.Width != d->width ||
      r->staging_desc.Height != d->height ||
      r->staging_desc.Format != source.Format) {
    WH_JS_RELEASE(r->staging);
    D3D11_TEXTURE2D_DESC desc = {0};
    desc.Width = d->width;
    desc.Height = d->height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = source.Format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(r->device->lpVtbl->CreateTexture2D(r->device, &desc, 0,
                                                  &r->staging)))
      return FT_STATUS_ERROR;
    r->staging_desc = desc;
  }
  D3D11_BOX box = {0, 0, 0, d->width, d->height, 1};
  if (FAILED(r->context4->lpVtbl->Wait(r->context4, r->readiness,
                                       d->fence_value)))
    return FT_STATUS_ERROR;
  r->context->lpVtbl->CopySubresourceRegion(
      r->context, (ID3D11Resource *)r->staging, 0, 0, 0, 0,
      (ID3D11Resource *)texture, 0, &box);
  r->context->lpVtbl->Flush(r->context);
  // Poll rather than block, so a stalled producer fence cannot pin this
  // worker past a stop request.
  D3D11_MAPPED_SUBRESOURCE mapped = {0};
  HRESULT mapped_result = DXGI_ERROR_WAS_STILL_DRAWING;
  uint64_t deadline = now_time_us() + 2000000ull;
  for (;;) {
    mapped_result = r->context->lpVtbl->Map(
        r->context, (ID3D11Resource *)r->staging, 0, D3D11_MAP_READ,
        D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (mapped_result != DXGI_ERROR_WAS_STILL_DRAWING)
      break;
    mutex_take(s->mutex);
    bool stop = s->stop;
    mutex_drop(s->mutex);
    if (stop || now_time_us() > deadline)
      return FT_STATUS_TIMEOUT;
    sleep_ms(1);
  }
  if (FAILED(mapped_result))
    return FT_STATUS_ERROR;
  // A producer that died after the check leaves its fence satisfied although
  // the copy it covered may never have run.
  ft_status result = FT_STATUS_CLOSED;
  if (wh_js_readiness_alive(r->readiness))
    result = wh_js_post_pixels(s, d->width, d->height, mapped.pData,
                               mapped.RowPitch,
                               source.Format == DXGI_FORMAT_R8G8B8A8_UNORM
                                   ? FT_PIXEL_FORMAT_RGBA8_UNORM
                                   : FT_PIXEL_FORMAT_BGRA8_UNORM)
                 ? FT_STATUS_OK
                 : FT_STATUS_ERROR;
  r->context->lpVtbl->Unmap(r->context, (ID3D11Resource *)r->staging, 0);
  return result;
}

// Hand a held frame to the GUI, replacing one it has not taken. That one was
// never sampled, so it is released at once.
static void wh_js_post_import(WH_Jackstay *s, ft_acquired_frame **frame,
                              const ft_acquired_frame_descriptor *d,
                              void *texture, void *readiness,
                              uint64_t incarnation) {
  mutex_take(s->mutex);
  ft_acquired_frame *superseded = s->mailbox.native;
  WH_JS_Frame *m = &s->mailbox;
  memset(m, 0, sizeof(*m));
  m->native = *frame;
  *frame = 0;
  m->texture = texture;
  m->readiness = readiness;
  m->incarnation = incarnation;
  m->pool_id = d->pool_id;
  m->slot_id = d->slot_id;
  m->fence_id = d->fence_id;
  m->fence_value = d->fence_value;
  m->pixel_format = d->pixel_format;
  m->width = d->width;
  m->height = d->height;
  free(s->pixels);
  s->pixels = 0;
  s->state.width = d->width;
  s->state.height = d->height;
  m->version = ++s->state.frame_version;
  mutex_drop(s->mutex);
  ft_acquired_frame_release(&superseded);
  wh_js_wake(s);
}

// One D3D11 incarnation on an open link (consumed): describe, attach on the
// chosen path, then acquire until the producer goes or the request changes.
// A device-loss epoch closes setup; the caller reconnects and re-imports.
static ft_status wh_js_d3d11_run(WH_Jackstay *s, WH_JS_Link *link,
                                 unsigned request) {
  ft_d3d11_acquisition_connection *connection = 0;
  ft_acquisition_consumer *consumer = 0;
  ft_acquisition_release_timeline *timeline = 0;
  ft_acquisition_cancellation *cancellation = 0;
  WH_JS_Reader reader = {0};
  ft_status result =
      ft_acquisition_d3d11_connection_create_local(&link->local, &connection);
  wh_js_link_close(link);
  mutex_take(s->mutex);
  s->d3d11 = connection;
  bool stop = s->stop, refused = s->import_refused;
  WH_JS_Gpu gpu = s->gpu;
  uint64_t incarnation = ++s->incarnation;
  mutex_drop(s->mutex);
  if (stop)
    wh_js_d3d11_cancel(connection);
  ft_d3d11_adapter adapter = {0};
  if (result == FT_STATUS_OK)
    result = ft_acquisition_d3d11_describe(connection, &adapter);
  char producer[32], renderer[32];
  wh_js_luid_text(producer, sizeof(producer), adapter.luid);
  wh_js_luid_text(renderer, sizeof(renderer), gpu.adapter);
  const char *why = 0;
  if (!gpu.device || !gpu.release_fence)
    why = "the renderer has no shared fences";
  else if (gpu.adapter != adapter.luid)
    why = "the renderer is on another adapter";
  else if (refused)
    why = "the renderer refused the import";
  bool import = result == FT_STATUS_OK && !why;
  if (import) {
    // Holding: one frame on screen, one waiting in the mailbox, one whose
    // deferred release is still on the GPU.
    result = ft_acquisition_d3d11_attach(connection, gpu.device, 3, &consumer);
    if (result == FT_STATUS_ADAPTER_MISMATCH) {
      // The connection stays usable after a refusal.
      import = false;
      why = "the producer refused the renderer's adapter";
      result = FT_STATUS_OK;
    } else if (result == FT_STATUS_OK) {
      result = ft_acquisition_d3d11_register_release(
          connection, consumer, gpu.release_fence, &timeline);
      if (result != FT_STATUS_OK) {
        mutex_take(s->mutex);
        s->import_refused = true;
        mutex_drop(s->mutex);
      }
    }
  }
  if (result == FT_STATUS_OK && !import) {
    if (!wh_js_reader_open(&reader, adapter.luid))
      result = FT_STATUS_UNSUPPORTED;
    else
      result = ft_acquisition_d3d11_attach(connection, reader.device, 1,
                                           &consumer);
  }
  if (result == FT_STATUS_OK)
    result = ft_acquisition_cancellation_create(&cancellation);
  mutex_take(s->mutex);
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
  s->state.producer_adapter = adapter.luid;
  snprintf(s->state.media_status, 128,
           result == FT_STATUS_OK ? "Live" : "Disconnected (setup %d)", result);
  if (result == FT_STATUS_OK) {
    s->state.path = import ? WH_JS_PATH_IMPORT : WH_JS_PATH_READBACK;
    if (import)
      snprintf(s->state.path_status, sizeof(s->state.path_status),
               "D3D11 import on %s %s", producer, adapter.description);
    else
      snprintf(s->state.path_status, sizeof(s->state.path_status),
               "D3D11 read back on %s %s: %s (renderer %s)", producer,
               adapter.description, why, renderer);
  } else if (adapter.luid) {
    snprintf(s->state.path_status, sizeof(s->state.path_status),
             "D3D11 on %s %s: setup failed (%d)", producer,
             adapter.description, result);
  }
  mutex_drop(s->mutex);
  wh_js_wake(s);

  uint64_t cursor = 0, requested_epoch = 0;
  bool requested_configuration = false;
  while (result == FT_STATUS_OK) {
    mutex_take(s->mutex);
    stop = s->stop || s->request != request || (import && s->import_refused);
    mutex_drop(s->mutex);
    if (stop)
      break;
    if (ft_acquisition_d3d11_connection_alive(connection) != FT_STATUS_OK) {
      result = FT_STATUS_CLOSED;
      break;
    }
    // Snapshot before acquisition so a publication, release or
    // reconfiguration between the check and the wait is not lost.
    ft_acquisition_events before = {0};
    result = ft_acquisition_snapshot(consumer, &before);
    if (result != FT_STATUS_OK || before.closed)
      break;
    ft_acquired_frame *frame = 0;
    ft_acquisition_range range = {0};
    ft_status status = ft_acquisition_acquire(consumer, FT_ACQUIRE_LATEST,
                                              cursor, &frame, &range);
    uint32_t interest = FT_WAIT_DATA;
    if (status == FT_STATUS_OK) {
      ft_acquired_frame_descriptor d = {0};
      void *texture = 0, *readiness = 0;
      result = ft_acquired_frame_describe(frame, &d);
      if (result == FT_STATUS_OK)
        result = ft_acquired_frame_d3d11_resources(frame, &texture, &readiness);
      if (result == FT_STATUS_OK && (!d.width || !d.height ||
                                     d.width > 16384 || d.height > 16384))
        result = FT_STATUS_ERROR;
      if (result != FT_STATUS_OK) {
        ft_acquired_frame_release(&frame);
        break;
      }
      cursor = d.cursor;
      if (import) {
        wh_js_post_import(s, &frame, &d, texture, readiness, incarnation);
      } else {
        result = wh_js_reader_read(s, &reader, &d, texture, readiness);
        // Mapped or discarded: no GPU work of ours still uses it.
        ft_acquired_frame_release(&frame);
        if (result == FT_STATUS_TIMEOUT) {
          mutex_take(s->mutex);
          stop = s->stop;
          mutex_drop(s->mutex);
          if (!stop)
            result = FT_STATUS_ERROR;
        }
        if (result != FT_STATUS_OK)
          break;
      }
      continue;
    }
    if (status == FT_STATUS_CLOSED) {
      result = status;
      break;
    }
    if (status == FT_STATUS_RECONFIGURATION) {
      // A new pool generation, e.g. after a resize. Held frames keep theirs.
      interest = FT_WAIT_ALL;
      if (!requested_configuration ||
          requested_epoch != before.reconfiguration_epoch) {
        requested_configuration = true;
        requested_epoch = before.reconfiguration_epoch;
        result = ft_acquisition_relinquish_configuration(consumer);
        if (result != FT_STATUS_OK)
          break;
        status = ft_acquisition_d3d11_install_configuration(connection, consumer);
        if (status == FT_STATUS_OK)
          continue;
        if (status != FT_STATUS_EMPTY && status != FT_STATUS_STALE) {
          result = status;
          break;
        }
      }
    } else if (status == FT_STATUS_HOLDING_LIMIT) {
      interest = FT_WAIT_CAPACITY;
    } else if (status != FT_STATUS_EMPTY && status != FT_STATUS_MISS &&
               status != FT_STATUS_GAP) {
      result = status;
      break;
    }
    ft_acquisition_events after = {0};
    status = ft_acquisition_wait(consumer, &before, interest, cancellation,
                                 16 * 1000 * 1000, &after);
    if (status == FT_STATUS_CLOSED || status == FT_STATUS_CANCELLED) {
      result = FT_STATUS_CLOSED;
      break;
    }
    if (status != FT_STATUS_OK && status != FT_STATUS_TIMEOUT) {
      result = status;
      break;
    }
  }
  mutex_take(s->mutex);
  s->d3d11 = 0;
  ft_acquired_frame *untaken =
      s->mailbox.incarnation == incarnation ? s->mailbox.native : 0;
  if (untaken)
    memset(&s->mailbox, 0, sizeof(s->mailbox));
  for (WH_JS_Timeline *t = s->timelines; t; t = t->next)
    if (t->incarnation == incarnation)
      t->retired = true;
  wh_js_prune_timelines(s);
  mutex_drop(s->mutex);
  ft_acquired_frame_release(&untaken);
  wh_js_reader_close(&reader);
  ft_acquisition_cancellation_destroy(&cancellation);
  ft_acquisition_consumer_destroy(&consumer);
  ft_acquisition_d3d11_connection_destroy(&connection);
  return result;
}
