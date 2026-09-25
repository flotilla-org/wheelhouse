// Separate-process acceptance against Jackstay's independent reference source.
// Built like Wheelhouse itself, on RAD's base layer.
#define BUILD_CONSOLE_INTERFACE 1
#include "base/base_inc.h"
#include "jackstay/wheelhouse_jackstay.h"
#include "base/base_inc.c"
#include "jackstay/wheelhouse_jackstay.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Checks run in every build and fail without a debugger or crash dialog.
#define assert(x)                                                              \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x);   \
      fflush(stderr);                                                          \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static WH_JS_State wait_state(WH_Jackstay *session, int mode) {
  WH_JS_State state = {0};
  for (int i = 0; i < 2000; ++i) {
    wh_js_snapshot(session, &state, 0, 0);
    if ((mode == 0 && state.connected && state.frame_version &&
         state.control) ||
        (mode == 1 && !state.resetting) || (mode == 2 && state.stopped))
      return state;
    sleep_ms(5);
  }
  fprintf(stderr, "timeout: %s / %s mode=%d\n", state.media_status,
          state.input_status, mode);
  exit(1);
}
// The harness acknowledges the independent source's state report. Queueing an
// event locally does not prove it reached the source before reset or shutdown.
static void wait_source(const char *marker) {
  puts(marker);
  fflush(stdout);
  assert(getchar() == '\n');
}
#if OS_WINDOWS
// D3D11 frames from Jackstay's d3d11_source (resizing, so pool generations
// change). This process plays the renderer's part as Wheelhouse's D3D11 layer
// does: a device with a shared release fence. "d3d11-import" hands the session
// that device, on the producer's (default) adapter; "d3d11-mismatch" names
// another adapter, and "d3d11-readback" none, so both must read frames back.
// Every mode then survives the source being killed and restarted.
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")
#define HARNESS_RELEASE(object)                                                \
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
  ID3D11Fence *release;
  uint64_t release_value, luid;
} Renderer;
static void renderer_open(Renderer *r) {
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  assert(SUCCEEDED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level,
                                     1, D3D11_SDK_VERSION, &r->device, 0,
                                     &r->context)) ||
         SUCCEEDED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_WARP, 0,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level,
                                     1, D3D11_SDK_VERSION, &r->device, 0,
                                     &r->context)));
  IDXGIDevice *dxgi = 0;
  IDXGIAdapter *adapter = 0;
  DXGI_ADAPTER_DESC desc = {0};
  assert(SUCCEEDED(r->device->lpVtbl->QueryInterface(r->device, &IID_IDXGIDevice,
                                                     (void **)&dxgi)));
  assert(SUCCEEDED(dxgi->lpVtbl->GetAdapter(dxgi, &adapter)));
  assert(SUCCEEDED(adapter->lpVtbl->GetDesc(adapter, &desc)));
  r->luid = ((uint64_t)(uint32_t)desc.AdapterLuid.HighPart << 32) |
            desc.AdapterLuid.LowPart;
  HARNESS_RELEASE(adapter);
  HARNESS_RELEASE(dxgi);
  assert(SUCCEEDED(r->device->lpVtbl->QueryInterface(
      r->device, &IID_ID3D11Device1, (void **)&r->device1)));
  assert(SUCCEEDED(r->device->lpVtbl->QueryInterface(
      r->device, &IID_ID3D11Device5, (void **)&r->device5)));
  assert(SUCCEEDED(r->context->lpVtbl->QueryInterface(
      r->context, &IID_ID3D11DeviceContext4, (void **)&r->context4)));
  assert(SUCCEEDED(r->device5->lpVtbl->CreateFence(
      r->device5, 0, D3D11_FENCE_FLAG_SHARED, &IID_ID3D11Fence,
      (void **)&r->release)));
}
// Import, GPU-wait, read back one pixel row, signal and release: the steps
// the view takes, with a readback standing in for the draw.
static bool renderer_use(Renderer *r, WH_Jackstay *session, WH_JS_Frame *frame) {
  ID3D11Texture2D *texture = 0, *staging = 0;
  ID3D11Fence *ready = 0;
  assert(SUCCEEDED(r->device1->lpVtbl->OpenSharedResource1(
      r->device1, frame->texture, &IID_ID3D11Texture2D, (void **)&texture)));
  assert(SUCCEEDED(r->device5->lpVtbl->OpenSharedFence(
      r->device5, frame->readiness, &IID_ID3D11Fence, (void **)&ready)));
  if (!wh_js_readiness_alive(ready)) {
    HARNESS_RELEASE(texture);
    HARNESS_RELEASE(ready);
    wh_js_frame_release(session, frame, 0);
    return false;
  }
  D3D11_TEXTURE2D_DESC desc = {0};
  desc.Width = frame->width;
  desc.Height = 1;
  desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  assert(SUCCEEDED(r->device->lpVtbl->CreateTexture2D(r->device, &desc, 0,
                                                      &staging)));
  D3D11_BOX box = {0, frame->height / 2, 0, frame->width, frame->height / 2 + 1, 1};
  assert(SUCCEEDED(r->context4->lpVtbl->Wait(r->context4, ready,
                                             frame->fence_value)));
  r->context->lpVtbl->CopySubresourceRegion(r->context, (ID3D11Resource *)staging,
                                            0, 0, 0, 0, (ID3D11Resource *)texture,
                                            0, &box);
  assert(SUCCEEDED(r->context4->lpVtbl->Signal(r->context4, r->release,
                                               ++r->release_value)));
  r->context->lpVtbl->Flush(r->context);
  wh_js_frame_release(session, frame, r->release_value);
  D3D11_MAPPED_SUBRESOURCE mapped = {0};
  assert(SUCCEEDED(r->context->lpVtbl->Map(r->context, (ID3D11Resource *)staging,
                                           0, D3D11_MAP_READ, 0, &mapped)));
  uint32_t lit = 0;
  for (uint32_t x = 0; x < frame->width; ++x)
    lit |= ((const uint32_t *)mapped.pData)[x] & 0x00ffffff;
  r->context->lpVtbl->Unmap(r->context, (ID3D11Resource *)staging, 0);
  HARNESS_RELEASE(staging);
  HARNESS_RELEASE(texture);
  HARNESS_RELEASE(ready);
  return lit != 0;
}
// Frames until at least `count` arrived across two sizes.
static void d3d11_frames(Renderer *r, WH_Jackstay *session, WH_JS_Path path,
                         int count) {
  WH_JS_State state = {0};
  uint64_t version = 0;
  uint32_t first_width = 0;
  bool resized = false;
  int seen = 0;
  for (int i = 0; i < 6000 && (seen < count || !resized); ++i) {
    WH_JS_Frame frame = {0};
    wh_js_snapshot(session, &state, &frame, version);
    if (frame.native) {
      assert(path == WH_JS_PATH_IMPORT && state.path == WH_JS_PATH_IMPORT);
      assert(frame.version > version);
      version = frame.version;
      if (!renderer_use(r, session, &frame))
        continue;
    } else if (frame.pixels) {
      assert(path == WH_JS_PATH_READBACK &&
             state.path == WH_JS_PATH_READBACK);
      version = frame.version;
      uint8_t lit = 0;
      for (size_t x = 0; x < (size_t)frame.width * 4; ++x)
        lit |= frame.pixels[(size_t)(frame.height / 2) * frame.width * 4 + x];
      assert(lit);
      free(frame.pixels);
    } else {
      sleep_ms(5);
      continue;
    }
    if (!first_width)
      first_width = frame.width;
    resized = resized || frame.width != first_width;
    seen++;
  }
  if (seen < count || !resized) {
    fprintf(stderr, "frames=%d resized=%d: %s / %s\n", seen, resized,
            state.media_status, state.path_status);
    exit(1);
  }
  printf("%d frames, resized, %s\n", seen, state.path_status);
  fflush(stdout);
}
static int d3d11_main(const char *address, const char *mode) {
  Renderer renderer = {0};
  renderer_open(&renderer);
  WH_JS_Gpu gpu = {renderer.luid, renderer.device, renderer.release};
  WH_JS_Path path = WH_JS_PATH_IMPORT;
  if (!strcmp(mode, "d3d11-mismatch")) {
    gpu.adapter ^= 1;
    path = WH_JS_PATH_READBACK;
  } else if (!strcmp(mode, "d3d11-readback")) {
    memset(&gpu, 0, sizeof(gpu));
    path = WH_JS_PATH_READBACK;
  }
  WH_Jackstay *session = wh_js_create(
      (WH_JS_Endpoint){address, "", false, WH_JS_MEDIA_D3D11}, gpu, 0, 0);
  assert(session);
  wh_js_connect(session, false);
  d3d11_frames(&renderer, session, path, 12);
  WH_JS_State state;
  wh_js_snapshot(session, &state, 0, 0);
  assert(state.producer_adapter == renderer.luid);
  // Kill the producer: setup closes (or its readiness fence is abandoned),
  // and the session reconnects to a new one and re-imports.
  puts("restart-source");
  fflush(stdout);
  int disconnected = 0;
  for (int i = 0; i < 3000 && !disconnected; ++i) {
    WH_JS_Frame frame = {0};
    wh_js_snapshot(session, &state, &frame, ~0ull);
    if (frame.native)
      wh_js_frame_release(session, &frame, 0); // never sampled
    free(frame.pixels);
    disconnected = !state.connected;
    sleep_ms(5);
  }
  assert(disconnected);
  d3d11_frames(&renderer, session, path, 6);
  wh_js_stop(session);
  wait_state(session, 2);
  assert(wh_js_destroy(&session));
  // Deferred releases complete on our own GPU work.
  HANDLE event = CreateEventW(0, FALSE, FALSE, 0);
  assert(SUCCEEDED(renderer.release->lpVtbl->SetEventOnCompletion(
      renderer.release, renderer.release_value, event)));
  assert(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0);
  CloseHandle(event);
  printf("D3D11 %s passed on adapter %016llx\n", mode,
         (unsigned long long)renderer.luid);
  return 0;
}
#endif
static int session_main(int argc, char **argv) {
  assert(argc >= 2);
  const char *mode = argc > 2 ? argv[2] : "input";
#if OS_WINDOWS
  if (!strncmp(mode, "d3d11", 5))
    return d3d11_main(argv[1], mode);
#endif
  WH_Jackstay *session =
      wh_js_create((WH_JS_Endpoint){argv[1], "", true}, (WH_JS_Gpu){0}, 0, 0);
  assert(session);
  WH_JS_State state;
  wh_js_snapshot(session, &state, 0, 0);
  assert(!state.connected && !state.control);
  if (!strcmp(mode, "missing")) {
    wh_js_connect(session, true);
    sleep_ms(200);
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
      sleep_ms(5);
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
      sleep_ms(5);
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
internal void entry_point(CmdLine *cmdline) {
  exit(session_main((int)cmdline->argc, cmdline->argv));
}
