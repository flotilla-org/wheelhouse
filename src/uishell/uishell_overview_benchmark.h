// Opt-in render-feed replay. No sessions, live producers or global input injection.
#ifndef UISHELL_OVERVIEW_BENCHMARK_H
#define UISHELL_OVERVIEW_BENCHMARK_H

typedef struct UIShell_OverviewBenchmark UIShell_OverviewBenchmark;
struct UIShell_OverviewBenchmark
{
  B32 enabled, initialized, screenshots;
  U32 count, frame, phase_frames;
  U64 begin_us, build_us, rebuilds, cells_built, terminal_visits, updates, surface_allocations;
  CFG_ID owners[48], views[48];
  String8 directory;
  FILE *metrics;
};
global UIShell_OverviewBenchmark uishell_overview_benchmark;
internal void uishell_overview_benchmark_init(CmdLine *cmd);
internal void uishell_overview_benchmark_begin(void);
internal void uishell_overview_benchmark_window(RD_WindowState *ws);
internal void uishell_overview_benchmark_end(void);
internal void uishell_overview_benchmark_feed(CFG_Node *view, UIShell_TerminalCellCache *cache, U16 cols, U16 rows);
#endif
