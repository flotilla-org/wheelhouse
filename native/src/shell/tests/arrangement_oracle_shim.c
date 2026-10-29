// The oracle's calls (arrangement_oracle.c), for
// crates/wheelhouse-arrangement-oracle. The Rust side serialises every call
// (one lock), so the globals below need no locking of their own.
//
// A tab is a number n >= 1: the View Address of key n, as one Workspace's.
// 0 is no View. Panels are numbered as the arrangement numbers them.

// The base layer's own `main` is renamed out of the way (build.rs); its
// entry point is never run.
internal void entry_point(CmdLine *cmd_line) {}

#define ORACLE_EXPORT __attribute__((visibility("default")))

typedef struct OracleArrangement OracleArrangement;
struct OracleArrangement
{
  Arena *arena;
  RD_Arrangement *arrangement;
};

// What the base layer's `main` would have set up, for each calling thread.
internal void
oracle_init(void)
{
  local_persist B32 system_ready = 0;
  if(!system_ready)
  {
#if OS_LINUX
    SystemInfo *info = &lnx_state.system_info;
#elif OS_MAC
    SystemInfo *info = &mac_state.system_info;
#endif
    info->logical_processor_count = 1;
    info->page_size = (U64)getpagesize();
    info->large_page_size = MB(2);
    info->allocation_granularity = info->page_size;
    system_ready = 1;
  }
  if(tctx_selected() == 0) { tctx_select(tctx_alloc()); }
}

internal RD_ViewAddress
oracle_view(U64 tab)
{
  RD_ViewAddress result = {0};
  if(tab == 0) { return result; }
  result.owner = RD_ViewOwnerKind_Workspace;
  result.key.u64[0] = tab;
  return result;
}

internal U64
oracle_tab(RD_ViewAddress view)
{
  return rd_view_address_is_nil(view) ? 0 : view.key.u64[0];
}

internal Dir2
oracle_dir(S32 dir)
{
  return dir < 0 ? Dir2_Invalid : (Dir2)dir;
}

//- Which tabs are hidden: the settle rule's `shown`.
global U64 oracle_hidden[64];
global U64 oracle_hidden_count;

internal B32
oracle_shown(RD_ViewAddress view)
{
  for(U64 i = 0; i < oracle_hidden_count; i++) { if(oracle_hidden[i] == oracle_tab(view)) { return 0; } }
  return 1;
}

ORACLE_EXPORT void
oracle_set_hidden(const U64 *tabs, U64 count)
{
  oracle_hidden_count = Min(count, ArrayCount(oracle_hidden));
  for(U64 i = 0; i < oracle_hidden_count; i++) { oracle_hidden[i] = tabs[i]; }
}

//- Arrangements

ORACLE_EXPORT OracleArrangement *
oracle_new(void)
{
  oracle_init();
  Arena *arena = arena_alloc();
  OracleArrangement *h = push_array(arena, OracleArrangement, 1);
  h->arena = arena;
  h->arrangement = rd_arrangement_alloc(arena, (RD_PanelOwner){RD_PanelOwnerKind_Workspace});
  return h;
}

ORACLE_EXPORT void
oracle_free(OracleArrangement *h)
{
  arena_release(h->arena);
}

// Gives `panel` a provider's name, "n<name>", or none for 0.
ORACLE_EXPORT void
oracle_set_named(OracleArrangement *h, U64 panel, U64 name)
{
  RD_ArrangementPanel *p = rd_arrangement_panel_from_id(h->arrangement, panel);
  if(p != &rd_nil_arrangement_panel) { p->doc_id = name ? push_str8f(h->arena, "n%I64u", name) : str8_zero(); }
}

// The arrangement as numbers: next_id, root axis (0 Row, 1 Column), the
// panel count, then each panel in tree order: id, parent index (~0 for the
// root), weight bits, options_from, restore_emptied, name, selected, tab
// count and tabs; then the removed count and removed tabs, newest first.
// Returns the length, writing what fits.
ORACLE_EXPORT U64
oracle_dump(OracleArrangement *h, U64 *out, U64 cap)
{
  RD_Arrangement *a = h->arrangement;
  U64 n = 0;
#define Put(x) do { U64 v__ = (U64)(x); if(n < cap) { out[n] = v__; } n += 1; } while(0)
  Put(a->next_id);
  Put(a->root_axis == Axis2_X ? 0 : 1);
  U64 panels = 0;
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p)) { panels += 1; }
  Put(panels);
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  {
    U64 parent = ~(U64)0, index = 0;
    for(RD_ArrangementPanel *q = a->root; q != &rd_nil_arrangement_panel; q = rd_arrangement_next(a->root, q), index += 1)
    {
      if(q == p->parent) { parent = index; }
    }
    U32 bits = 0;
    MemoryCopy(&bits, &p->weight, sizeof(bits));
    Put(p->id);
    Put(parent);
    Put(bits);
    Put(p->options_from);
    Put(p->restore_emptied);
    Put(p->doc_id.size ? u64_from_str8(str8_skip(p->doc_id, 1), 10) : 0);
    Put(oracle_tab(p->selected));
    Put(p->tab_count);
    for(RD_ArrangementTab *tab = p->first_tab; tab != 0; tab = tab->next) { Put(oracle_tab(tab->view)); }
  }
  U64 removed = 0;
  for(RD_ArrangementTab *tab = a->first_removed; tab != 0; tab = tab->next) { removed += 1; }
  Put(removed);
  for(RD_ArrangementTab *tab = a->first_removed; tab != 0; tab = tab->next) { Put(oracle_tab(tab->view)); }
#undef Put
  return n;
}

// rd_arrangement_problem's text, writing what fits; its length.
ORACLE_EXPORT U64
oracle_problem(OracleArrangement *h, U8 *out, U64 cap)
{
  Temp scratch = scratch_begin(0, 0);
  String8 problem = rd_arrangement_problem(scratch.arena, h->arrangement);
  MemoryCopy(out, problem.str, Min(cap, problem.size));
  scratch_end(scratch);
  return problem.size;
}

ORACLE_EXPORT F32
oracle_quantize(F32 weight)
{
  oracle_init();
  return rd_arrangement_quantize(weight);
}

// The words rd_dock_rule_message gives a rule, into `out`; their length.
ORACLE_EXPORT U64
oracle_rule_message(U32 rule, U8 *out, U64 cap)
{
  oracle_init();
  String8 message = rd_dock_rule_message((RD_DockRule)rule);
  U64 n = Min(message.size, cap);
  MemoryCopy(out, message.str, n);
  return message.size;
}

ORACLE_EXPORT U64 oracle_clear(OracleArrangement *h) { return rd_arrangement_clear(h->arrangement); }
ORACLE_EXPORT void oracle_discard(OracleArrangement *h) { rd_arrangement_discard(h->arrangement); }
ORACLE_EXPORT U64 oracle_split(OracleArrangement *h, U64 panel, S32 dir) { return rd_arrangement_split(h->arrangement, panel, oracle_dir(dir)); }
ORACLE_EXPORT U64 oracle_add(OracleArrangement *h, U64 parent, F32 weight) { return rd_arrangement_add(h->arrangement, parent, weight); }
ORACLE_EXPORT U32 oracle_remove(OracleArrangement *h, U64 panel) { return rd_arrangement_remove(h->arrangement, panel); }
ORACLE_EXPORT U64 oracle_close(OracleArrangement *h, U64 panel) { return rd_arrangement_close(h->arrangement, panel); }
ORACLE_EXPORT U32 oracle_insert_tab(OracleArrangement *h, U64 tab, U64 to, U64 after, U32 select)
{ return rd_arrangement_insert_tab(h->arrangement, oracle_view(tab), to, oracle_view(after), select); }
ORACLE_EXPORT U32 oracle_detach_tab(OracleArrangement *h, U64 tab) { return rd_arrangement_detach_tab(h->arrangement, oracle_view(tab)); }
ORACLE_EXPORT U32 oracle_remove_tab(OracleArrangement *h, U64 tab) { return rd_arrangement_remove_tab(h->arrangement, oracle_view(tab)); }
ORACLE_EXPORT U32 oracle_select(OracleArrangement *h, U64 panel, U64 tab) { return rd_arrangement_select(h->arrangement, panel, oracle_view(tab)); }
ORACLE_EXPORT U32 oracle_reorder(OracleArrangement *h, U64 panel, U64 after) { return rd_arrangement_reorder(h->arrangement, panel, after); }
ORACLE_EXPORT void oracle_resize(OracleArrangement *h, U64 panel, F32 delta, F32 floor) { rd_arrangement_resize(h->arrangement, panel, delta, floor); }
ORACLE_EXPORT void oracle_equalize(OracleArrangement *h, U64 panel) { rd_arrangement_equalize(h->arrangement, panel); }
ORACLE_EXPORT U32 oracle_readdress(OracleArrangement *h, U64 from, U64 to) { return rd_arrangement_readdress(h->arrangement, oracle_view(from), oracle_view(to)); }

// rd_arrangement_settle, with the heir it discards.
ORACLE_EXPORT U32
oracle_settle(OracleArrangement *h, U64 panel, S32 dir, U32 may_close, U64 *heir)
{
  *heir = 0;
  return rd_arrangement_settle__heir(h->arrangement, rd_arrangement_panel_from_id(h->arrangement, panel), oracle_dir(dir), oracle_shown, may_close, heir);
}

// rd_arrangement_drop: out is panel, source, heir.
ORACLE_EXPORT void
oracle_drop(OracleArrangement *h, U64 tab, U64 to, S32 dir, U64 after, U64 *out)
{
  RD_ArrangementDrop drop = rd_arrangement_drop(h->arrangement, oracle_view(tab), to, oracle_dir(dir), oracle_view(after), oracle_shown);
  out[0] = drop.panel;
  out[1] = drop.source;
  out[2] = drop.heir;
}

// A drop whose source is in another arrangement, as uishell_drop_view
// (shell_commands.c) edits the two: out is the drop's panel, source and
// heir, then whether the source arrangement changed and its settle's heir.
ORACLE_EXPORT void
oracle_drop_across(OracleArrangement *h, OracleArrangement *other, U64 tab, U64 to, S32 dir, U64 after, U64 *out)
{
  RD_ViewAddress view = oracle_view(tab);
  RD_ArrangementPanel *from = rd_arrangement_panel_from_view(other->arrangement, view);
  RD_PanelID source_id = from->id;
  oracle_drop(h, tab, to, dir, after, out);
  out[3] = out[4] = 0;
  if(out[0] == 0 || out[1] != 0) { return; }
  RD_Arrangement *left = other->arrangement;
  RD_ArrangementPanel *left_panel = rd_arrangement_panel_from_id(left, source_id);
  out[3] = rd_arrangement_detach_tab(left, view);
  out[3] |= rd_arrangement_settle__heir(left, left_panel, oracle_dir(dir), oracle_shown, left_panel->tab_count == 0, &out[4]);
}

//- Docking

typedef struct OracleHost OracleHost;
struct OracleHost
{
  U32 kind; // RD_DockHostKind, COUNT for none
  U32 has_workspace_subject;
  U64 controlled_split;
  U64 workspace_region;
  U64 level;
  F32 available_width;
  U32 pad;
};

typedef struct OracleProposal OracleProposal;
struct OracleProposal
{
  OracleHost host;
  U64 selected_workspace_region;
  U64 view_level;
  U32 instances_after;
  U32 control_surfaces_after;
  U32 closing;
  U32 pad;
};

typedef struct OracleRegistration OracleRegistration;
struct OracleRegistration
{
  U32 traits;
  F32 minimum_width;
  U32 default_host;
  // Named "sidebar_section" when nonzero, else "r<index>".
  U32 sidebar_section;
};

internal RD_DockHost
oracle_host(OracleHost h)
{
  RD_DockHost host = {(RD_DockHostKind)h.kind, h.controlled_split, h.workspace_region, h.has_workspace_subject, h.available_width, h.level};
  return host;
}

global Arena *oracle_registry_arena;

ORACLE_EXPORT void
oracle_set_registrations(const OracleRegistration *r, U64 count)
{
  oracle_init();
  if(oracle_registry_arena == 0) { oracle_registry_arena = arena_alloc(); }
  arena_clear(oracle_registry_arena);
  MemoryZeroArray(rd_view_registrations);
  rd_view_registration_count = Min(count, ArrayCount(rd_view_registrations));
  for(U64 i = 0; i < rd_view_registration_count; i++)
  {
    RD_ViewRegistration *v = &rd_view_registrations[i];
    v->name = r[i].sidebar_section ? str8_lit("sidebar_section") : push_str8f(oracle_registry_arena, "r%I64u", i);
    v->traits = r[i].traits;
    v->minimum_width = r[i].minimum_width;
    v->default_host = (RD_DockHostKind)r[i].default_host;
  }
}

internal RD_ViewRegistration *
oracle_registration(S64 index)
{
  return index < 0 ? 0 : &rd_view_registrations[index];
}

//- Instance counts by Controlled Split: rd_dock_instance_count, when on.
global U64 oracle_instance_splits[8];
global U32 oracle_instance_counts[8];
global U64 oracle_instance_count;

internal U32
oracle_instances(CFG_ID controlled_split, RD_ViewRegistration *registration)
{
  for(U64 i = 0; i < oracle_instance_count; i++) { if(oracle_instance_splits[i] == controlled_split) { return oracle_instance_counts[i]; } }
  return 0;
}

ORACLE_EXPORT void
oracle_set_instances(U32 enabled, const U64 *splits, const U32 *counts, U64 count)
{
  rd_dock_instance_count = enabled ? oracle_instances : 0;
  oracle_instance_count = Min(count, ArrayCount(oracle_instance_splits));
  for(U64 i = 0; i < oracle_instance_count; i++) { oracle_instance_splits[i] = splits[i]; oracle_instance_counts[i] = counts[i]; }
}

ORACLE_EXPORT U32
oracle_check(S64 registration, OracleProposal p)
{
  RD_DockProposal proposal = {oracle_host(p.host), p.selected_workspace_region, p.instances_after, p.control_surfaces_after, p.closing, p.view_level};
  return rd_dock_check(oracle_registration(registration), proposal);
}

internal RD_DockView
oracle_dock_view(U32 exists, S64 registration, OracleHost source, U64 level)
{
  RD_DockView view = {exists, oracle_registration(registration), oracle_host(source), level};
  return view;
}

ORACLE_EXPORT U32
oracle_placement(U32 exists, S64 registration, OracleHost source, U64 level, OracleHost destination)
{ return rd_dock_placement(oracle_dock_view(exists, registration, source, level), oracle_host(destination)); }

// By name, as rd_dock_creation looks a View up; -1 is a name nothing has.
ORACLE_EXPORT U32
oracle_creation(S64 registration, OracleHost destination)
{
  String8 name = registration < 0 ? str8_lit("unregistered") : rd_view_registrations[registration].name;
  return rd_dock_creation(name, oracle_host(destination));
}

ORACLE_EXPORT U32
oracle_closure(U32 exists, S64 registration, OracleHost source, U64 level)
{ return rd_dock_closure(oracle_dock_view(exists, registration, source, level)); }

ORACLE_EXPORT U64
oracle_view_level(S64 registration, OracleHost host)
{ return rd_dock_view_level(oracle_registration(registration), oracle_host(host)); }

//- Restore: each tab's registration, by tab.
global U64 oracle_tab_keys[256];
global S64 oracle_tab_registrations[256];
global U64 oracle_tab_count;

internal RD_ViewRegistration *
oracle_tab_registration(RD_ViewAddress view)
{
  for(U64 i = 0; i < oracle_tab_count; i++) { if(oracle_tab_keys[i] == oracle_tab(view)) { return oracle_registration(oracle_tab_registrations[i]); } }
  return 0;
}

ORACLE_EXPORT void
oracle_set_tab_registrations(const U64 *tabs, const S64 *registrations, U64 count)
{
  oracle_tab_count = Min(count, ArrayCount(oracle_tab_keys));
  for(U64 i = 0; i < oracle_tab_count; i++) { oracle_tab_keys[i] = tabs[i]; oracle_tab_registrations[i] = registrations[i]; }
}

// rd_dock_restore over `count` arrangements with their hosts; the sidebar's
// and workspace's indices, or -1. Writes each document's dirty mark and the
// pending sections (what fits); returns how many are pending.
ORACLE_EXPORT U64
oracle_restore(OracleArrangement **arrangements, const OracleHost *hosts, U64 count, S64 sidebar, S64 workspace,
               U32 *dirty, U64 *pending, U64 cap)
{
  Temp scratch = scratch_begin(0, 0);
  RD_DockDocuments documents = {scratch.arena};
  RD_DockDocument **list = push_array(scratch.arena, RD_DockDocument *, count);
  for(U64 i = 0; i < count; i++) { list[i] = rd_dock_documents_add(&documents, arrangements[i]->arrangement, oracle_host(hosts[i])); }
  documents.sidebar = sidebar < 0 ? 0 : list[sidebar];
  documents.workspace = workspace < 0 ? 0 : list[workspace];
  RD_ViewAddressList moved = rd_dock_restore(&documents, oracle_tab_registration);
  for(U64 i = 0; i < count; i++) { dirty[i] = list[i]->dirty; }
  U64 n = 0;
  for(RD_ViewAddressNode *node = moved.first; node != 0; node = node->next, n += 1) { if(n < cap) { pending[n] = oracle_tab(node->v); } }
  scratch_end(scratch);
  return n;
}
