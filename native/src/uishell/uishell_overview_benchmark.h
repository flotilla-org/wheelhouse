// Isolated overview scenarios: synthetic replay or live terminal producers.
#ifndef UISHELL_OVERVIEW_BENCHMARK_H
#define UISHELL_OVERVIEW_BENCHMARK_H

typedef struct UIShell_OverviewBenchmark UIShell_OverviewBenchmark;
struct UIShell_OverviewBenchmark
{
  B32 enabled, initialized, screenshots;
  U32 count, frame, phase_frames;
  B32 interrupt, busy, interactive, live, live_transitions;
  U32 live_phase, live_checkpoint_mask;
  B32 live_image_seen[48];
  String8 command;
  U64 start_us;
  U32 source_frame;
  U32 pending_since[48];
  RD_FrameMetrics shell_metrics;
  UIShell_TerminalMetrics terminal_metrics;
  CFG_ID owners[48], views[48];
  String8 directory;
  FILE *metrics;
};
global UIShell_OverviewBenchmark uishell_overview_benchmark;
internal void uishell_overview_benchmark_init(CmdLine *cmd);
internal void uishell_overview_benchmark_begin(void);
internal void uishell_overview_benchmark_window(RD_WindowState *ws);
internal void uishell_overview_benchmark_end(void);
internal void uishell_overview_benchmark_feed(CFG_Node *view, UIShell_TerminalCellCache *cache, UIShell_TerminalImageCache *images, U16 cols, U16 rows);
#endif
