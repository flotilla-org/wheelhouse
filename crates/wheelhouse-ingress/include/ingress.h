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
// apply takes one metadata patch: 1 applied, 0 rejected, 2 not taken yet. A
// request's patches (POST /v1/metadata/patch with an array) are applied in one
// poll, so no frame shows part of them. retract drops every fact the
// endpoint's provider published, before a snapshot (POST
// /v1/metadata/snapshot) replaces them in the same poll. It returns 1; 2 when
// some window kept its facts (the patches still apply, and the request is
// unavailable); or 0 for an endpoint without a provider of its own; then, as
// with a NULL retract, the snapshot is refused (404).
extern void wheelhouse_ingress_poll(WheelhouseIngress *, uint32_t (*)(void *, const uint8_t *, size_t),
  uint32_t (*)(void *), void *);
typedef struct { const uint8_t *data; size_t len; } WheelhouseIngressText;
// view is the View's Automation ID (workspace/<uuid>/view/<slot key>), empty
// for none; view_id is a session-local CFG_ID, deprecated for it.
typedef struct {
  uint64_t workspace_id, view_id;
  WheelhouseIngressText entity_kind, entity_id, cwd, live_cwd, view;
} WheelhouseIngressWorkdir;
typedef void (*WheelhouseIngressWorkdirEmit)(void *, const WheelhouseIngressWorkdir *);
// observe runs on the UI thread and synchronously emits borrowed records.
// Return 1 for a complete snapshot, 0 when temporarily unavailable. Empty text
// is encoded as null; records with neither directory are omitted. poll without
// an observer answers reads with 503, as do invalid UTF-8 records. Adjacent
// reads in a drain share one snapshot; intervening patches invalidate it.
// No host pointers cross to the worker.
extern void wheelhouse_ingress_poll_observed(WheelhouseIngress *,
  uint32_t (*)(void *, const uint8_t *, size_t), uint32_t (*)(void *),
  uint32_t (*)(void *, WheelhouseIngressWorkdirEmit, void *), void *);
// The Automation Endpoint's reads (GET /v1/observed/semantic and
// /v1/observed/self; docs/protocol/automation-endpoint.md), answered at a
// frame's end by wheelhouse_ingress_automation with the host's automation
// setting (mode: off serves none of them, 404). While a client waits or has
// read in the last 10s, observe builds the Semantic Tree on the UI thread:
// it emits, synchronously, WORKSPACES with a JSON array of workspace nodes
// (wheelhouse_semantic_workspaces) and ANNOTATION with an Automation ID and
// a JSON object whose props (merged one level deep) and state flags join
// that node; then returns 1, or 0 when it can't now (503). flags has
// TERMINAL_TEXT when a read asks for screens and terminal_text allows it.
#define WHEELHOUSE_AUTOMATION_OFF 0u
#define WHEELHOUSE_AUTOMATION_OBSERVE 1u
#define WHEELHOUSE_AUTOMATION_FULL 2u
#define WHEELHOUSE_AUTOMATION_WORKSPACES 1u
#define WHEELHOUSE_AUTOMATION_ANNOTATION 2u
#define WHEELHOUSE_AUTOMATION_TERMINAL_TEXT 1u
typedef void (*WheelhouseIngressAutomationEmit)(void *out, uint32_t kind, WheelhouseIngressText a, WheelhouseIngressText b);
typedef struct {
  uint32_t mode, terminal_text;
  uint32_t (*observe)(void *context, uint32_t flags, WheelhouseIngressAutomationEmit emit, void *out);
  void *context;
} WheelhouseIngressAutomationHost;
extern void wheelhouse_ingress_automation(WheelhouseIngress *, const WheelhouseIngressAutomationHost *);
extern void wheelhouse_ingress_stop(WheelhouseIngress *);
// The Automation Endpoint's default address for this instance,
// wheelhouse/<instance UUID> under the runtime directory (address.rs):
// $XDG_RUNTIME_DIR/wheelhouse/<UUID>, else <temp>/wheelhouse-<uid>/<UUID>, its
// private directory made; \\.\pipe\wheelhouse\<UUID> on Windows. Writes it
// NUL-terminated and returns 1, or returns 0 with why in the buffer.
extern uint32_t wheelhouse_ingress_default_address(uint8_t *address, size_t capacity);
// Replaces the file at path with exactly address (empty: no endpoint), so a
// launcher reading it never sees part of it. 1, or 0 with a NUL-terminated
// error.
extern uint32_t wheelhouse_ingress_write_address_file(WheelhouseIngressText path, WheelhouseIngressText address,
  uint8_t *error, size_t capacity);
extern uint64_t wheelhouse_ingress_now_ms(void);

// A connector: a command supervised on a thread of its own (connector.rs).
// start copies argv (program first) and env (changes to this process's
// environment: "NAME=value" sets, "NAME" removes), appends the command's
// output to log, and returns
// NULL with a NUL-terminated error on failure. When the command exits it is
// restarted after 1s, doubling to 30s (reset by a run of 30s or more); the
// log notes each exit. wake runs on the supervisor thread whenever the
// command starts or exits. running reports whether it runs now and, when
// starts is not NULL, how many times it has started. stop ends it and what
// it started (its process group, or job on Windows) and releases the handle;
// NULL is accepted. stop_begin asks it to stop without waiting, so several
// can stop at once before each is stopped. Should Wheelhouse end without
// stopping it, it ends too (connector.rs).
typedef struct WheelhouseConnector WheelhouseConnector;
// With WHEELHOUSE_CONNECTOR_READY_LINE in flags, the command's first stdout
// line says it is ready and the rest goes to the log; a run that exits before
// it is ready has failed and waits for restart. state reports where it is
// (WheelhouseConnectorState) and, for ready or failed, copies the ready line
// or why it failed into text (NUL-terminated, cut to fit), storing the full
// length in len.
#define WHEELHOUSE_CONNECTOR_READY_LINE 1u
typedef enum WheelhouseConnectorState
{
  WheelhouseConnectorState_Waiting = 0,
  WheelhouseConnectorState_Running = 1,
  WheelhouseConnectorState_Ready = 2,
  WheelhouseConnectorState_Failed = 3,
} WheelhouseConnectorState;
extern WheelhouseConnector *wheelhouse_connector_start(const WheelhouseIngressText *argv, size_t argc,
  const WheelhouseIngressText *env, size_t env_count, WheelhouseIngressText log, uint32_t flags,
  void (*)(void), uint8_t *, size_t);
extern uint32_t wheelhouse_connector_running(const WheelhouseConnector *, uint64_t *starts);
extern uint32_t wheelhouse_connector_state(const WheelhouseConnector *, uint8_t *text, size_t capacity, size_t *len);
// Ends the current run and starts the next at once, without waiting.
extern void wheelhouse_connector_restart(const WheelhouseConnector *);
extern void wheelhouse_connector_stop_begin(WheelhouseConnector *);
extern void wheelhouse_connector_stop(WheelhouseConnector *);

// SIGTERM, SIGINT and SIGHUP ask to quit (quit.rs): the first runs wake on a
// background thread and makes quit_signalled true; a second ends the process.
// install returns 0 on failure; on Windows it does nothing.
extern uint32_t wheelhouse_quit_signals_install(void (*)(void));
extern uint32_t wheelhouse_quit_signalled(void);
#endif
