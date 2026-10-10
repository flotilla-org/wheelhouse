//- The Dashboard (CONTEXT.md; ADR 0012). One process opens one: the one
// `--dashboard:<name|path>` names, else the one opened last on this device,
// else a new one named "default". A name is a directory under the platform's
// folder for things a person may share between devices; a path is used as
// it is:
//
//   <shared>/dashboards/<name>/        the Dashboard directory
//     id                               its Dashboard ID (UUIDv7), written on first save
//     dashboard.kdl, workspace-<id>.kdl  Andamento's records (uishell_sidebar_records.c)
//     windows/<andamento_records id>/  records of the process's other windows
//   <device>/
//     last_dashboard                   the directory opened last
//     presentation/<Dashboard ID>.wheelhouse   its windows, with their workspaces
//
//   <shared>:  Linux $XDG_CONFIG_HOME/wheelhouse (~/.config/wheelhouse);
//              macOS ~/Library/Application Support/Wheelhouse;
//              Windows %APPDATA%\Wheelhouse
//   <device>:  Linux $XDG_STATE_HOME/wheelhouse (~/.local/state/wheelhouse);
//              macOS the same as <shared>; Windows %LOCALAPPDATA%\Wheelhouse
//
// An explicit --user keeps both beside that file instead, as it does logs, so
// isolated runs never touch the person's own. The user file keeps this
// device's settings (fonts, keybindings, theme); windows are the Dashboard's
// Presentation State and live in its presentation file, keyed by Dashboard
// ID. Workspace nodes there still hold their panel trees, named by Workspace
// ID, until they move into Andamento's workspace records (roadmap, step 7b).
//
// Each window still has its own Andamento core (one core per process is
// deferred), and only one core can own the Dashboard's records. The
// presentation's first window owns them; any other window keeps its own
// records under `windows/` in the Dashboard directory, as each window kept
// its own directory before. If the first window closes while another stays,
// the next launch's first window takes the Dashboard's records over, and
// what it kept on its own is left behind.
//
// Opening Dashboards is a fresh start (roadmap, "State model migration"):
// windows a user file saved before them are not imported into any
// Dashboard. They stay in the user file, unread, so an older build still
// opens them with its records beside that file.

// An environment variable's value, empty when unset.
internal String8
uishell_dashboard_env(Arena *arena, char *name)
{
  String8 result = str8_zero();
#if OS_WINDOWS
  Temp scratch = scratch_begin(&arena, 1);
  String16 name16 = str16_from_8(scratch.arena, str8_cstring(name));
  DWORD size = GetEnvironmentVariableW((WCHAR *)name16.str, 0, 0);
  if(size != 0)
  {
    U16 *buffer = push_array(scratch.arena, U16, size);
    DWORD length = GetEnvironmentVariableW((WCHAR *)name16.str, (WCHAR *)buffer, size);
    if(length < size) { result = str8_from_16(arena, str16(buffer, length)); }
  }
  scratch_end(scratch);
#else
  char *value = getenv(name);
  if(value != 0) { result = push_str8_copy(arena, str8_cstring(value)); }
#endif
  return result;
}

// The platform's folder for what a person may share between devices, or
// (`device`) for what only this device remembers.
internal String8
uishell_dashboard_platform_folder(Arena *arena, B32 device)
{
  String8 home = get_process_info()->user_program_data_path;
#if OS_WINDOWS
  // user_program_data_path is the roaming %APPDATA%.
  String8 local = device ? uishell_dashboard_env(arena, "LOCALAPPDATA") : str8_zero();
  return push_str8f(arena, "%S/Wheelhouse", local.size ? local : home);
#elif OS_MAC
  (void)device;
  return push_str8f(arena, "%S/Library/Application Support/Wheelhouse", home);
#else
  // XDG ignores a relative path.
  String8 base = uishell_dashboard_env(arena, device ? "XDG_STATE_HOME" : "XDG_CONFIG_HOME");
  if(path_style_from_str8(base) != PathStyle_UnixAbsolute)
  { base = push_str8f(arena, device ? "%S/.local/state" : "%S/.config", home); }
  return push_str8f(arena, "%S/wheelhouse", base);
#endif
}

// Makes `path` and any folders above it; whether it exists after.
internal B32
uishell_dashboard_make_directories(String8 path)
{
  for(U64 i = 1; i < path.size; i++)
  {
    if(path.str[i] == '/' || path.str[i] == '\\') { make_directory(str8_prefix(path, i)); }
  }
  make_directory(path);
  return folder_path_exists(path);
}

// A Dashboard ID as read from its file: letters, digits and '-' only, as a
// file name can hold.
internal B32
uishell_dashboard_id_is_valid(String8 id)
{
  B32 result = id.size != 0 && id.size <= 64;
  for(U64 i = 0; i < id.size && result; i++)
  { result = char_is_alpha(id.str[i]) || char_is_digit(id.str[i], 10) || id.str[i] == '-'; }
  return result;
}

// Opens `dashboard` (a name or a path; empty for the last opened, or a new
// one), with everything beside an explicit user file's `folder` if given.
// Nothing is written until the Dashboard is first saved.
internal void
uishell_dashboard_open(String8 dashboard, String8 folder)
{
  UIShell_Dashboard *d = &uishell_dashboard;
  if(!d->arena) { d->arena = arena_alloc(); }
  arena_clear(d->arena);
  Temp scratch = scratch_begin(0, 0);
  String8 shared = folder.size ? folder : uishell_dashboard_platform_folder(scratch.arena, 0);
  String8 device = folder.size ? folder : uishell_dashboard_platform_folder(scratch.arena, 1);
  if(!dashboard.size)
  { dashboard = str8_skip_chop_whitespace(data_from_file_path(scratch.arena, push_str8f(scratch.arena, "%S/last_dashboard", device))); }
  if(!dashboard.size) { dashboard = str8_lit("default"); }
  B32 is_path = (str8_find_needle(dashboard, 0, str8_lit("/"), 0) < dashboard.size ||
                 str8_find_needle(dashboard, 0, str8_lit("\\"), 0) < dashboard.size ||
                 str8_match(dashboard, str8_lit("."), 0) || str8_match(dashboard, str8_lit(".."), 0));
  String8 dir = is_path ? path_absolute_dst_from_relative_dst_src(scratch.arena, dashboard, get_process_info()->initial_path) :
    push_str8f(scratch.arena, "%S/dashboards/%S", shared, dashboard);
  while(dir.size > 1 && (dir.str[dir.size-1] == '/' || dir.str[dir.size-1] == '\\')) { dir.size -= 1; }
  String8 id = str8_skip_chop_whitespace(data_from_file_path(scratch.arena, push_str8f(scratch.arena, "%S/id", dir)));
  if(!uishell_dashboard_id_is_valid(id)) { id = uishell_string_from_workspace_id(scratch.arena, uishell_workspace_id_make()); }
  d->dir = push_str8_copy(d->arena, dir);
  d->id = push_str8_copy(d->arena, id);
  d->device_dir = push_str8_copy(d->arena, device);
  d->presentation_path = push_str8f(d->arena, "%S/presentation/%S.wheelhouse", device, id);
  d->identity_saved = 0;
  scratch_end(scratch);
}

internal void
uishell_dashboard_open_from_cmd_line(CmdLine *cmd_line)
{
  Temp scratch = scratch_begin(0, 0);
  String8 user = cmd_line_string(cmd_line, str8_lit("user"));
  String8 folder = user.size ? str8_chop_last_slash(path_absolute_dst_from_relative_dst_src(scratch.arena, user, get_process_info()->initial_path)) :
    str8_zero();
  uishell_dashboard_open(cmd_line_string(cmd_line, str8_lit("dashboard")), folder);
  scratch_end(scratch);
}

// Writes the Dashboard's ID into it, and notes it as opened last, once.
internal void
uishell_dashboard_save_identity(void)
{
  UIShell_Dashboard *d = &uishell_dashboard;
  if(d->identity_saved || !d->dir.size) { return; }
  Temp scratch = scratch_begin(0, 0);
  String8 id_path = push_str8f(scratch.arena, "%S/id", d->dir);
  B32 saved = (uishell_dashboard_make_directories(d->dir) &&
               uishell_dashboard_make_directories(str8_chop_last_slash(d->presentation_path)));
  if(saved && !str8_match(str8_skip_chop_whitespace(data_from_file_path(scratch.arena, id_path)), d->id, 0))
  { saved = write_data_to_file_path(id_path, push_str8f(scratch.arena, "%S\n", d->id)); }
  saved = saved && write_data_to_file_path(push_str8f(scratch.arena, "%S/last_dashboard", d->device_dir),
                                           push_str8f(scratch.arena, "%S\n", d->dir));
  d->identity_saved = saved;
  scratch_end(scratch);
}

// After a user file is read into `user`: its windows are set aside, and the
// Dashboard's are read in their place. Without a Dashboard, it keeps its own.
internal void
uishell_dashboard_load_windows(CFG_Node *user, String8 user_path)
{
  UIShell_Dashboard *d = &uishell_dashboard;
  if(!d->user_file_windows_arena) { d->user_file_windows_arena = arena_alloc(); }
  arena_clear(d->user_file_windows_arena);
  d->user_file_windows = str8_zero();
  if(!d->presentation_path.size) { return; }
  Temp scratch = scratch_begin(0, 0);
  String8List set_aside = {0};
  for(CFG_Node *c = user->first, *next = &cfg_nil_node; c != &cfg_nil_node; c = next)
  {
    next = c->next;
    if(!str8_match(c->string, str8_lit("window"), 0)) { continue; }
    str8_list_push(scratch.arena, &set_aside, cfg_string_from_tree(scratch.arena, rd_state->cfg_schema_table, str8_chop_last_slash(user_path), c));
    cfg_node_release(rd_state->cfg, c);
  }
  d->user_file_windows = str8_list_join(d->user_file_windows_arena, &set_aside, 0);
  String8 data = data_from_file_path(scratch.arena, d->presentation_path);
  CFG_NodePtrList nodes = cfg_node_ptr_list_from_string(scratch.arena, rd_state->cfg, rd_state->cfg_schema_table,
                                                         str8_chop_last_slash(d->presentation_path), data);
  for(CFG_NodePtrNode *n = nodes.first; n != 0; n = n->next)
  {
    cfg_node_insert_child(rd_state->cfg, user, user->last, n->v);
    if(!str8_match(n->v->string, str8_lit("window"), 0)) { cfg_node_release(rd_state->cfg, n->v); }
  }
  scratch_end(scratch);
}

// A window's Andamento records directory: the Dashboard's own for the
// presentation's first window, one under `windows/` for any other.
internal String8
uishell_dashboard_records_dir(Arena *arena, CFG_Node *window, String8 records_id)
{
  UIShell_Dashboard *d = &uishell_dashboard;
  if(!d->dir.size) { return str8_zero(); }
  Temp scratch = scratch_begin(&arena, 1);
  CFG_NodePtrList windows = cfg_node_top_level_list_from_string(scratch.arena, str8_lit("window"));
  B32 first = windows.first != 0 && windows.first->v == window;
  scratch_end(scratch);
  return first ? push_str8_copy(arena, d->dir) : push_str8f(arena, "%S/windows/%S", d->dir, records_id);
}
