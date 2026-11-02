// Rich Text View checks (#561), headless: synthetic markdown, and a
// synthetic conversation as chat (#616), opened in the View and driven by the
// UI events and commands the window sends, asserting on boxes and the core's
// state, never on pixels. Then the timing tests: the generated 35 MB
// transcript to its first painted tail (RICH_TEXT_CONVERSATION_BENCH), and
// the 10k-line generated document scrolled through a frame at a time, its
// layout cost per frame printed as RICH_TEXT_BENCH lines.

#define RichCheck(condition, label) do { if(!(condition)) { fprintf(stderr, "FAIL rich text: %s (line %d)\n", label, __LINE__); d->failures++; } \
                                         else { fprintf(stderr, "PASS rich text: %s\n", label); } } while(0)

// Layout's share of a 60 Hz frame (16.7 ms) at the 95th percentile: a
// quarter in an optimized build. CI's builds are unoptimized, Rust's parley
// included, and about twelve times slower there: a frame.
#if BUILD_DEBUG
# define UISHELL_RT_DIAG_LAYOUT_BUDGET_US 16700
# define UISHELL_RT_DIAG_LAYOUT_MAX_US max_U64
#else
# define UISHELL_RT_DIAG_LAYOUT_BUDGET_US 4175
# define UISHELL_RT_DIAG_LAYOUT_MAX_US 16700
#endif

internal int
uishell_rt_diag_u64_compare(const void *a, const void *b)
{
  U64 x = *(const U64 *)a, y = *(const U64 *)b;
  return x < y ? -1 : x > y ? 1 : 0;
}

typedef struct UIShell_RtDiagClip UIShell_RtDiagClip;
struct UIShell_RtDiagClip
{
  Arena *arena;
  String8 text;
  String8 html;
  U64 writes;
};

internal void
uishell_rt_diag_clip_write(void *user, String8 text)
{
  UIShell_RtDiagClip *clip = (UIShell_RtDiagClip *)user;
  clip->text = push_str8_copy(clip->arena, text);
  clip->html = str8_zero();
  clip->writes += 1;
}

internal void
uishell_rt_diag_clip_write_rich(void *user, String8 text, String8 html)
{
  UIShell_RtDiagClip *clip = (UIShell_RtDiagClip *)user;
  clip->text = push_str8_copy(clip->arena, text);
  clip->html = push_str8_copy(clip->arena, html);
  clip->writes += 1;
}

internal String8
uishell_rt_diag_clip_read(void *user, Arena *arena)
{
  return push_str8_copy(arena, ((UIShell_RtDiagClip *)user)->text);
}

typedef struct UIShell_RtDiag UIShell_RtDiag;
struct UIShell_RtDiag
{
  RD_WindowState *ws;
  Arena *arena;
  RD_PanelAddress panel;
  CFG_Node *view;
  RD_ViewAddress address;
  Rng2F32 rect;
  Vec2F32 mouse;
  U64 now_us;
  U32 failures;
  UIShell_RtDiagClip clip;
};

// One frame of the View in `d->rect`, with `events`.
internal UIShell_RichTextState *
uishell_rt_diag_frame(UIShell_RtDiag *d, UI_EventList *events)
{
  UI_EventList none = {0};
  UI_AnimationInfo animation = {0};
  ui_begin_build(d->ws->os, events ? events : &none, &d->ws->ui->icon_info, d->ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = d->mouse;
  UIShell_RegsScope(.window = d->ws->cfg_id, .panel = d->panel, .tab = d->address, .view = d->address)
  UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
  {
    wh_view_ui_direct(wh_renderer_from_name(str8_lit("rich_text")), d->address, d->rect);
  }
  ui_end_build();
  MemoryZeroStruct(&rd_state->cmds[0]);
  RD_ViewState *vs = rd_view_state_from_view(d->address, 0);
  return (UIShell_RichTextState *)vs->user_data;
}

internal UIShell_RichTextState *
uishell_rt_diag_frames(UIShell_RtDiag *d, U32 count)
{
  UIShell_RichTextState *s = 0;
  for(U32 i = 0; i < count; i += 1) { s = uishell_rt_diag_frame(d, 0); }
  return s;
}

internal UIShell_RichTextState *
uishell_rt_diag_event(UIShell_RtDiag *d, UI_Event event)
{
  d->now_us += 20000;
  event.timestamp_us = d->now_us;
  UI_EventNode node = {.v = event};
  UI_EventList events = {&node, &node, 1};
  return uishell_rt_diag_frame(d, &events);
}

// A command to the View, as the window's dispatch gives it: seen in the
// next frame.
internal UIShell_RichTextState *
uishell_rt_diag_cmd(UIShell_RtDiag *d, String8 name, String8 string)
{
  UIShell_Regs regs = {UISHELL_REGS_LIT_INIT_TOP};
  regs.window = d->ws->cfg_id;
  regs.panel = d->panel;
  regs.view = regs.tab = d->address;
  regs.string = string;
  uishell_cmd_list_push_new(rd_state->cmds_arenas[0], &rd_state->cmds[0], name, &regs);
  return uishell_rt_diag_frame(d, 0);
}

// An edit command through the window's path (uishell_dispatch_ui_event_command),
// as Ctrl-C or Cmd-A reach it.
internal void
uishell_rt_diag_edit(UIShell_RtDiag *d, String8 name)
{
  UIShell_RegsScope(.window = d->ws->cfg_id, .view = d->address, .tab = d->address, .panel = d->panel,
                    .edit_owner_key = ui_key_zero(), .edit_owner_captured = 0)
  {
    uishell_dispatch_ui_event_command(name);
  }
  // A command that became UI events, rather than a callback, arrives next frame.
  UI_EventList events = d->ws->ui_events;
  MemoryZeroStruct(&d->ws->ui_events);
  uishell_rt_diag_frame(d, &events);
}

internal UIShell_RichTextState *
uishell_rt_diag_open(UIShell_RtDiag *d, String8 path)
{
  CFG_Node *window = cfg_node_from_id(d->ws->cfg_id);
  CFG_Node *host = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
  d->panel = rd_fixture_leaf(rd_fixture_owner(host));
  d->view = rd_fixture_view(d->panel, str8_lit("rich_text"), rd_eval_string_from_file_path(d->arena, path), 1);
  d->address = rd_view_address_from_cfg(d->view);
  UIShell_RichTextState *s = 0;
  // The content cache reads the file on its own threads.
  for(U32 i = 0; i < 1000; i += 1)
  {
    s = uishell_rt_diag_frame(d, 0);
    if(s && s->doc && s->model.block_count) { break; }
    sleep_ms(5);
  }
  return uishell_rt_diag_frames(d, 4);
}

// The Block boxes this frame built, in order.
typedef struct UIShell_RtDiagBox UIShell_RtDiagBox;
struct UIShell_RtDiagBox
{
  UI_Box *box;
  UIShell_RtBlockDraw *draw;
};

internal U64
uishell_rt_diag_boxes(Arena *arena, UIShell_RtDiagBox **out)
{
  U64 count = 0;
  for(UI_Box *b = ui_state->root; !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, ui_state->root).next)
  {
    if(b->custom_draw == uishell_rt_block_draw) { count += 1; }
  }
  *out = push_array(arena, UIShell_RtDiagBox, Max(count, 1));
  U64 i = 0;
  for(UI_Box *b = ui_state->root; !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, ui_state->root).next)
  {
    if(b->custom_draw == uishell_rt_block_draw) { (*out)[i++] = (UIShell_RtDiagBox){b, (UIShell_RtBlockDraw *)b->custom_draw_user_data}; }
  }
  return count;
}

internal UIShell_RtDiagBox
uishell_rt_diag_box(Arena *arena, U64 key)
{
  UIShell_RtDiagBox *boxes = 0;
  U64 count = uishell_rt_diag_boxes(arena, &boxes);
  for(U64 i = 0; i < count; i += 1) { if(boxes[i].draw->key == key) { return boxes[i]; } }
  return (UIShell_RtDiagBox){&ui_nil_box, 0};
}

internal Rng2F32
uishell_rt_diag_content(UIShell_RtDiag *d)
{
  return ui_box_from_key(ui_key_from_stringf(rd_view_ui_key(d->address), "###rich_text_content"))->rect;
}

internal UIShell_RtNode *
uishell_rt_diag_block_with(UIShell_RichTextState *s, String8 prefix)
{
  for(U64 i = 0; i < s->model.block_count; i += 1)
  {
    UIShell_RtNode *n = uishell_rt_node(&s->model, s->model.block_keys[i]);
    if(n && str8_match(str8_prefix(n->text, prefix.size), prefix, 0)) { return n; }
  }
  return 0;
}

// A point in a Block's text, at `byte`, on screen.
internal Vec2F32
uishell_rt_diag_point(UIShell_RtDiag *d, UIShell_RichTextState *s, U64 key, U32 byte)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_RtDiagBox b = uishell_rt_diag_box(scratch.arena, key);
  UIShell_RtNode *n = uishell_rt_node(&s->model, key);
  Vec2F32 result = v2f32(-1, -1);
  if(!ui_box_is_nil(b.box) && n)
  {
    UIShell_RtFrame f = uishell_rt_frame(scratch.arena, &s->model, &s->theme, n);
    UIShell_RtLayoutEntry *e = 0;
    uishell_rt_block_height(s, n, s->width, &e);
    WhTextCaret caret = {0};
    if(e && wh_text_caret_rect(&e->layout->text, byte, WH_TEXT_DOWNSTREAM, &caret))
    {
      result = v2f32(b.box->rect.x0 + f.indent + f.inset_x + caret.x + 1, b.box->rect.y0 + f.pad_top + f.inset_y + caret.y + caret.height/2);
    }
  }
  scratch_end(scratch);
  return result;
}

internal UIShell_RichTextState *
uishell_rt_diag_click(UIShell_RtDiag *d, Vec2F32 at, WM_Modifiers mods, U32 clicks)
{
  UIShell_RichTextState *s = 0;
  d->mouse = at;
  for(U32 i = 0; i < clicks; i += 1)
  {
    // Presses closer together than the double-click time, at one place.
    s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = at, .modifiers = mods});
    s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = at, .modifiers = mods});
  }
  return uishell_rt_diag_frames(d, 2);
}

// Clicks a Block's text at `byte`, and returns the file the click opened,
// read from the commands the release's frame queued; empty for none.
internal String8
uishell_rt_diag_click_opens(UIShell_RtDiag *d, UIShell_RichTextState *s, U64 key, U32 byte, Arena *arena)
{
  d->now_us += 2000000;
  Vec2F32 p = uishell_rt_diag_point(d, s, key, byte);
  d->mouse = p;
  uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = p});
  UI_EventNode node = {.v = (UI_Event){.kind = UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = p, .timestamp_us = d->now_us += 20000}};
  UI_EventList events = {&node, &node, 1};
  UI_AnimationInfo animation = {0};
  ui_begin_build(d->ws->os, &events, &d->ws->ui->icon_info, d->ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = p;
  UIShell_RegsScope(.window = d->ws->cfg_id, .panel = d->panel, .tab = d->address, .view = d->address)
  UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
  {
    wh_view_ui_direct(wh_renderer_from_name(str8_lit("rich_text")), d->address, d->rect);
  }
  ui_end_build();
  String8 opened = str8_zero();
  for(UIShell_CmdNode *n = rd_state->cmds[0].first; n; n = n->next)
  {
    if(str8_match(n->cmd.name, str8_lit("open"), 0)) { opened = push_str8_copy(arena, n->cmd.regs->file_path); }
  }
  MemoryZeroStruct(&rd_state->cmds[0]);
  return opened;
}

// One frame with `event`, and the string of the `name` command it queued;
// empty for none.
internal String8
uishell_rt_diag_event_cmd(UIShell_RtDiag *d, UI_Event event, String8 name, Arena *arena)
{
  d->now_us += 20000;
  event.timestamp_us = d->now_us;
  UI_EventNode node = {.v = event};
  UI_EventList events = {&node, &node, 1};
  UI_AnimationInfo animation = {0};
  ui_begin_build(d->ws->os, &events, &d->ws->ui->icon_info, d->ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = d->mouse;
  UIShell_RegsScope(.window = d->ws->cfg_id, .panel = d->panel, .tab = d->address, .view = d->address)
  UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
  {
    wh_view_ui_direct(wh_renderer_from_name(str8_lit("rich_text")), d->address, d->rect);
  }
  ui_end_build();
  String8 result = str8_zero();
  for(UIShell_CmdNode *n = rd_state->cmds[0].first; n; n = n->next)
  {
    if(str8_match(n->cmd.name, name, 0)) { result = push_str8_copy(arena, n->cmd.regs->string); }
  }
  MemoryZeroStruct(&rd_state->cmds[0]);
  return result;
}

// A label this frame built whose text has `needle`.
internal B32
uishell_rt_diag_label_with(String8 needle)
{
  for(UI_Box *b = ui_state->root; !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, ui_state->root).next)
  {
    if((b->flags & UI_BoxFlag_DrawText) && str8_find_needle(b->string, 0, needle, 0) < b->string.size) { return 1; }
  }
  return 0;
}

internal String8
uishell_rt_diag_copy(UIShell_RtDiag *d)
{
  U64 writes = d->clip.writes;
  uishell_rt_diag_edit(d, str8_lit("copy"));
  return d->clip.writes > writes ? d->clip.text : str8_lit("(nothing copied)");
}

internal B32
uishell_rt_diag_contains(String8 haystack, String8 needle)
{
  return str8_find_needle(haystack, 0, needle, 0) < haystack.size;
}

//- Conversations (#616).

// A Container in the model by its kind, its Turn role or Section kind, and
// a prefix of its first Block's text or its name.
internal UIShell_RtNode *
uishell_rt_diag_container(UIShell_RichTextState *s, U32 kind, U32 role_or_section, String8 prefix)
{
  for(U64 i = 0; i < s->model.slots_count; i += 1)
  {
    for(UIShell_RtNode *n = s->model.slots[i]; n != 0; n = n->hash_next)
    {
      if(n->kind != kind || (kind == WHEELHOUSE_RICH_KIND_TURN ? n->role : n->section) != role_or_section) { continue; }
      UIShell_RtNode *first = n->child_count ? uishell_rt_node(&s->model, n->children[0]) : 0;
      if(str8_match(str8_prefix(n->name, prefix.size), prefix, 0) || (first && str8_match(str8_prefix(first->text, prefix.size), prefix, 0))) { return n; }
    }
  }
  return 0;
}

internal UI_Box *
uishell_rt_diag_drawn(UI_BoxCustomDrawFunctionType *draw, U64 key)
{
  for(UI_Box *b = ui_state->root; !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, ui_state->root).next)
  {
    if(b->custom_draw == draw && *(U64 *)b->custom_draw_user_data == key) { return b; }
  }
  return &ui_nil_box;
}

internal UI_Box *
uishell_rt_diag_named(UI_Box *root, String8 needle)
{
  for(UI_Box *b = root; !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, root).next)
  {
    if(str8_find_needle(b->string, 0, needle, 0) < b->string.size) { return b; }
  }
  return &ui_nil_box;
}

internal String8
uishell_rt_diag_status_mark(UI_Box *header)
{
  UI_Box *mark = uishell_rt_diag_named(header, str8_lit("###rt_status"));
  return ui_box_is_nil(mark) ? str8_zero() : str8_prefix(mark->string, str8_find_needle(mark->string, 0, str8_lit("###"), 0));
}

internal String8
uishell_rt_diag_conversation(Arena *arena)
{
  String8 lines[] =
  {
    str8_lit("{\"type\":\"Hello\",\"contract\":1}"),
    str8_lit("{\"type\":\"RunStarted\",\"threadId\":\"t1\",\"seq\":1,\"runId\":\"r1\"}"),
    str8_lit("{\"type\":\"TextMessageStart\",\"threadId\":\"t1\",\"seq\":2,\"messageId\":\"m1\",\"role\":\"user\"}"),
    str8_lit("{\"type\":\"TextMessageContent\",\"threadId\":\"t1\",\"seq\":3,\"messageId\":\"m1\",\"delta\":\"Please run the *harbour* tests.\"}"),
    str8_lit("{\"type\":\"TextMessageEnd\",\"threadId\":\"t1\",\"seq\":4,\"messageId\":\"m1\"}"),
    str8_lit("{\"type\":\"ReasoningMessageStart\",\"threadId\":\"t1\",\"seq\":5,\"messageId\":\"rs1\"}"),
    str8_lit("{\"type\":\"ReasoningMessageContent\",\"threadId\":\"t1\",\"seq\":6,\"messageId\":\"rs1\",\"delta\":\"Consider the moorings first.\"}"),
    str8_lit("{\"type\":\"ReasoningMessageEnd\",\"threadId\":\"t1\",\"seq\":7,\"messageId\":\"rs1\"}"),
    str8_lit("{\"type\":\"TextMessageStart\",\"threadId\":\"t1\",\"seq\":8,\"messageId\":\"m2\",\"role\":\"assistant\"}"),
    str8_lit("{\"type\":\"TextMessageContent\",\"threadId\":\"t1\",\"seq\":9,\"messageId\":\"m2\",\"delta\":\"Running them now: the tide tables, the berth allocations, the pilot rosters and the cargo manifests each have a suite, and the slowest is the capstan.\"}"),
    str8_lit("{\"type\":\"TextMessageEnd\",\"threadId\":\"t1\",\"seq\":10,\"messageId\":\"m2\"}"),
    str8_lit("{\"type\":\"ToolCallStart\",\"threadId\":\"t1\",\"seq\":11,\"toolCallId\":\"c1\",\"toolCallName\":\"Bash\",\"title\":\"cargo test harbour\",\"kind\":\"shell\"}"),
    str8_lit("{\"type\":\"ToolCallArgs\",\"threadId\":\"t1\",\"seq\":12,\"toolCallId\":\"c1\",\"delta\":\"{\\\"command\\\":\\\"cargo test harbour\\\"}\"}"),
    str8_lit("{\"type\":\"ToolCallResult\",\"threadId\":\"t1\",\"seq\":13,\"toolCallId\":\"c1\",\"messageId\":\"r1\",\"status\":\"failed\",\"mediaType\":\"text/x-ansi\",\"content\":\"\\u001b[31merror\\u001b[0m: berth 4\\nkestrel ok\\n\"}"),
    str8_lit("{\"type\":\"ToolCallStart\",\"threadId\":\"t1\",\"seq\":14,\"toolCallId\":\"c2\",\"toolCallName\":\"Read\",\"title\":\"tides.md\",\"kind\":\"read\"}"),
    str8_lit("{\"type\":\"ToolCallStart\",\"threadId\":\"t1\",\"seq\":15,\"toolCallId\":\"c3\",\"toolCallName\":\"Edit\",\"title\":\"berths.rs\",\"kind\":\"edit\"}"),
    str8_lit("{\"type\":\"ToolCallResult\",\"threadId\":\"t1\",\"seq\":16,\"toolCallId\":\"c3\",\"messageId\":\"r3\",\"status\":\"ok\",\"mediaType\":\"text/plain\",\"content\":\"edited\\n\"}"),
    str8_lit("{\"type\":\"ToolCallStart\",\"threadId\":\"t1\",\"seq\":17,\"toolCallId\":\"c4\",\"toolCallName\":\"Grep\",\"title\":\"pilot\",\"kind\":\"search\"}"),
    str8_lit("{\"type\":\"ToolCallResult\",\"threadId\":\"t1\",\"seq\":18,\"toolCallId\":\"c4\",\"messageId\":\"r4\",\"status\":\"interrupted\",\"content\":\"\"}"),
    str8_lit("{\"type\":\"ToolCallStart\",\"threadId\":\"t1\",\"seq\":19,\"toolCallId\":\"c5\",\"toolCallName\":\"Fetch\",\"title\":\"example.com\",\"kind\":\"web\"}"),
    str8_lit("{\"type\":\"ToolCallResult\",\"threadId\":\"t1\",\"seq\":20,\"toolCallId\":\"c5\",\"messageId\":\"r5\",\"status\":\"awaiting_approval\",\"content\":\"\"}"),
    str8_lit("{\"type\":\"ToolCallStart\",\"threadId\":\"t1\",\"seq\":21,\"toolCallId\":\"c9\",\"toolCallName\":\"Task\",\"title\":\"Find the berth\",\"kind\":\"agent\"}"),
    str8_lit("{\"type\":\"RunStarted\",\"threadId\":\"t2\",\"seq\":1,\"runId\":\"r2\",\"parent\":{\"threadId\":\"t1\",\"toolCallId\":\"c9\"}}"),
    str8_lit("{\"type\":\"TextMessageStart\",\"threadId\":\"t2\",\"seq\":2,\"messageId\":\"s1\",\"role\":\"user\"}"),
    str8_lit("{\"type\":\"TextMessageContent\",\"threadId\":\"t2\",\"seq\":3,\"messageId\":\"s1\",\"delta\":\"Find the free berth.\"}"),
    str8_lit("{\"type\":\"TextMessageEnd\",\"threadId\":\"t2\",\"seq\":4,\"messageId\":\"s1\"}"),
    str8_lit("{\"type\":\"RunFinished\",\"threadId\":\"t2\",\"seq\":5,\"runId\":\"r2\",\"outcome\":\"completed\"}"),
    str8_lit("{\"type\":\"ToolCallResult\",\"threadId\":\"t1\",\"seq\":22,\"toolCallId\":\"c9\",\"messageId\":\"r9\",\"status\":\"ok\",\"mediaType\":\"text/markdown\",\"content\":\"Berth 7.\"}"),
    str8_lit("{\"type\":\"Custom\",\"threadId\":\"t1\",\"seq\":23,\"name\":\"compaction\",\"value\":{}}"),
    str8_lit("{\"type\":\"TextMessageStart\",\"threadId\":\"t1\",\"seq\":24,\"messageId\":\"m3\",\"role\":\"assistant\"}"),
    str8_lit("{\"type\":\"TextMessageContent\",\"threadId\":\"t1\",\"seq\":25,\"messageId\":\"m3\",\"delta\":\"Done.\"}"),
    str8_lit("{\"type\":\"TextMessageEnd\",\"threadId\":\"t1\",\"seq\":26,\"messageId\":\"m3\"}"),
  };
  String8List out = {0};
  for EachElement(i, lines) { str8_list_pushf(arena, &out, "%S\n", lines[i]); }
  return str8_list_join(arena, &out, 0);
}

//- The fixture: synthetic, so the repo stays public-safe.

internal String8
uishell_rt_diag_paragraph(Arena *arena, U64 i)
{
  String8 words[] = {str8_lit("tide"), str8_lit("mooring"), str8_lit("berth"), str8_lit("hawser"), str8_lit("capstan"), str8_lit("ensign")};
  String8List parts = {0};
  str8_list_pushf(arena, &parts, "Paragraph %llu:", (unsigned long long)i);
  for(U64 w = 0; w < 18; w += 1) { str8_list_push(arena, &parts, words[(i*7 + w*3) % ArrayCount(words)]); }
  // Every fifth paragraph names the kestrel, for find.
  if(i % 5 == 0) { str8_list_push(arena, &parts, str8_lit("kestrel")); }
  str8_list_push(arena, &parts, str8_lit("ends here."));
  return str8_list_join(arena, &parts, &(StringJoin){.sep = str8_lit_comp(" ")});
}

internal String8
uishell_rt_diag_fixture(Arena *arena, U64 paragraphs, String8 extra_in_paragraph, U64 extra_at)
{
  String8List out = {0};
  str8_list_pushf(arena, &out, "---\ntitle: The harbour log\n---\n\n");
  str8_list_pushf(arena, &out, "# The harbour log\n\n");
  str8_list_pushf(arena, &out, "Opening words with a [relative link](notes.md), a [web link](https://example.com/tides) and [[Harbour Master]].\n\n");
  str8_list_pushf(arena, &out, "![A lighthouse](lighthouse.png)\n\n");
  str8_list_pushf(arena, &out, "```rust\nfn main() {\n    println!(\"fenced\");\n}\n```\n\n");
  str8_list_pushf(arena, &out, "| Vessel | Berth | Tonnage |\n|:-------|:-----:|--------:|\n| Kestrel | 4 | 1200 |\n| Plover | 7 | 950 |\n| Tern \xE2\x9A\x93 | 2 | 30 |\n\n");
  str8_list_pushf(arena, &out, "- a tight item\n- another item\n\n> A quote holds a paragraph.\n\n");
  for(U64 i = 0; i < paragraphs; i += 1)
  {
    String8 p = uishell_rt_diag_paragraph(arena, i);
    if(i == extra_at && extra_in_paragraph.size) { p = push_str8f(arena, "%S %S", p, extra_in_paragraph); }
    str8_list_pushf(arena, &out, "%S\n\n", p);
    if(i == 12) { str8_list_pushf(arena, &out, "## Mid-channel\n\n"); }
  }
  str8_list_pushf(arena, &out, "## Repeat\n\nThe first.\n\n## Repeat\n\nThe second.\n");
  return str8_list_join(arena, &out, 0);
}

internal B32
uishell_rich_text_diagnostics(RD_WindowState *ws)
{
  Arena *arena = arena_alloc();
  UIShell_RtDiag diag = {0}, *d = &diag;
  d->ws = ws;
  d->arena = arena;
  d->rect = r2f32p(0, 0, 900, 600);
  d->now_us = 1000000;
  d->clip.arena = arena;
  UI_State *saved_ui = ui_state, *saved_window_ui = ws->ui, *test_ui = ui_state_alloc();
  WM_ClipboardIO saved_clip = wm_clipboard_io;
  UIShell_CmdList saved_commands[2] = {rd_state->cmds[0], rd_state->cmds[1]};
  U64 saved_generation = rd_state->cmds_gen;
  rd_state->cmds[0] = rd_state->cmds[1] = (UIShell_CmdList){0};
  rd_state->cmds_gen = 0;
  ws->ui = test_ui;
  ui_select_state(test_ui);
  wm_clipboard_io = (WM_ClipboardIO){&d->clip, uishell_rt_diag_clip_write, uishell_rt_diag_clip_read, uishell_rt_diag_clip_write_rich};
  String8 dir = str8_chop_last_slash(uishell_launch_user_path);
  if(dir.size == 0) { dir = get_current_path(arena); }
  String8 path = push_str8f(arena, "%S/rich-text-fixture.md", dir);
  String8 note = push_str8f(arena, "%S/Harbour Master.md", dir);
  U64 paragraphs = 60;
  write_data_to_file_path(path, uishell_rt_diag_fixture(arena, paragraphs, str8_zero(), 0));
  write_data_to_file_path(note, str8_lit("# Harbour Master\n\nA note beside the log.\n"));
  write_data_to_file_path(push_str8f(arena, "%S/notes.md", dir), str8_lit("# Notes\n"));
  char *error = 0;

  //- Opening: text/markdown resolves through the Handlers to Rich Text.
  RichCheck(str8_match(wh_handler_from_media_type(WH_Content_File, wh_media_type_from_path(path))->renderer, str8_lit("rich_text"), 0),
            "a markdown file's media type resolves to the Rich Text Renderer");
  RichCheck(wh_handler_from_media_type(WH_Content_File, wh_media_type_from_path(str8_lit("/tmp/notes.txt")))->renderer.size == 0,
            "a plain text file resolves to no Handler");
  RichCheck(str8_match(wh_handler_from_media_type(WH_Content_Embed, str8_lit("application/vnd.flotilla.table"))->renderer, str8_lit("table"), 0),
            "a table Embed's media type resolves to the table Renderer");

  UIShell_RichTextState *s = uishell_rt_diag_open(d, path);
  RichCheck(s && s->doc && s->model.block_count > paragraphs, "the fixture opens");
  if(!(s && s->doc)) { goto done; }

  //- One box per visible Block, stacked by the core's offsets.
  {
    Temp scratch = scratch_begin(&arena, 1);
    UIShell_RtDiagBox *boxes = 0;
    U64 box_count = uishell_rt_diag_boxes(scratch.arena, &boxes);
    WheelhouseRichWindow w = {0};
    wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
    Rng2F32 content = uishell_rt_diag_content(d);
    U64 visible = 0, matched = 0, measured = 0;
    for(U64 i = 0; i < w.entry_count; i += 1)
    {
      WheelhouseRichWindowEntry *e = &w.entries[i];
      if(e->part != WHEELHOUSE_RICH_PART_BLOCK || e->top + e->height <= w.top || e->top >= w.top + w.viewport) { continue; }
      visible += 1;
      measured += e->measured;
      for(U64 b = 0; b < box_count; b += 1)
      {
        F32 y = boxes[b].box->rect.y0 - content.y0;
        if(boxes[b].draw->key == e->key && abs_f32(y - (F32)(e->top - w.top)) < 1.f && abs_f32(dim_2f32(boxes[b].box->rect).y - e->height) < 1.f) { matched += 1; }
      }
    }
    fprintf(stderr, "RICH_TEXT_BOXES boxes=%llu visible=%llu matched=%llu\n", (unsigned long long)box_count, (unsigned long long)visible, (unsigned long long)matched);
    RichCheck(visible > 3 && box_count == visible && matched == visible, "opening a fixture creates one box per visible Block, where the core places it");
    RichCheck(measured == visible, "every visible Block's height is measured");
    // The "document" style set has no rules: markdown is as it was, every
    // Block across the reading column and no Container with rows of its own.
    U64 full = 0, rows = 0, empty_rows = 0;
    for(U64 b = 0; b < box_count; b += 1)
    {
      Rng2F32 r = boxes[b].box->rect;
      full += abs_f32(r.x0 - (content.x0 + s->content_x)) < 0.5f && abs_f32(r.x1 - (content.x0 + s->content_x + s->width)) < 0.5f;
    }
    for(U64 i = 0; i < w.entry_count; i += 1)
    {
      if(w.entries[i].part == WHEELHOUSE_RICH_PART_BLOCK) { continue; }
      rows += 1;
      empty_rows += w.entries[i].measured && w.entries[i].height == 0;
    }
    RichCheck(s->styles == uishell_rt_style_set_from_name(str8_lit("document")) && s->styles->rule_count == 0 && full == box_count &&
              rows > 0 && empty_rows == rows, "the document style set leaves markdown as it was");
    UIShell_RtNode *front = 0;
    for(U64 i = 0; i < s->model.block_count && !front; i += 1)
    {
      UIShell_RtNode *n = uishell_rt_node(&s->model, s->model.block_keys[i]);
      if(n && n->hidden) { front = n; }
    }
    RichCheck(front && ui_box_is_nil(uishell_rt_diag_box(scratch.arena, front->key).box), "front matter is hidden");
    scratch_end(scratch);
  }

  //- Scrolling moves the anchor and keeps measured heights.
  WheelhouseRichAnchor anchor = {0};
  {
    Temp scratch = scratch_begin(&arena, 1);
    WheelhouseRichWindow w = {0};
    wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
    U64 before_count = w.entry_count;
    WheelhouseRichWindowEntry *before = push_array(scratch.arena, WheelhouseRichWindowEntry, Max(before_count, 1));
    MemoryCopy(before, w.entries, sizeof(*before)*before_count);
    F64 top0 = w.top;
    WheelhouseRichAnchor anchor0 = {0};
    B32 had_anchor = wheelhouse_rich_text_anchor(s->doc, s->sub, &anchor0, &error);
    Rng2F32 content = uishell_rt_diag_content(d);
    d->mouse = center_2f32(content);
    // Into the paragraphs, past the code and the table.
    for(U32 i = 0; i < 12; i += 1)
    {
      s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Scroll, .pos = d->mouse, .delta_2f32 = v2f32(0, 1)});
    }
    s = uishell_rt_diag_frames(d, 2);
    wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
    B32 has_anchor = wheelhouse_rich_text_anchor(s->doc, s->sub, &anchor, &error);
    U64 kept = 0, compared = 0;
    for(U64 i = 0; i < before_count; i += 1)
    {
      if(!before[i].measured) { continue; }
      for(U64 j = 0; j < w.entry_count; j += 1)
      {
        if(w.entries[j].key != before[i].key || w.entries[j].part != before[i].part) { continue; }
        compared += 1;
        kept += w.entries[j].measured && w.entries[j].height == before[i].height;
      }
    }
    fprintf(stderr, "RICH_TEXT_SCROLL top=%.1f->%.1f anchor=%llx:%u compared=%llu kept=%llu\n", top0, w.top,
            (unsigned long long)anchor.key, anchor.byte, (unsigned long long)compared, (unsigned long long)kept);
    RichCheck(has_anchor && w.top > top0 && (!had_anchor || anchor.key != anchor0.key || anchor.byte != anchor0.byte),
              "the wheel scrolls, moving the Scroll Anchor");
    RichCheck(compared > 0 && kept == compared, "scrolling keeps the measured heights");
    RichCheck(anchor.position != WHEELHOUSE_RICH_POSITION_TEXT || abs_f32((F32)(anchor.viewport_offset - anchor.line_offset)) >= 0,
              "the anchor names a line");
    scratch_end(scratch);
  }

  //- A reload keeps the anchor's Block where it was in the viewport.
  {
    Temp scratch = scratch_begin(&arena, 1);
    s = uishell_rt_diag_frames(d, 2);
    wheelhouse_rich_text_anchor(s->doc, s->sub, &anchor, &error);
    UIShell_RtNode *anchored = uishell_rt_node(&s->model, anchor.key);
    U64 anchor_key = anchor.key;
    String8 anchored_text = anchored ? push_str8_copy(scratch.arena, anchored->text) : str8_zero();
    Rng2F32 content = uishell_rt_diag_content(d);
    F32 y_before = uishell_rt_diag_box(scratch.arena, anchor_key).box->rect.y0 - content.y0;
    // Lengthen the paragraph just above the anchored one, so it wraps
    // again; its height changes above the anchor.
    S64 above = -1;
    for(U64 i = 0; i < paragraphs; i += 1)
    {
      UIShell_RtNode *n = uishell_rt_diag_block_with(s, push_str8f(scratch.arena, "Paragraph %llu:", (unsigned long long)i));
      if(n && anchored && n->order + 1 == anchored->order) { above = (S64)i; }
    }
    UIShell_RtNode *edited = above >= 0 ? uishell_rt_diag_block_with(s, push_str8f(scratch.arena, "Paragraph %lld:", above)) : 0;
    F32 edited_before = 0;
    WheelhouseRichWindow w = {0};
    wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
    for(U64 i = 0; edited && i < w.entry_count; i += 1) { if(w.entries[i].key == edited->key) { edited_before = w.entries[i].height; } }
    U64 generation = s->model.generation;
    String8 longer = str8_lit("and then the harbour master added a long sentence about the weather, the tides, the pilots and the cargo, which wraps.");
    write_data_to_file_path(path, uishell_rt_diag_fixture(scratch.arena, paragraphs, longer, (U64)Max(above, 0)));
    for(U32 i = 0; i < 1000 && s->model.generation == generation; i += 1) { s = uishell_rt_diag_frame(d, 0); sleep_ms(5); }
    s = uishell_rt_diag_frames(d, 3);
    WheelhouseRichAnchor after = {0};
    wheelhouse_rich_text_anchor(s->doc, s->sub, &after, &error);
    UIShell_RtNode *now = uishell_rt_node(&s->model, after.key);
    F32 y_after = uishell_rt_diag_box(scratch.arena, after.key).box->rect.y0 - content.y0;
    F32 edited_after = 0;
    wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
    UIShell_RtNode *edited_now = above >= 0 ? uishell_rt_diag_block_with(s, push_str8f(scratch.arena, "Paragraph %lld:", above)) : 0;
    for(U64 i = 0; edited_now && i < w.entry_count; i += 1) { if(w.entries[i].key == edited_now->key) { edited_after = w.entries[i].height; } }
    fprintf(stderr, "RICH_TEXT_RELOAD generation=%llu->%llu anchor_y=%.1f->%.1f above=%lld height=%.1f->%.1f\n",
            (unsigned long long)generation, (unsigned long long)s->model.generation, y_before, y_after, above, edited_before, edited_after);
    RichCheck(s->model.generation > generation, "saving the file reloads it in place");
    RichCheck(above >= 0 && edited_after > edited_before, "the edit above the anchor changed a Block's height");
    RichCheck(now && str8_match(now->text, anchored_text, 0) && abs_f32(y_after - y_before) < 1.f,
              "after a reload, the anchor's Block keeps its viewport offset");
    scratch_end(scratch);
  }

  //- Resizing keeps the reading position.
  {
    Temp scratch = scratch_begin(&arena, 1);
    s = uishell_rt_diag_frames(d, 2);
    // Put the viewport's top at a paragraph's later line, so the anchored
    // byte isn't its Block's first and moves when the Block rewraps.
    UIShell_RtNode *target = uishell_rt_diag_block_with(s, str8_lit("Paragraph 30:"));
    UIShell_RtLayoutEntry *e = 0;
    if(target) { uishell_rt_block_height(s, target, s->width, &e); }
    if(target && e && e->layout->text.line_count > 1)
    {
      wheelhouse_rich_text_reveal(s->doc, s->sub, target->key, 0, &error);
      s = uishell_rt_diag_frames(d, 3);
      WheelhouseRichWindow w = {0};
      wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
      for(U64 i = 0; i < w.entry_count; i += 1)
      {
        if(w.entries[i].key != target->key) { continue; }
        UIShell_RtFrame f = uishell_rt_frame(scratch.arena, &s->model, &s->theme, target);
        F64 line_top = w.entries[i].top + f.pad_top + f.inset_y + e->layout->text.lines[e->layout->text.line_count-1].y;
        wheelhouse_rich_text_scroll_by(s->doc, s->sub, line_top + 2 - w.top, &error);
        break;
      }
      s = uishell_rt_diag_frames(d, 3);
    }
    WheelhouseRichAnchor before = {0};
    wheelhouse_rich_text_anchor(s->doc, s->sub, &before, &error);
    RichCheck(target && before.key == target->key && before.byte > 0, "the anchor sits on a paragraph's later line");
    Vec2F32 line_before = uishell_rt_diag_point(d, s, before.key, before.byte);
    d->rect = r2f32p(0, 0, 560, 600);
    s = uishell_rt_diag_frames(d, 4);
    WheelhouseRichAnchor after = {0};
    wheelhouse_rich_text_anchor(s->doc, s->sub, &after, &error);
    Vec2F32 line_after = uishell_rt_diag_point(d, s, after.key, before.byte);
    fprintf(stderr, "RICH_TEXT_RESIZE anchor=%llx:%u->%llx:%u line_y=%.1f->%.1f\n", (unsigned long long)before.key, before.byte,
            (unsigned long long)after.key, after.byte, line_before.y, line_after.y);
    RichCheck(after.key == before.key && line_before.y > 0 && abs_f32(line_after.y - line_before.y) < 2.f,
              "resizing keeps the anchored line where it was");
    d->rect = r2f32p(0, 0, 900, 600);
    s = uishell_rt_diag_frames(d, 4);
    scratch_end(scratch);
  }

  //- Find reveals and highlights each match.
  {
    Temp scratch = scratch_begin(&arena, 1);
    U64 expected = 0;
    for(U64 i = 0; i < paragraphs; i += 1) { expected += i % 5 == 0; }
    // Kestrel is also a table cell, and the table's text is searched too.
    expected += 1;
    s = uishell_rt_diag_cmd(d, str8_lit("search"), str8_lit("kestrel"));
    for(U32 i = 0; i < 50 && s->find_active && (s->match_count < expected || s->match_current == WHEELHOUSE_NONE); i += 1) { s = uishell_rt_diag_frame(d, 0); }
    RichCheck(s->find_active && s->match_count == expected, "find counts every match");
    U64 revealed = 0, highlighted = 0, others = 0;
    U64 first = s->match_current;
    for(U64 step = 0; step < expected; step += 1)
    {
      if(step > 0) { s = uishell_rt_diag_cmd(d, str8_lit("find_next"), str8_zero()); }
      s = uishell_rt_diag_frames(d, 2);
      if(s->match_current >= s->match_count) { continue; }
      WheelhouseRichMatch *m = &s->matches[s->match_current];
      UIShell_RtDiagBox b = uishell_rt_diag_box(scratch.arena, m->key);
      Rng2F32 content = uishell_rt_diag_content(d);
      B32 shown = !ui_box_is_nil(b.box) && b.box->rect.y0 >= content.y0 - 1 && b.box->rect.y0 < content.y1;
      revealed += shown && s->match_current == (first + step) % expected;
      highlighted += shown && b.draw->current_find_quads > 0;
      UIShell_RtDiagBox *boxes = 0;
      U64 count = uishell_rt_diag_boxes(scratch.arena, &boxes);
      for(U64 i = 0; i < count; i += 1) { others += boxes[i].draw->find_quads; }
    }
    s = uishell_rt_diag_cmd(d, str8_lit("find_next"), str8_zero());
    fprintf(stderr, "RICH_TEXT_FIND matches=%llu revealed=%llu highlighted=%llu\n", (unsigned long long)s->match_count,
            (unsigned long long)revealed, (unsigned long long)highlighted);
    RichCheck(revealed == expected, "find_next reveals each match in turn");
    RichCheck(highlighted == expected, "each match is highlighted when it is the current one");
    RichCheck(s->match_current == first, "find wraps round to the first match");
    s = uishell_rt_diag_cmd(d, str8_lit("find_prev"), str8_zero());
    RichCheck(s->match_current == (first + expected - 1) % expected, "find_prev steps back");
    (void)others;
    scratch_end(scratch);
  }

  //- Copy: plain text and HTML through Ctrl-C's path, markdown by command.
  {
    Temp scratch = scratch_begin(&arena, 1);
    // To the top, where the opening paragraph and the table are.
    s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Navigate, .delta_unit = UI_EventDeltaUnit_Whole, .delta_2s32 = v2s32(0, -1)});
    s = uishell_rt_diag_frames(d, 3);
    UIShell_RtNode *opening = uishell_rt_diag_block_with(s, str8_lit("Opening words"));
    UIShell_RtNode *quote = uishell_rt_diag_block_with(s, str8_lit("A quote holds"));
    RichCheck(opening && quote, "the opening paragraph and the quote are in the model");
    if(opening && quote)
    {
      // Press at the opening's start, Shift-click past the table.
      s = uishell_rt_diag_click(d, uishell_rt_diag_point(d, s, opening->key, 0), 0, 1);
      s = uishell_rt_diag_click(d, uishell_rt_diag_point(d, s, quote->key, 7), WM_Modifier_Shift, 1);
      String8 plain = uishell_rt_diag_copy(d);
      String8 html = d->clip.html;
      String8 want = str8_lit("Opening words with a relative link, a web link and Harbour Master.");
      fprintf(stderr, "RICH_TEXT_COPY plain=\"%.*s\"\n", (int)Min(plain.size, 400), plain.str);
      fprintf(stderr, "RICH_TEXT_COPY html=\"%.*s\"\n", (int)Min(html.size, 400), html.str);
      RichCheck(str8_match(str8_prefix(plain, want.size), want, 0), "a selection across Blocks copies their plain text");
      RichCheck(uishell_rt_diag_contains(plain, str8_lit("Vessel\tBerth\tTonnage\nKestrel\t4\t1200")), "the table in it copies as TSV");
      RichCheck(uishell_rt_diag_contains(plain, str8_lit("fn main() {")) && str8_match(str8_postfix(plain, 7), str8_lit("A quote"), 0),
                "the copy runs from the press to the Shift-click");
      RichCheck(uishell_rt_diag_contains(html, str8_lit("<a href=\"https://example.com/tides\">web link</a>")) &&
                uishell_rt_diag_contains(html, str8_lit("<table")), "the copy's HTML has the link and the table");
#if OS_LINUX
      Temp read = scratch_begin(&arena, 1);
      RichCheck(str8_match(wm_get_selection_text(read.arena), plain, 0) && lnx_wm_state->selection_owned, "the selection also went to PRIMARY");
      scratch_end(read);
#endif
      s = uishell_rt_diag_cmd(d, str8_lit("copy_markdown"), str8_zero());
      String8 markdown = d->clip.text;
      RichCheck(uishell_rt_diag_contains(markdown, str8_lit("[web link](https://example.com/tides)")) &&
                uishell_rt_diag_contains(markdown, str8_lit("| Vessel |")), "Copy as Markdown gives markdown");
      // A double click selects a word, a triple click the Block.
      U32 at = (U32)str8_find_needle(opening->text, 0, str8_lit("words"), 0) + 2;
      s = uishell_rt_diag_click(d, uishell_rt_diag_point(d, s, opening->key, at), 0, 2);
      RichCheck(str8_match(uishell_rt_diag_copy(d), str8_lit("words"), 0), "a double click selects a word");
      d->now_us += 2000000;
      s = uishell_rt_diag_click(d, uishell_rt_diag_point(d, s, opening->key, at), 0, 3);
      RichCheck(str8_match(uishell_rt_diag_copy(d), want, 0), "a triple click selects the Block");
      // Select All, through its command, copies the whole document.
      d->now_us += 2000000;
      uishell_rt_diag_edit(d, str8_lit("select_all"));
      String8 all = uishell_rt_diag_copy(d);
      RichCheck(uishell_rt_diag_contains(all, str8_lit("The harbour log\n")) &&
                uishell_rt_diag_contains(all, uishell_rt_diag_paragraph(scratch.arena, paragraphs-1)), "Select All selects the whole document");
      // Shift with an arrow extends a selection.
      s = uishell_rt_diag_click(d, uishell_rt_diag_point(d, s, opening->key, 0), 0, 1);
      d->now_us += 2000000;
      for(U32 i = 0; i < 7; i += 1)
      {
        s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Navigate, .flags = UI_EventFlag_KeepMark|UI_EventFlag_ExplicitDirectional,
                                                .delta_unit = UI_EventDeltaUnit_Char, .delta_2s32 = v2s32(1, 0)});
      }
      RichCheck(str8_match(uishell_rt_diag_copy(d), str8_lit("Opening"), 0), "Shift with an arrow extends the selection");
      s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_Esc, .slot = UI_EventActionSlot_Cancel});
    }
    // A code Block's copy button copies its code.
    UIShell_RtNode *code = 0;
    for(U64 i = 0; i < s->model.block_count && !code; i += 1)
    {
      UIShell_RtNode *n = uishell_rt_node(&s->model, s->model.block_keys[i]);
      if(n && n->kind == WHEELHOUSE_RICH_KIND_CODE && !n->hidden) { code = n; }
    }
    UI_Box *button = &ui_nil_box;
    for(UI_Box *b = ui_state->root; !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, ui_state->root).next)
    {
      if(str8_find_needle(b->string, 0, str8_lit("###rt_copy"), 0) < b->string.size) { button = b; }
    }
    RichCheck(code && !ui_box_is_nil(button), "a code Block has a copy button");
    if(!ui_box_is_nil(button))
    {
      d->now_us += 2000000;
      U64 writes = d->clip.writes;
      s = uishell_rt_diag_click(d, center_2f32(button->rect), 0, 1);
      RichCheck(d->clip.writes == writes+1 && str8_match(d->clip.text, str8_lit("fn main() {\n    println!(\"fenced\");\n}"), 0),
                "the copy button copies the code");
    }
    scratch_end(scratch);
  }

  //- Links are actions.
  {
    Temp scratch = scratch_begin(&arena, 1);
    String8 resolved = str8_zero();
    U64 heading = 0;
    RichCheck(uishell_rt_resolve_link(scratch.arena, s, str8_lit("https://example.com/tides"), &resolved, &heading) == UIShell_RtLink_External,
              "a web link opens externally");
    uishell_rt_resolve_link(scratch.arena, s, str8_lit("notes.md"), &resolved, &heading);
    fprintf(stderr, "RICH_TEXT_LINK notes.md -> %.*s (beside %.*s)\n", str8_varg(resolved), str8_varg(dir));
    RichCheck(uishell_rt_resolve_link(scratch.arena, s, str8_lit("notes.md"), &resolved, &heading) == UIShell_RtLink_File &&
              path_match_normalized(resolved, push_str8f(scratch.arena, "%S/notes.md", dir)), "a relative .md link resolves beside the file");
    RichCheck(uishell_rt_resolve_link(scratch.arena, s, str8_lit("Harbour Master"), &resolved, &heading) == UIShell_RtLink_File &&
              path_match_normalized(resolved, note), "a wikilink resolves to the note beside the file");
    RichCheck(uishell_rt_resolve_link(scratch.arena, s, str8_lit("#mid-channel"), &resolved, &heading) == UIShell_RtLink_Heading && heading != 0,
              "a fragment link resolves to its heading");
    // Repeated headings take GitHub's suffixes, in document order.
    U64 first = 0, second = 0;
    uishell_rt_resolve_link(scratch.arena, s, str8_lit("#repeat"), &resolved, &first);
    uishell_rt_resolve_link(scratch.arena, s, str8_lit("#repeat-1"), &resolved, &second);
    UIShell_RtNode *first_node = uishell_rt_node(&s->model, first), *second_node = uishell_rt_node(&s->model, second);
    RichCheck(first_node && second_node && first_node->order < second_node->order, "a repeated heading's anchors follow document order");
    // Only the web and mail leave the app.
    String8 refused[] = {str8_lit("javascript:alert(1)"), str8_lit("vbscript:msgbox"), str8_lit("smb://host/share"), str8_lit("ms-settings:"),
                         str8_lit("data:text/html,x")};
    B32 none = 1;
    for EachElement(i, refused) { none &= uishell_rt_resolve_link(scratch.arena, s, refused[i], &resolved, &heading) == UIShell_RtLink_None; }
    RichCheck(none, "links with other schemes do nothing");
    RichCheck(uishell_rt_resolve_link(scratch.arena, s, str8_lit("mailto:harbour@example.com"), &resolved, &heading) == UIShell_RtLink_External,
              "a mail link opens externally");
    // A relative link is percent-decoded, its query and fragment dropped.
    String8 spaced = push_str8f(scratch.arena, "%S/tide tables.md", dir);
    write_data_to_file_path(spaced, str8_lit("# Tide tables\n"));
    RichCheck(uishell_rt_resolve_link(scratch.arena, s, str8_lit("tide%20tables.md?rev=2#noon"), &resolved, &heading) == UIShell_RtLink_File &&
              path_match_normalized(resolved, spaced), "a percent-encoded relative link resolves to its file");
    RichCheck(uishell_rt_resolve_link(scratch.arena, s, str8_lit("No Such Note"), &resolved, &heading) == UIShell_RtLink_File &&
              path_match_normalized(resolved, push_str8f(scratch.arena, "%S/No Such Note", dir)), "a missing wikilink names its path, without inventing a note");
    // Clicking the relative link opens it as a new View.
    UIShell_RtNode *opening = uishell_rt_diag_block_with(s, str8_lit("Opening words"));
    if(opening)
    {
      U32 at = (U32)str8_find_needle(opening->text, 0, str8_lit("relative"), 0) + 3;
      String8 opened = uishell_rt_diag_click_opens(d, s, opening->key, at, scratch.arena);
      RichCheck(path_match_normalized(opened, push_str8f(scratch.arena, "%S/notes.md", dir)), "a click on a relative .md link opens it as a new View");
    }
    // A link to a file that isn't there opens nothing, and says so.
    UIShell_RtNode *image = uishell_rt_diag_block_with(s, str8_lit("A lighthouse"));
    RichCheck(image != 0, "an image shows as its alt text");
    if(image)
    {
      String8 opened = uishell_rt_diag_click_opens(d, s, image->key, 3, scratch.arena);
      s = uishell_rt_diag_frames(d, 1);
      RichCheck(opened.size == 0 && uishell_rt_diag_label_with(str8_lit("lighthouse.png doesn't exist")),
                "a click on a link to a missing file opens nothing and says the file doesn't exist");
    }
    scratch_end(scratch);
  }

  //- The table Embed: focus on click, Esc returns it, the wheel only while focused.
  {
    Temp scratch = scratch_begin(&arena, 1);
    s = uishell_rt_diag_frames(d, 2);
    UIShell_RtNode *table = 0;
    for(U64 i = 0; i < s->model.block_count && !table; i += 1)
    {
      UIShell_RtNode *n = uishell_rt_node(&s->model, s->model.block_keys[i]);
      if(n && n->kind == WHEELHOUSE_RICH_KIND_EMBED) { table = n; }
    }
    UI_Box *grid = &ui_nil_box;
    UIShell_RtDiagBox tb = table ? uishell_rt_diag_box(scratch.arena, table->key) : (UIShell_RtDiagBox){&ui_nil_box, 0};
    for(UI_Box *b = tb.box->first; !ui_box_is_nil(tb.box) && !ui_box_is_nil(b); b = b->next) { if(b->custom_draw == uishell_table_draw) { grid = b; } }
    RichCheck(table && !ui_box_is_nil(grid), "the table is an Embedded View: a binnacle Grid in its Block");
    if(table && !ui_box_is_nil(grid))
    {
      UIShell_TableDraw *td = (UIShell_TableDraw *)grid->custom_draw_user_data;
      RichCheck(td->snap.axes[BINNACLE_ROWS].count == 3 && td->snap.axes[BINNACLE_COLS].count == 3, "the Grid holds the table's rows and columns");
      // Its cells are laid out as Rich Text is, when drawn: an anchor, which
      // the UI font lacks, comes from a fallback face, not as a missing glyph.
      U64 layouts = s->layouts.layouts;
      DR_BucketScope(dr_bucket_make()) { grid->custom_draw(grid, grid->custom_draw_user_data); }
      U64 drawn = s->layouts.layouts - layouts;
      BinnacleAxisSnapshot *cols = &td->snap.axes[BINNACLE_COLS];
      UIShell_RtLayoutEntry *anchor = cols->strip_len ? uishell_rt_cell_layout(s, str8_lit("Tern \xE2\x9A\x93"), cols->strip[0].size) : 0;
      B32 found = anchor && anchor->layout->text.glyph_count != 0 && s->layouts.layouts == layouts + drawn;
      for(U64 i = 0; found && i < anchor->layout->text.glyph_count; i += 1) { found = anchor->layout->text.glyphs[i].id != 0; }
      RichCheck(drawn != 0 && found, "a table cell's text is laid out with fallback, as Rich Text is");
      // Unfocused, the wheel over it scrolls the Rich Text.
      WheelhouseRichWindow w = {0};
      wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
      F64 top = w.top;
      d->mouse = center_2f32(grid->rect);
      s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Scroll, .pos = d->mouse, .delta_2f32 = v2f32(0, 1)});
      s = uishell_rt_diag_frames(d, 2);
      wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
      RichCheck(w.top > top, "the wheel over an unfocused table scrolls the Rich Text");
      s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Navigate, .delta_unit = UI_EventDeltaUnit_Whole, .delta_2s32 = v2s32(0, -1)});
      s = uishell_rt_diag_frames(d, 3);
      tb = uishell_rt_diag_box(scratch.arena, table->key);
      grid = &ui_nil_box;
      for(UI_Box *b = tb.box->first; !ui_box_is_nil(tb.box) && !ui_box_is_nil(b); b = b->next) { if(b->custom_draw == uishell_table_draw) { grid = b; } }
      d->now_us += 2000000;
      Vec2F32 cell = v2f32(grid->rect.x0 + 20, grid->rect.y0 + uishell_rt_table_line(&s->theme)*1.5f);
      s = uishell_rt_diag_click(d, cell, 0, 1);
      td = (UIShell_TableDraw *)grid->custom_draw_user_data;
      RichCheck(s->focus_embed == table->key, "a click focuses the table Embed");
      tb = uishell_rt_diag_box(scratch.arena, table->key);
      RichCheck(tb.draw && tb.draw->focused, "the focused Embed shows its focus");
      wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
      top = w.top;
      d->mouse = cell;
      s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Scroll, .pos = d->mouse, .delta_2f32 = v2f32(0, 1)});
      wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
      RichCheck(w.top == top, "a focused table takes the wheel");
      s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_Esc, .slot = UI_EventActionSlot_Cancel});
      WheelhouseRichSelection sel = {0};
      B32 selected = wheelhouse_rich_text_selection(s->doc, s->sub, &sel, &error);
      RichCheck(s->focus_embed == 0 && selected && sel.anchor_key == table->key && sel.focus_key == table->key,
                "Esc returns focus to the Rich Text, the Embed selected");
      // Tab moves between links, Embeds and toggles: it reaches the table.
      B32 reached = 0;
      for(U32 i = 0; i < 12 && !reached; i += 1)
      {
        s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Navigate, .flags = UI_EventFlag_Secondary, .delta_unit = UI_EventDeltaUnit_Char, .delta_2s32 = v2s32(0, 1)});
        reached = s->focus_embed == table->key;
      }
      RichCheck(reached, "Tab reaches the table Embed");
      s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_Esc, .slot = UI_EventActionSlot_Cancel});
      RichCheck(s->focus_embed == 0, "Esc leaves it again");
    }
    scratch_end(scratch);
  }

  //- The source toggle shows the file in the code/text view and back.
  {
    uishell_rt_toggle_source(d->address);
    RichCheck(str8_match(rd_cfg_from_view(d->address)->string, str8_lit("text"), 0), "the source toggle shows the source in the text view");
    uishell_rt_toggle_source(d->address);
    RichCheck(str8_match(rd_cfg_from_view(d->address)->string, str8_lit("rich_text"), 0), "and toggles back to Rich Text");
    s = uishell_rt_diag_open(d, path);
  }

  //- Inside a right-to-left run, a drag and Shift with an arrow select
  // what they pass over on screen.
  {
    Temp scratch = scratch_begin(&arena, 1);
    String8 rtl_path = push_str8f(arena, "%S/rich-text-rtl.md", dir);
    // Hebrew between English: its second word starts at byte 16, a letter
    // two bytes.
    write_data_to_file_path(rtl_path, str8_lit("Before \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D \xD7\x98\xD7\x95\xD7\x91 after.\n"));
    s = uishell_rt_diag_open(d, rtl_path);
    UIShell_RtNode *n = uishell_rt_diag_block_with(s, str8_lit("Before"));
    RichCheck(n != 0, "a paragraph with a right-to-left run opens");
    if(n)
    {
      Vec2F32 from = uishell_rt_diag_point(d, s, n->key, 16), to = uishell_rt_diag_point(d, s, n->key, 20);
      for(U32 keys = 0; keys < 2; keys += 1)
      {
        d->now_us += 2000000;
        d->mouse = from;
        s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = from});
        if(!keys) { d->mouse = to; s = uishell_rt_diag_frames(d, 2); }
        s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = d->mouse});
        for(U32 i = 0; keys && i < 2; i += 1)
        {
          s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Navigate, .flags = UI_EventFlag_KeepMark|UI_EventFlag_ExplicitDirectional,
                                                  .delta_unit = UI_EventDeltaUnit_Char, .delta_2s32 = v2s32(-1, 0)});
        }
        s = uishell_rt_diag_frames(d, 1);
        // The two letters left of the press, from its caret to theirs.
        UIShell_RtDiagBox box = uishell_rt_diag_box(scratch.arena, n->key);
        B32 spans = box.draw && box.draw->selection_quads == 1 &&
                    abs_f32(box.box->rect.x0 + box.draw->quads[0].x0 - (to.x - 1)) < 0.5f &&
                    abs_f32(box.box->rect.x0 + box.draw->quads[0].x1 - (from.x - 1)) < 0.5f;
        RichCheck(spans && str8_match(uishell_rt_diag_copy(d), str8_lit("\xD7\xA2\xD7\x95"), 0),
                  keys ? "Shift with Left inside a right-to-left run selects leftwards on screen"
                       : "a drag inside a right-to-left run selects what it passes over");
      }
    }
    scratch_end(scratch);
  }

  //- A conversation (#616): its contract file through the Handlers to the
  // same Renderer, presented as chat.
  {
    Temp scratch = scratch_begin(&arena, 1);
    String8 chat = push_str8f(arena, "%S/harbour.events.jsonl", dir);
    write_data_to_file_path(chat, uishell_rt_diag_conversation(scratch.arena));
    RichCheck(str8_match(wh_handler_from_media_type(WH_Content_File, wh_media_type_from_path(chat))->renderer, str8_lit("rich_text"), 0) &&
              str8_match(wh_handler_from_media_type(WH_Content_File, str8_lit("application/vnd.wheelhouse.conversation+jsonl"))->renderer,
                         str8_lit("rich_text"), 0),
              "a conversation's file and media type resolve to the Rich Text Renderer");
    RichCheck(wh_media_type_from_path(str8_lit("/tmp/plain.jsonl")).size == 0 && wh_media_type_from_path(str8_lit("/tmp/.events.jsonl")).size == 0,
              "other JSON Lines, and a file named only .events.jsonl, resolve to no Handler");
    d->rect = r2f32p(0, 0, 900, 900);
    s = uishell_rt_diag_open(d, chat);
    RichCheck(s && s->doc && s->conversation && s->styles == uishell_rt_style_set_from_name(str8_lit("chat")),
              "a conversation opens with the chat style set");
    UIShell_RtNode *user = s ? uishell_rt_diag_container(s, WHEELHOUSE_RICH_KIND_TURN, WHEELHOUSE_RICH_ROLE_USER, str8_lit("Please run")) : 0;
    UIShell_RtNode *assistant = s ? uishell_rt_diag_container(s, WHEELHOUSE_RICH_KIND_TURN, WHEELHOUSE_RICH_ROLE_ASSISTANT, str8_lit("Running them")) : 0;
    UIShell_RtNode *bash = s ? uishell_rt_diag_container(s, WHEELHOUSE_RICH_KIND_SECTION, WHEELHOUSE_RICH_SECTION_TOOL, str8_lit("Bash")) : 0;
    UIShell_RtNode *reasoning = s ? uishell_rt_diag_container(s, WHEELHOUSE_RICH_KIND_SECTION, WHEELHOUSE_RICH_SECTION_REASONING, str8_zero()) : 0;
    UIShell_RtNode *subagent = s ? uishell_rt_diag_container(s, WHEELHOUSE_RICH_KIND_SECTION, WHEELHOUSE_RICH_SECTION_SUBAGENT, str8_zero()) : 0;
    RichCheck(user && assistant && bash && reasoning && subagent, "the conversation's Turns and Sections are in the model");
    if(user && assistant && bash && reasoning && subagent)
    {
      WheelhouseRichWindow w = {0};
      wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
      RichCheck(w.at_end, "a conversation opens at its newest");
      // To the top, where the Turns are.
      s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Navigate, .delta_unit = UI_EventDeltaUnit_Whole, .delta_2s32 = v2s32(0, -1)});
      s = uishell_rt_diag_frames(d, 3);
      Rng2F32 content = uishell_rt_diag_content(d);
      F32 column_x0 = content.x0 + s->content_x, column_x1 = column_x0 + s->width;

      //- A user Turn is a bubble: end-aligned, at most 75% wide, with a
      // background; an assistant Turn is page content across the column.
      UI_Box *bubble = uishell_rt_diag_drawn(uishell_rt_chrome_draw, user->key);
      UIShell_RtDiagBox user_text = uishell_rt_diag_box(scratch.arena, user->children[0]);
      fprintf(stderr, "RICH_TEXT_CHAT bubble=%.1f..%.1f column=%.1f..%.1f view=%.1f\n", bubble->rect.x0, bubble->rect.x1, column_x0, column_x1, dim_2f32(d->rect).x);
      RichCheck(!ui_box_is_nil(bubble) && abs_f32(bubble->rect.x1 - column_x1) < 0.5f && dim_2f32(bubble->rect).x <= 0.75f*s->width + 0.5f &&
                dim_2f32(bubble->rect).x < dim_2f32(d->rect).x && user_text.draw && user_text.box->rect.x0 > bubble->rect.x0 &&
                user_text.box->rect.x1 < bubble->rect.x1 && user_text.box->rect.y0 >= bubble->rect.y0 && user_text.box->rect.y1 <= bubble->rect.y1,
                "a user Turn is a bubble, end-aligned and narrower than the View, around its text");
      UIShell_RtDiagBox assistant_text = uishell_rt_diag_box(scratch.arena, assistant->children[0]);
      RichCheck(assistant_text.draw && abs_f32(assistant_text.box->rect.x0 - column_x0) < 0.5f && abs_f32(assistant_text.box->rect.x1 - column_x1) < 0.5f &&
                ui_box_is_nil(uishell_rt_diag_drawn(uishell_rt_chrome_draw, assistant->key)), "an assistant Turn spans the column, without chrome");

      //- Tool calls and reasoning are collapsed by default; a click, Enter
      // or Space toggles them.
      UI_Box *header = uishell_rt_diag_drawn(uishell_rt_header_draw, bash->key);
      U64 output = bash->child_count ? bash->children[0] : 0;
      RichCheck(!ui_box_is_nil(header) && uishell_rt_collapsed(s, bash->key) && uishell_rt_collapsed(s, reasoning->key) &&
                ui_box_is_nil(uishell_rt_diag_box(scratch.arena, output).box) && ((UIShell_RtHeaderDraw *)header->custom_draw_user_data)->collapsed,
                "a tool Section and reasoning are collapsed by default, showing their headers");
      RichCheck(uishell_rt_diag_contains(uishell_rt_diag_named(header, str8_lit("###rt_name"))->string, str8_lit("Bash")) &&
                uishell_rt_diag_contains(uishell_rt_diag_named(header, str8_lit("###rt_title"))->string, str8_lit("cargo test harbour")) &&
                !ui_box_is_nil(uishell_rt_diag_named(header, str8_lit("###rt_icon"))), "a tool Section's header has its icon, name and title");
      d->now_us += 2000000;
      s = uishell_rt_diag_click(d, center_2f32(header->rect), 0, 1);
      UIShell_RtDiagBox log = uishell_rt_diag_box(scratch.arena, output);
      RichCheck(!uishell_rt_collapsed(s, bash->key) && log.draw && log.draw->kind == WHEELHOUSE_RICH_KIND_LOG, "a click on the header expands it");
      if(log.draw)
      {
        // Its output is monospace, with ANSI's red through the palette.
        UIShell_RtNode *n = uishell_rt_node(&s->model, output);
        UIShell_RtLayoutEntry *e = 0;
        uishell_rt_block_height(s, n, dim_2f32(log.box->rect).x, &e);
        B32 red = 0;
        for(U64 i = 0; e && i < e->layout->style_count; i += 1)
        {
          UIShell_RtStyleInk *ink = &e->layout->inks[i];
          Vec4F32 c = uishell_rt_ink_color(&s->theme, ink);
          red |= (ink->ink & UIShell_RtInk_Literal) && abs_f32(c.x - ((s->theme.palette[1]>>16)&0xff)/255.f) < 0.01f &&
                 abs_f32(c.y - ((s->theme.palette[1]>>8)&0xff)/255.f) < 0.01f;
        }
        RichCheck(uishell_rt_base_style(&s->theme, n).family == WH_TEXT_FAMILY_CODE && red, "a log Block is monospace, its colours mapped through the theme's palette");
      }
      for(U32 key = 0; key < 2; key += 1)
      {
        // Tab to the header, then Space and Enter.
        B32 reached = 0;
        for(U32 i = 0; i < 24 && !reached; i += 1)
        {
          s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Navigate, .flags = UI_EventFlag_Secondary, .delta_unit = UI_EventDeltaUnit_Char, .delta_2s32 = v2s32(0, 1)});
          reached = s->focus_stop >= 0 && s->focus_stop_value.kind == UIShell_RtStop_Toggle && s->focus_stop_value.key == bash->key;
        }
        B32 before = uishell_rt_collapsed(s, bash->key);
        s = uishell_rt_diag_event(d, key ? (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_Return, .slot = UI_EventActionSlot_Accept}
                                         : (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_Space});
        RichCheck(reached && uishell_rt_collapsed(s, bash->key) == !before, key ? "Enter on a focused header toggles its Section" : "Space on a focused header toggles its Section");
        s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_Esc, .slot = UI_EventActionSlot_Cancel});
      }

      //- The status mark follows the status: a distinct mark for each.
      {
        String8 names[] = {str8_lit("Bash"), str8_lit("Read"), str8_lit("Edit"), str8_lit("Grep"), str8_lit("Fetch")};
        U32 wanted[] = {WHEELHOUSE_RICH_STATUS_FAILED, WHEELHOUSE_RICH_STATUS_PENDING, WHEELHOUSE_RICH_STATUS_OK, WHEELHOUSE_RICH_STATUS_INTERRUPTED,
                        WHEELHOUSE_RICH_STATUS_AWAITING_APPROVAL};
        String8 marks[ArrayCount(names)] = {0};
        B32 follows = 1;
        s = uishell_rt_diag_frames(d, 2);
        for EachElement(i, names)
        {
          UIShell_RtNode *tool = uishell_rt_diag_container(s, WHEELHOUSE_RICH_KIND_SECTION, WHEELHOUSE_RICH_SECTION_TOOL, names[i]);
          UI_Box *h = tool ? uishell_rt_diag_drawn(uishell_rt_header_draw, tool->key) : &ui_nil_box;
          marks[i] = uishell_rt_diag_status_mark(h);
          follows &= tool && tool->status == wanted[i] && ((UIShell_RtHeaderDraw *)h->custom_draw_user_data)->status == wanted[i] &&
                     str8_match(marks[i], rd_icon_kind_text_table[uishell_rt_status_icon(wanted[i])], 0);
          for(U64 j = 0; j < i; j += 1) { follows &= !str8_match(marks[i], marks[j], 0); }
        }
        RichCheck(follows, "each tool Section's status mark follows its status: pending, ok, failed, interrupted, awaiting approval");
      }

      //- Hover actions copy the message, the tool's output and command;
      // Copy All copies the transcript.
      {
        s = uishell_rt_diag_frames(d, 1);
        UIShell_RtDiagBox text = uishell_rt_diag_box(scratch.arena, user->children[0]);
        d->mouse = center_2f32(text.box->rect);
        s = uishell_rt_diag_frames(d, 2);
        UI_Box *copy = uishell_rt_diag_named(ui_state->root, push_str8f(scratch.arena, "###rt_action_%u", WHEELHOUSE_RICH_ACTION_MESSAGE));
        RichCheck(s->hover_key == user->key && !ui_box_is_nil(copy), "hovering a Turn shows its copy action");
        if(!ui_box_is_nil(copy))
        {
          d->now_us += 2000000;
          s = uishell_rt_diag_click(d, center_2f32(copy->rect), 0, 1);
          RichCheck(str8_match(d->clip.text, str8_lit("Please run the *harbour* tests."), 0), "copy message copies the Turn's markdown as plain text");
        }
        header = uishell_rt_diag_drawn(uishell_rt_header_draw, bash->key);
        d->mouse = v2f32(header->rect.x0 + 10, center_2f32(header->rect).y);
        s = uishell_rt_diag_frames(d, 2);
        U32 tool_actions[] = {WHEELHOUSE_RICH_ACTION_TOOL_OUTPUT, WHEELHOUSE_RICH_ACTION_COMMAND};
        String8 tool_copies[] = {str8_lit("error: berth 4\nkestrel ok"), str8_lit("cargo test harbour")};
        for EachElement(i, tool_actions)
        {
          UI_Box *button = uishell_rt_diag_named(ui_state->root, push_str8f(scratch.arena, "###rt_action_%u", tool_actions[i]));
          U64 writes = d->clip.writes;
          if(!ui_box_is_nil(button))
          {
            d->now_us += 2000000;
            Vec2F32 at = center_2f32(button->rect);
            s = uishell_rt_diag_click(d, at, 0, 1);
            d->mouse = v2f32(header->rect.x0 + 10, center_2f32(header->rect).y);
            s = uishell_rt_diag_frames(d, 1);
          }
          fprintf(stderr, "RICH_TEXT_CHAT action=%u copied=\"%.*s\"\n", tool_actions[i], str8_varg(d->clip.text));
          RichCheck(d->clip.writes == writes+1 && str8_match(d->clip.text, tool_copies[i], 0),
                    i == 0 ? "copy tool output copies its output without escapes" : "copy command copies the tool call's command");
        }
        UI_Box *all = uishell_rt_diag_named(ui_state->root, str8_lit("###rich_text_copy_all"));
        RichCheck(!ui_box_is_nil(all), "a conversation's bar has Copy All");
        if(!ui_box_is_nil(all))
        {
          // Backfilled first, a step a frame; then copied.
          d->now_us += 2000000;
          U64 writes = d->clip.writes;
          s = uishell_rt_diag_click(d, center_2f32(all->rect), 0, 1);
          for(U32 i = 0; i < 1000 && d->clip.writes == writes; i += 1) { s = uishell_rt_diag_frame(d, 0); }
          String8 text = d->clip.text;
          RichCheck(str8_match(str8_prefix(text, 9), str8_lit("## User\n\n"), 0) && uishell_rt_diag_contains(text, str8_lit("Please run the *harbour* tests.")) &&
                    uishell_rt_diag_contains(text, str8_lit("```sh\ncargo test harbour\n```")) && uishell_rt_diag_contains(text, str8_lit("Find the free berth.")) &&
                    uishell_rt_diag_contains(text, str8_lit("Done.")) && !uishell_rt_diag_contains(text, str8_lit("moorings")),
                    "Copy All copies the whole transcript, without reasoning");
        }
      }

      //- A Divider marks the compaction.
      {
        B32 divider = 0;
        for(U64 i = 0; i < s->model.block_count; i += 1)
        {
          UIShell_RtNode *n = uishell_rt_node(&s->model, s->model.block_keys[i]);
          divider |= n && n->kind == WHEELHOUSE_RICH_KIND_DIVIDER && str8_match(n->text, str8_lit("compaction"), 0);
        }
        RichCheck(divider, "a Divider marks the compaction");
      }

      //- A subagent's header, inside the call that started it, is a link
      // that opens its thread as its own View.
      {
        UIShell_RtNode *task = uishell_rt_diag_container(s, WHEELHOUSE_RICH_KIND_SECTION, WHEELHOUSE_RICH_SECTION_TOOL, str8_lit("Task"));
        if(task && uishell_rt_collapsed(s, task->key)) { uishell_rt_toggle(s, task->key); }
        s = uishell_rt_diag_frames(d, 2);
        UI_Box *link = &ui_nil_box;
        for(U32 i = 0; i < 20 && ui_box_is_nil(link); i += 1)
        {
          link = uishell_rt_diag_drawn(uishell_rt_header_draw, subagent->key);
          if(ui_box_is_nil(link)) { s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Scroll, .pos = center_2f32(content), .delta_2f32 = v2f32(0, 1)}); }
        }
        String8 asked = str8_zero();
        if(!ui_box_is_nil(link))
        {
          d->now_us += 2000000;
          d->mouse = center_2f32(link->rect);
          asked = uishell_rt_diag_event_cmd(d, (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = d->mouse},
                                            str8_lit("open_conversation_thread"), scratch.arena);
          s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = d->mouse});
        }
        RichCheck(str8_match(asked, subagent->title, 0) && uishell_rt_collapsed(s, subagent->key), "a click on a subagent's header asks for its thread, not a toggle");
        RD_PanelOwner owner = d->panel.owner;
        U64 before = rd_fixture_count(owner, str8_lit("rich_text"));
        uishell_rt_open_thread(d->address, asked);
        CFG_Node *opened = &cfg_nil_node;
        for(CFG_Node *n = d->view->parent->first; n != &cfg_nil_node; n = n->next)
        {
          if(str8_match(cfg_node_child_from_string(n, str8_lit("thread"))->first->string, asked, 0)) { opened = n; }
        }
        RichCheck(rd_fixture_count(owner, str8_lit("rich_text")) == before + 1 && opened != &cfg_nil_node, "opening the thread adds a View of it beside this one");
        if(opened != &cfg_nil_node)
        {
          d->view = opened;
          d->address = rd_view_address_from_cfg(opened);
          s = uishell_rt_diag_frames(d, 1);
          for(U32 i = 0; i < 1000 && !(s && s->doc && s->model.block_count); i += 1) { s = uishell_rt_diag_frame(d, 0); sleep_ms(5); }
          s = uishell_rt_diag_frames(d, 3);
          B32 own = uishell_rt_diag_block_with(s, str8_lit("Find the free berth.")) != 0;
          B32 other = uishell_rt_diag_block_with(s, str8_lit("Please run")) != 0;
          RichCheck(s->conversation && own && !other, "the thread's View shows its thread alone");
        }
      }
    }
    d->rect = r2f32p(0, 0, 900, 600);
    scratch_end(scratch);
  }

  //- Timing: the generated 35 MB transcript, to its first painted tail.
  {
    WheelhouseBytes bytes = {0};
    String8 large = push_str8f(arena, "%S/rich-text-large.events.jsonl", dir);
    if(wheelhouse_rich_text_generate_conversation(7, 35u << 20, &bytes, &error))
    {
      write_data_to_file_path(large, str8(bytes.data, bytes.len));
      wheelhouse_bytes_free(bytes);
    }
    d->rect = r2f32p(0, 0, 1200, 900);
    U64 begin = now_time_us();
    CFG_Node *window = cfg_node_from_id(d->ws->cfg_id);
    CFG_Node *host = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
    d->panel = rd_fixture_leaf(rd_fixture_owner(host));
    d->view = rd_fixture_view(d->panel, str8_lit("rich_text"), rd_eval_string_from_file_path(d->arena, large), 1);
    d->address = rd_view_address_from_cfg(d->view);
    U64 frames = 0;
    for(; frames < 4000; frames += 1)
    {
      s = uishell_rt_diag_frame(d, 0);
      if(s && s->first_tail_us) { break; }
      if(!(s && s->doc)) { sleep_ms(1); }
    }
    B32 painted = s && s->first_tail_us != 0;
    // Backfill steps a frame at a time behind the tail.
    U64 backfill_frames = painted ? 60 : 0;
    s = painted ? uishell_rt_diag_frames(d, (U32)backfill_frames) : s;
    WheelhouseRichWindow w = {0};
    if(painted) { wheelhouse_rich_text_window(s->doc, s->sub, &w, &error); }
    printf("RICH_TEXT_CONVERSATION_BENCH build=%s transcript_bytes=%llu first_tail_ms=%.1f open_to_tail_ms=%.1f frames=%llu backfill_frames=%llu blocks=%llu\n",
           BUILD_DEBUG ? "debug" : "release", (unsigned long long)properties_from_file_path(large).size,
           painted ? (s->first_tail_us - begin)/1000.0 : -1.0, painted ? (s->first_tail_us - s->opened_us)/1000.0 : -1.0,
           (unsigned long long)frames, (unsigned long long)backfill_frames, painted ? (unsigned long long)s->model.block_count : 0ull);
    RichCheck(painted && w.at_end, "the 35 MB transcript paints its tail, and stays there as backfill runs");
    d->rect = r2f32p(0, 0, 900, 600);
  }

  //- Timing: the 10k-line generated document, scrolled through a frame at a time.
  {
    WheelhouseBytes bytes = {0};
    String8 large = push_str8f(arena, "%S/rich-text-large.md", dir);
    if(wheelhouse_rich_text_generate(7, 10000, &bytes, &error))
    {
      write_data_to_file_path(large, str8(bytes.data, bytes.len));
      wheelhouse_bytes_free(bytes);
    }
    d->rect = r2f32p(0, 0, 1200, 900);
    s = uishell_rt_diag_open(d, large);
    RichCheck(s && s->doc && s->model.block_count > 1000, "the 10k-line document opens");
    if(s && s->doc)
    {
      U64 frames = 0, cap = 20000, layouts = 0;
      U64 *layout_us = push_array(arena, U64, cap);
      U64 *frame_us = push_array(arena, U64, cap);
      Rng2F32 content = uishell_rt_diag_content(d);
      d->mouse = center_2f32(content);
      WheelhouseRichWindow w = {0};
      // A notch of the wheel a frame: three lines, at 60 frames a second.
      for(; frames < cap; frames += 1)
      {
        U64 begin = now_time_us();
        s = uishell_rt_diag_event(d, (UI_Event){.kind = UI_EventKind_Scroll, .pos = d->mouse, .delta_2f32 = v2f32(0, 1)});
        frame_us[frames] = now_time_us() - begin;
        layout_us[frames] = s->stats.frame_layout_us;
        layouts += s->stats.frame_layouts;
        wheelhouse_rich_text_window(s->doc, s->sub, &w, &error);
        if(w.at_end) { frames += 1; break; }
      }
      qsort(layout_us, frames, sizeof(U64), uishell_rt_diag_u64_compare);
      qsort(frame_us, frames, sizeof(U64), uishell_rt_diag_u64_compare);
      U64 layout_p50 = layout_us[frames/2], layout_p95 = layout_us[frames*95/100], layout_max = layout_us[frames-1];
      U64 frame_p50 = frame_us[frames/2], frame_p95 = frame_us[frames*95/100], frame_max = frame_us[frames-1];
      printf("RICH_TEXT_BENCH document_bytes=%llu blocks=%llu frames=%llu blocks_laid_out=%llu layout_us_p50=%llu layout_us_p95=%llu layout_us_max=%llu frame_us_p50=%llu frame_us_p95=%llu frame_us_max=%llu\n",
             (unsigned long long)properties_from_file_path(large).size, (unsigned long long)s->model.block_count, (unsigned long long)frames,
             (unsigned long long)layouts, (unsigned long long)layout_p50, (unsigned long long)layout_p95, (unsigned long long)layout_max,
             (unsigned long long)frame_p50, (unsigned long long)frame_p95, (unsigned long long)frame_max);
      RichCheck(w.at_end, "the scroll reaches the document's end");
      printf("RICH_TEXT_BUDGET build=%s layout_us_p95_budget=%llu\n", BUILD_DEBUG ? "debug" : "release", (unsigned long long)UISHELL_RT_DIAG_LAYOUT_BUDGET_US);
      RichCheck(layout_p95 <= UISHELL_RT_DIAG_LAYOUT_BUDGET_US && layout_max <= UISHELL_RT_DIAG_LAYOUT_MAX_US, "layout cost per frame is inside its budget");
    }
  }

  done:;
  fprintf(stderr, "rich text diagnostics %s (%u failures)\n", d->failures == 0 ? "passed" : "failed", d->failures);
  wm_clipboard_io = saved_clip;
  rd_state->cmds[0] = saved_commands[0];
  rd_state->cmds[1] = saved_commands[1];
  rd_state->cmds_gen = saved_generation;
  ws->ui = saved_window_ui;
  ui_select_state(saved_ui);
  B32 ok = d->failures == 0;
  arena_release(arena);
  return ok;
}
#undef RichCheck
