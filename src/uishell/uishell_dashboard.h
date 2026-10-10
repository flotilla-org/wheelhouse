#ifndef UISHELL_DASHBOARD_H
#define UISHELL_DASHBOARD_H

//- The Dashboard this process opened (uishell_dashboard.c).
typedef struct UIShell_Dashboard UIShell_Dashboard;
struct UIShell_Dashboard
{
  Arena *arena;
  // The Dashboard directory, and its Dashboard ID (from `<dir>/id`, or new).
  String8 dir;
  String8 id;
  // Where this device keeps what only it remembers, and the Dashboard's
  // Presentation State there: its windows, with their workspaces.
  String8 device_dir;
  String8 presentation_path;
  // Whether its ID and "last opened" are written yet.
  B32 identity_saved;
  // The windows a user file saved before Dashboards, written back to it
  // unread so an older build still finds them.
  Arena *user_file_windows_arena;
  String8 user_file_windows;
};

global UIShell_Dashboard uishell_dashboard;

internal void uishell_dashboard_open(String8 dashboard, String8 folder);
internal void uishell_dashboard_open_from_cmd_line(CmdLine *cmd_line);
internal B32 uishell_dashboard_make_directories(String8 path);
internal void uishell_dashboard_load_windows(CFG_Node *user, String8 user_path);
internal String8 uishell_dashboard_records_dir(Arena *arena, CFG_Node *window, String8 records_id);
internal void uishell_dashboard_save_identity(void);

#endif // UISHELL_DASHBOARD_H
