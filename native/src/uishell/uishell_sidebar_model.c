//- The sidebar's model, built only from the core's change stream
// (docs/design/change-stream.md; wheelhouse.h, wheelhouse_poll). Sections in
// order, each a node of its own whose children are its roots; nodes by key
// within their section; each node's children in order. A delta is applied
// whole: until it is, the model is not read.
//
// It owns its text. Content a delta replaces or removes stays in the arena
// as garbage until there is more garbage than content, when the live tree
// is copied into a fresh arena (uishell_sidebar_model_compact). Nodes keep
// their address across an Upsert, so child lists hold pointers; a
// compaction moves them all, and the preorder is rebuilt.
//
// The preorder (sections, each followed by its nodes depth first) is what
// a snapshot lists, and is rebuilt only when a list changed. A node's
// `revision` is the generation its content last changed at, so a cache of
// something drawn from one node can tell it is stale without comparing.

typedef struct UIShell_SidebarModelNode UIShell_SidebarModelNode;
typedef struct UIShell_SidebarModelSection UIShell_SidebarModelSection;

typedef struct UIShell_SidebarModelList UIShell_SidebarModelList;
struct UIShell_SidebarModelList
{
  UIShell_SidebarModelNode **v;
  U64 count;
  U64 capacity;
};

struct UIShell_SidebarModelNode
{
  UIShell_SidebarModelNode *hash_next;
  UIShell_SidebarModelSection *section;
  // The node whose list holds it: its section's own node for a root, none
  // for a section's.
  UIShell_SidebarModelNode *parent;
  // Its content as the delta gave it, with text the model owns. The
  // first_* indices are meaningless here: read `fields`, `details` and
  // `controls`.
  WheelhouseStreamNode view;
  WheelhouseField *fields;
  WheelhouseField *details;
  WheelhouseControl *controls;
  UIShell_SidebarModelList children;
  U64 revision;
  // Its preorder position, while the preorder is current.
  U64 index;
  U64 bytes;
};

struct UIShell_SidebarModelSection
{
  String8 name;
  UIShell_SidebarModelNode *node;
  UIShell_SidebarModelNode **slots;
  U64 slot_count;
  U64 node_count;
  // Removed by this delta: its nodes' REMOVE ops still find it.
  B32 removed;
};

typedef struct UIShell_SidebarModel UIShell_SidebarModel;
struct UIShell_SidebarModel
{
  Arena *arena;
  U64 generation;
  // Every section, and the order of them: SECTION adds one, SECTIONS
  // places it.
  UIShell_SidebarModelSection **sections;
  U64 section_count;
  U64 section_capacity;
  UIShell_SidebarModelSection **order;
  U64 order_count;
  U64 order_capacity;
  WheelhouseDisplayVariable *variables;
  U64 variable_count;
  WheelhouseText *diagnostics;
  U64 diagnostic_count;
  WheelhouseActivationError *errors;
  U64 error_count;
  UIShell_SidebarModelNode **preorder;
  U64 preorder_count;
  B32 preorder_dirty;
  U64 live;
  U64 garbage;
  U64 compactions;
};

// Garbage below this is never worth a copy; 0 compacts on every apply.
global U64 uishell_sidebar_model_compact_floor = MB(1);

// Arenas chain as the model grows, so a small reserve only bounds address
// space; compaction holds two at once.
global U64 uishell_sidebar_model_arena_reserve = MB(4);

internal WheelhouseText
uishell_sidebar_model_text(Arena *arena, WheelhouseText text)
{
  U8 *data = push_array_no_zero(arena, U8, text.len+1);
  if(text.len) { MemoryCopy(data, text.data, text.len); }
  data[text.len] = 0;
  return (WheelhouseText){data, text.len};
}

internal WheelhouseEntity
uishell_sidebar_model_entity(Arena *arena, WheelhouseEntity entity)
{
  return (WheelhouseEntity){uishell_sidebar_model_text(arena, entity.provider),
    uishell_sidebar_model_text(arena, entity.kind), uishell_sidebar_model_text(arena, entity.id)};
}

internal WheelhouseField *
uishell_sidebar_model_fields(Arena *arena, WheelhouseField *from, U64 count)
{
  WheelhouseField *fields = push_array(arena, WheelhouseField, count);
  for(U64 i = 0; i < count; i++)
  {
    fields[i] = from[i];
    fields[i].text = uishell_sidebar_model_text(arena, from[i].text);
  }
  return fields;
}

internal WheelhouseControl *
uishell_sidebar_model_controls(Arena *arena, WheelhouseControl *from, U64 count)
{
  WheelhouseControl *controls = push_array(arena, WheelhouseControl, count);
  for(U64 i = 0; i < count; i++)
  {
    controls[i] = from[i];
    controls[i].label = uishell_sidebar_model_text(arena, from[i].label);
    controls[i].glyph = uishell_sidebar_model_text(arena, from[i].glyph);
    controls[i].value = uishell_sidebar_model_text(arena, from[i].value);
    controls[i].variable = uishell_sidebar_model_text(arena, from[i].variable);
  }
  return controls;
}

// Copy a node's content into the model's arena: from a delta, or from the
// node itself when compacting.
internal void
uishell_sidebar_model_set_content(UIShell_SidebarModel *model, UIShell_SidebarModelNode *node,
                                  WheelhouseStreamNode view, WheelhouseField *fields,
                                  WheelhouseField *details, WheelhouseControl *controls)
{
  Arena *arena = model->arena;
  U64 start = arena_pos(arena);
  WheelhouseStreamNode *v = &node->view;
  *v = view;
  v->key = uishell_sidebar_model_text(arena, view.key);
  v->entity = uishell_sidebar_model_entity(arena, view.entity);
  v->label = uishell_sidebar_model_text(arena, view.label);
  v->layout = uishell_sidebar_model_text(arena, view.layout);
  v->form = uishell_sidebar_model_text(arena, view.form);
  v->loop_key = uishell_sidebar_model_text(arena, view.loop_key);
  v->default_host = uishell_sidebar_model_text(arena, view.default_host);
  v->status = uishell_sidebar_model_text(arena, view.status);
  v->icon = uishell_sidebar_model_text(arena, view.icon);
  v->short_label = uishell_sidebar_model_text(arena, view.short_label);
  v->medium_label = uishell_sidebar_model_text(arena, view.medium_label);
  v->first_field = v->first_detail = v->first_control = 0;
  node->fields = uishell_sidebar_model_fields(arena, fields, view.field_count);
  node->details = uishell_sidebar_model_fields(arena, details, view.detail_count);
  node->controls = uishell_sidebar_model_controls(arena, controls, view.control_count);
  model->garbage += node->bytes;
  model->live -= node->bytes;
  node->bytes = arena_pos(arena)-start;
  model->live += node->bytes;
  node->revision = model->generation;
}

internal UIShell_SidebarModelSection *
uishell_sidebar_model_section(UIShell_SidebarModel *model, String8 name)
{
  for(U64 i = 0; i < model->section_count; i++)
  { if(str8_match(model->sections[i]->name, name, 0)) { return model->sections[i]; } }
  return 0;
}

internal void
uishell_sidebar_model_add_section(UIShell_SidebarModel *model, UIShell_SidebarModelSection *section)
{
  if(model->section_count == model->section_capacity)
  {
    U64 capacity = Max(8, model->section_capacity*2);
    UIShell_SidebarModelSection **sections = push_array(model->arena, UIShell_SidebarModelSection *, capacity);
    if(model->section_count) { MemoryCopy(sections, model->sections, model->section_count*sizeof(*sections)); }
    model->garbage += model->section_capacity*sizeof(*sections);
    model->sections = sections; model->section_capacity = capacity;
  }
  model->sections[model->section_count++] = section;
}

internal UIShell_SidebarModelNode **
uishell_sidebar_model_slot(UIShell_SidebarModelSection *section, String8 key)
{
  if(!section->slot_count) { return 0; }
  UIShell_SidebarModelNode **slot = &section->slots[u64_hash_from_str8(key)&(section->slot_count-1)];
  while(*slot && !str8_match(uishell_sidebar_string((*slot)->view.key), key, 0)) { slot = &(*slot)->hash_next; }
  return slot;
}

// A node by its section's name and its key; a section's own node by an
// empty key.
internal UIShell_SidebarModelNode *
uishell_sidebar_model_node(UIShell_SidebarModel *model, String8 section_name, String8 key)
{
  UIShell_SidebarModelSection *section = uishell_sidebar_model_section(model, section_name);
  if(!section) { return 0; }
  if(!key.size) { return section->node; }
  UIShell_SidebarModelNode **slot = uishell_sidebar_model_slot(section, key);
  return slot ? *slot : 0;
}

internal void
uishell_sidebar_model_insert(UIShell_SidebarModel *model, UIShell_SidebarModelSection *section, UIShell_SidebarModelNode *node)
{
  if(section->node_count+1 > section->slot_count/2)
  {
    U64 count = section->slot_count ? section->slot_count*2 : 16;
    UIShell_SidebarModelNode **slots = push_array(model->arena, UIShell_SidebarModelNode *, count);
    for(U64 i = 0; i < section->slot_count; i++)
    for(UIShell_SidebarModelNode *n = section->slots[i], *next = 0; n; n = next)
    {
      next = n->hash_next;
      UIShell_SidebarModelNode **slot = &slots[u64_hash_from_str8(uishell_sidebar_string(n->view.key))&(count-1)];
      n->hash_next = *slot; *slot = n;
    }
    model->garbage += section->slot_count*sizeof(*slots);
    model->live += (count-section->slot_count)*sizeof(*slots);
    section->slots = slots; section->slot_count = count;
  }
  UIShell_SidebarModelNode **slot = &section->slots[u64_hash_from_str8(uishell_sidebar_string(node->view.key))&(section->slot_count-1)];
  node->hash_next = *slot; *slot = node;
  section->node_count++;
}

internal void
uishell_sidebar_model_reserve(UIShell_SidebarModel *model, UIShell_SidebarModelList *list, U64 count)
{
  if(count <= list->capacity) { return; }
  U64 capacity = Max(count, Max(4, list->capacity*2));
  UIShell_SidebarModelNode **v = push_array(model->arena, UIShell_SidebarModelNode *, capacity);
  if(list->count) { MemoryCopy(v, list->v, list->count*sizeof(*v)); }
  model->garbage += list->capacity*sizeof(*v);
  model->live += (capacity-list->capacity)*sizeof(*v);
  list->v = v; list->capacity = capacity;
}

internal B32
uishell_sidebar_model_list_ops(UIShell_SidebarModel *model, const WheelhouseDelta *delta,
                               WheelhouseDeltaOp op, UIShell_SidebarModelSection *section,
                               UIShell_SidebarModelList *list, UIShell_SidebarModelNode *parent)
{
  for(U64 i = op.first; i < op.first+op.count; i++)
  {
    WheelhouseListOp l = {0};
    if(!wheelhouse_delta_list_op(delta, i, &l)) { return 0; }
    if(l.kind == WHEELHOUSE_LIST_INSERT)
    {
      UIShell_SidebarModelNode **slot = section ? uishell_sidebar_model_slot(section, uishell_sidebar_string(l.key)) : 0;
      if(!slot || !*slot || l.at > list->count) { return 0; }
      uishell_sidebar_model_reserve(model, list, list->count+1);
      MemoryCopy(list->v+l.at+1, list->v+l.at, (list->count-l.at)*sizeof(*list->v));
      list->v[l.at] = *slot;
      (*slot)->parent = parent;
      list->count++;
    }
    else if(l.kind == WHEELHOUSE_LIST_REMOVE)
    {
      if(l.at >= list->count) { return 0; }
      MemoryCopy(list->v+l.at, list->v+l.at+1, (list->count-l.at-1)*sizeof(*list->v));
      list->count--;
    }
    else if(l.kind == WHEELHOUSE_LIST_MOVE)
    {
      if(l.from >= list->count || l.to >= list->count) { return 0; }
      UIShell_SidebarModelNode *moved = list->v[l.from];
      if(l.from < l.to) { MemoryCopy(list->v+l.from, list->v+l.from+1, (l.to-l.from)*sizeof(*list->v)); }
      else { MemoryCopy(list->v+l.to+1, list->v+l.to, (l.from-l.to)*sizeof(*list->v)); }
      list->v[l.to] = moved;
    }
    else { return 0; }
  }
  model->preorder_dirty = 1;
  return 1;
}

// SECTIONS' ops are on the order of the sections as it was before.
internal B32
uishell_sidebar_model_order_ops(UIShell_SidebarModel *model, const WheelhouseDelta *delta, WheelhouseDeltaOp op)
{
  for(U64 i = op.first; i < op.first+op.count; i++)
  {
    WheelhouseListOp l = {0};
    if(!wheelhouse_delta_list_op(delta, i, &l)) { return 0; }
    UIShell_SidebarModelSection **v = model->order;
    if(l.kind == WHEELHOUSE_LIST_INSERT)
    {
      UIShell_SidebarModelSection *section = uishell_sidebar_model_section(model, uishell_sidebar_string(l.key));
      if(!section || l.at > model->order_count) { return 0; }
      if(model->order_count == model->order_capacity)
      {
        U64 capacity = Max(8, model->order_capacity*2);
        v = push_array(model->arena, UIShell_SidebarModelSection *, capacity);
        if(model->order_count) { MemoryCopy(v, model->order, model->order_count*sizeof(*v)); }
        model->garbage += model->order_capacity*sizeof(*v);
        model->order = v; model->order_capacity = capacity;
      }
      MemoryCopy(v+l.at+1, v+l.at, (model->order_count-l.at)*sizeof(*v));
      v[l.at] = section;
      model->order_count++;
    }
    else if(l.kind == WHEELHOUSE_LIST_REMOVE)
    {
      if(l.at >= model->order_count) { return 0; }
      MemoryCopy(v+l.at, v+l.at+1, (model->order_count-l.at-1)*sizeof(*v));
      model->order_count--;
    }
    else if(l.kind == WHEELHOUSE_LIST_MOVE)
    {
      if(l.from >= model->order_count || l.to >= model->order_count) { return 0; }
      UIShell_SidebarModelSection *moved = v[l.from];
      if(l.from < l.to) { MemoryCopy(v+l.from, v+l.from+1, (l.to-l.from)*sizeof(*v)); }
      else { MemoryCopy(v+l.to+1, v+l.to, (l.from-l.to)*sizeof(*v)); }
      v[l.to] = moved;
    }
    else { return 0; }
  }
  model->preorder_dirty = 1;
  return 1;
}

internal UIShell_SidebarModelNode *
uishell_sidebar_model_upsert(UIShell_SidebarModel *model, const WheelhouseDelta *delta, WheelhouseDeltaOp op)
{
  WheelhouseStreamNode view = {0};
  if(!wheelhouse_delta_node(delta, op.first, &view)) { return 0; }
  Temp scratch = scratch_begin(0, 0);
  WheelhouseField *fields = push_array(scratch.arena, WheelhouseField, view.field_count);
  WheelhouseField *details = push_array(scratch.arena, WheelhouseField, view.detail_count);
  WheelhouseControl *controls = push_array(scratch.arena, WheelhouseControl, view.control_count);
  B32 ok = 1;
  for(U64 i = 0; i < view.field_count; i++) { ok = wheelhouse_delta_field(delta, view.first_field+i, &fields[i]) && ok; }
  for(U64 i = 0; i < view.detail_count; i++) { ok = wheelhouse_delta_field(delta, view.first_detail+i, &details[i]) && ok; }
  for(U64 i = 0; i < view.control_count; i++) { ok = wheelhouse_delta_control(delta, view.first_control+i, &controls[i]) && ok; }
  UIShell_SidebarModelNode *node = 0;
  String8 name = uishell_sidebar_string(op.section);
  UIShell_SidebarModelSection *section = uishell_sidebar_model_section(model, name);
  if(ok && op.kind == WHEELHOUSE_OP_SECTION)
  {
    if(!section)
    {
      section = push_array(model->arena, UIShell_SidebarModelSection, 1);
      section->name = push_str8_copy(model->arena, name);
      section->node = push_array(model->arena, UIShell_SidebarModelNode, 1);
      section->node->section = section;
      uishell_sidebar_model_add_section(model, section);
      model->live += sizeof(*section)+sizeof(*section->node)+name.size;
    }
    node = section->node;
  }
  else if(ok && section)
  {
    UIShell_SidebarModelNode **slot = uishell_sidebar_model_slot(section, uishell_sidebar_string(view.key));
    node = slot ? *slot : 0;
    if(!node)
    {
      node = push_array(model->arena, UIShell_SidebarModelNode, 1);
      node->section = section;
      model->live += sizeof(*node);
      uishell_sidebar_model_set_content(model, node, view, fields, details, controls);
      uishell_sidebar_model_insert(model, section, node);
      scratch_end(scratch);
      return node;
    }
  }
  if(node) { uishell_sidebar_model_set_content(model, node, view, fields, details, controls); }
  scratch_end(scratch);
  return node;
}

internal void
uishell_sidebar_model_forget(UIShell_SidebarModel *model, UIShell_SidebarModelNode *node)
{
  model->garbage += node->bytes+node->children.capacity*sizeof(*node->children.v)+sizeof(*node);
  model->live -= node->bytes+node->children.capacity*sizeof(*node->children.v)+sizeof(*node);
  node->bytes = 0;
}

// Copy the live tree into a fresh arena and release the old one.
internal void
uishell_sidebar_model_copy_tree(UIShell_SidebarModel *model, UIShell_SidebarModelSection *section,
                                UIShell_SidebarModelNode *from, UIShell_SidebarModelNode *to)
{
  to->section = section;
  to->bytes = 0;
  U64 revision = from->revision;
  uishell_sidebar_model_set_content(model, to, from->view, from->fields, from->details, from->controls);
  to->revision = revision;
  to->children = (UIShell_SidebarModelList){0};
  uishell_sidebar_model_reserve(model, &to->children, from->children.count);
  for(U64 i = 0; i < from->children.count; i++)
  {
    UIShell_SidebarModelNode *child = push_array(model->arena, UIShell_SidebarModelNode, 1);
    model->live += sizeof(*child);
    uishell_sidebar_model_copy_tree(model, section, from->children.v[i], child);
    child->parent = to;
    to->children.v[to->children.count++] = child;
    uishell_sidebar_model_insert(model, section, child);
  }
}

internal void
uishell_sidebar_model_compact(UIShell_SidebarModel *model)
{
  UIShell_SidebarModel old = *model;
  model->arena = arena_alloc(.reserve_size = uishell_sidebar_model_arena_reserve);
  model->live = model->garbage = 0;
  model->sections = 0; model->section_count = model->section_capacity = 0;
  model->order = push_array(model->arena, UIShell_SidebarModelSection *, Max(8, old.order_count));
  model->order_capacity = Max(8, old.order_count);
  model->order_count = 0;
  // Between deltas every section is in the order.
  for(U64 s = 0; s < old.order_count; s++)
  {
    UIShell_SidebarModelSection *from = old.order[s];
    UIShell_SidebarModelSection *section = push_array(model->arena, UIShell_SidebarModelSection, 1);
    section->name = push_str8_copy(model->arena, from->name);
    section->node = push_array(model->arena, UIShell_SidebarModelNode, 1);
    model->live += sizeof(*section)+sizeof(*section->node)+from->name.size;
    uishell_sidebar_model_copy_tree(model, section, from->node, section->node);
    uishell_sidebar_model_add_section(model, section);
    model->order[model->order_count++] = section;
  }
  WheelhouseDisplayVariable *variables = push_array(model->arena, WheelhouseDisplayVariable, old.variable_count);
  for(U64 i = 0; i < old.variable_count; i++)
  {
    WheelhouseDisplayVariable v = old.variables[i];
    v.name = uishell_sidebar_model_text(model->arena, v.name);
    v.label = uishell_sidebar_model_text(model->arena, v.label);
    v.icon = uishell_sidebar_model_text(model->arena, v.icon);
    v.value = uishell_sidebar_model_text(model->arena, v.value);
    variables[i] = v;
  }
  model->variables = variables;
  model->diagnostics = push_array(model->arena, WheelhouseText, old.diagnostic_count);
  for(U64 i = 0; i < old.diagnostic_count; i++) { model->diagnostics[i] = uishell_sidebar_model_text(model->arena, old.diagnostics[i]); }
  model->errors = push_array(model->arena, WheelhouseActivationError, old.error_count);
  for(U64 i = 0; i < old.error_count; i++)
  {
    model->errors[i].entity = uishell_sidebar_model_entity(model->arena, old.errors[i].entity);
    model->errors[i].message = uishell_sidebar_model_text(model->arena, old.errors[i].message);
  }
  model->preorder = 0; model->preorder_count = 0;
  model->preorder_dirty = 1;
  model->compactions++;
  arena_release(old.arena);
}

internal void
uishell_sidebar_model_release(UIShell_SidebarModel *model)
{
  if(model->arena) { arena_release(model->arena); }
  MemoryZeroStruct(model);
}

// Apply a delta whole. 0 when it doesn't fit the model, which then holds
// part of it: start again from a new subscription's first delta.
internal B32
uishell_sidebar_model_apply(UIShell_SidebarModel *model, const WheelhouseDelta *delta)
{
  if(!model->arena) { model->arena = arena_alloc(.reserve_size = uishell_sidebar_model_arena_reserve); }
  model->generation = wheelhouse_delta_generation(delta);
  B32 ok = 1;
  for(U64 i = 0; ok && i < wheelhouse_delta_op_count(delta); i++)
  {
    WheelhouseDeltaOp op = {0};
    if(!wheelhouse_delta_op(delta, i, &op)) { ok = 0; break; }
    String8 name = uishell_sidebar_string(op.section);
    switch(op.kind)
    {
      case WHEELHOUSE_OP_SECTION:
      case WHEELHOUSE_OP_UPSERT:
      { ok = uishell_sidebar_model_upsert(model, delta, op) != 0; }break;
      case WHEELHOUSE_OP_REMOVE:
      {
        UIShell_SidebarModelSection *section = uishell_sidebar_model_section(model, name);
        UIShell_SidebarModelNode **slot = section ? uishell_sidebar_model_slot(section, uishell_sidebar_string(op.key)) : 0;
        if(!slot || !*slot) { ok = 0; break; }
        UIShell_SidebarModelNode *node = *slot;
        *slot = node->hash_next;
        section->node_count--;
        uishell_sidebar_model_forget(model, node);
      }break;
      case WHEELHOUSE_OP_REMOVE_SECTION:
      {
        UIShell_SidebarModelSection *section = uishell_sidebar_model_section(model, name);
        if(!section) { ok = 0; break; }
        section->removed = 1;
      }break;
      case WHEELHOUSE_OP_SECTIONS:
      { ok = uishell_sidebar_model_order_ops(model, delta, op); }break;
      case WHEELHOUSE_OP_CHILDREN:
      {
        UIShell_SidebarModelSection *section = uishell_sidebar_model_section(model, name);
        UIShell_SidebarModelNode *parent = op.key.len ? uishell_sidebar_model_node(model, name, uishell_sidebar_string(op.key)) : section ? section->node : 0;
        if(!parent) { ok = 0; break; }
        ok = uishell_sidebar_model_list_ops(model, delta, op, section, &parent->children, parent);
      }break;
      case WHEELHOUSE_OP_DISPLAY_VARIABLES:
      {
        model->garbage += model->variable_count*sizeof(*model->variables);
        model->variables = push_array(model->arena, WheelhouseDisplayVariable, op.count);
        model->variable_count = op.count;
        for(U64 j = 0; ok && j < op.count; j++)
        {
          WheelhouseDisplayVariable v = {0};
          ok = wheelhouse_delta_display_variable(delta, op.first+j, &v);
          v.name = uishell_sidebar_model_text(model->arena, v.name);
          v.label = uishell_sidebar_model_text(model->arena, v.label);
          v.icon = uishell_sidebar_model_text(model->arena, v.icon);
          v.value = uishell_sidebar_model_text(model->arena, v.value);
          model->variables[j] = v;
        }
      }break;
      case WHEELHOUSE_OP_DIAGNOSTICS:
      {
        model->garbage += model->diagnostic_count*sizeof(*model->diagnostics);
        model->diagnostics = push_array(model->arena, WheelhouseText, op.count);
        model->diagnostic_count = op.count;
        for(U64 j = 0; ok && j < op.count; j++)
        {
          WheelhouseText text = {0};
          ok = wheelhouse_delta_diagnostic(delta, op.first+j, &text);
          model->diagnostics[j] = uishell_sidebar_model_text(model->arena, text);
        }
      }break;
      case WHEELHOUSE_OP_ERRORS:
      {
        model->garbage += model->error_count*sizeof(*model->errors);
        model->errors = push_array(model->arena, WheelhouseActivationError, op.count);
        model->error_count = op.count;
        for(U64 j = 0; ok && j < op.count; j++)
        {
          WheelhouseActivationError e = {0};
          ok = wheelhouse_delta_error(delta, op.first+j, &e);
          model->errors[j].entity = uishell_sidebar_model_entity(model->arena, e.entity);
          model->errors[j].message = uishell_sidebar_model_text(model->arena, e.message);
        }
      }break;
      default: { ok = 0; }break;
    }
  }
  // Sections this delta removed go once their nodes have.
  for(U64 i = 0; i < model->section_count;)
  {
    UIShell_SidebarModelSection *section = model->sections[i];
    if(!section->removed) { i++; continue; }
    uishell_sidebar_model_forget(model, section->node);
    model->garbage += section->slot_count*sizeof(*section->slots);
    model->live -= section->slot_count*sizeof(*section->slots);
    MemoryCopy(model->sections+i, model->sections+i+1, (model->section_count-i-1)*sizeof(*model->sections));
    model->section_count--;
    model->preorder_dirty = 1;
  }
  if(ok && model->garbage > Max(model->live, uishell_sidebar_model_compact_floor)) { uishell_sidebar_model_compact(model); }
  else if(ok && !uishell_sidebar_model_compact_floor && model->garbage) { uishell_sidebar_model_compact(model); }
  return ok;
}

internal void
uishell_sidebar_model_visit(UIShell_SidebarModel *model, UIShell_SidebarModelNode *node)
{
  node->index = model->preorder_count;
  model->preorder[model->preorder_count++] = node;
  for(U64 i = 0; i < node->children.count; i++) { uishell_sidebar_model_visit(model, node->children.v[i]); }
}

internal U64
uishell_sidebar_model_count(UIShell_SidebarModelNode *node)
{
  U64 count = 1;
  for(U64 i = 0; i < node->children.count; i++) { count += uishell_sidebar_model_count(node->children.v[i]); }
  return count;
}

// Sections in order, each followed by its nodes depth first, as a snapshot
// lists them. Valid until the next apply.
internal UIShell_SidebarModelNode **
uishell_sidebar_model_preorder(UIShell_SidebarModel *model, U64 *count_out)
{
  if(model->preorder_dirty || !model->preorder)
  {
    U64 count = 0;
    for(U64 i = 0; i < model->order_count; i++) { count += uishell_sidebar_model_count(model->order[i]->node); }
    model->garbage += model->preorder ? model->preorder_count*sizeof(*model->preorder) : 0;
    model->preorder = push_array(model->arena, UIShell_SidebarModelNode *, Max(count, 1));
    model->preorder_count = 0;
    for(U64 i = 0; i < model->order_count; i++) { uishell_sidebar_model_visit(model, model->order[i]->node); }
    model->preorder_dirty = 0;
  }
  *count_out = model->preorder_count;
  return model->preorder;
}

internal WheelhouseDisplayVariable *
uishell_sidebar_model_variable(UIShell_SidebarModel *model, String8 name)
{
  for(U64 i = 0; i < model->variable_count; i++)
  { if(str8_match(uishell_sidebar_string(model->variables[i].name), name, 0)) { return &model->variables[i]; } }
  return 0;
}

// A node's control as drawn: a display-variable control takes its value,
// persistence, label and (when it has none of its own) glyph from its
// variable, as a snapshot's control does.
internal WheelhouseControl
uishell_sidebar_model_control(UIShell_SidebarModel *model, UIShell_SidebarModelNode *node, U64 index)
{
  WheelhouseControl control = node->controls[index];
  if(control.kind != WHEELHOUSE_CONTROL_DISPLAY_VARIABLE) { return control; }
  WheelhouseDisplayVariable *v = uishell_sidebar_model_variable(model, uishell_sidebar_string(control.variable));
  if(!v) { return control; }
  if(v->value_kind != WHEELHOUSE_VALUE_NONE)
  { control.value_kind = v->value_kind; control.checked = v->checked; control.value = v->value; }
  control.persist = v->persist;
  if(!control.glyph.len) { control.glyph = v->icon; }
  control.label = v->label;
  return control;
}
