// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef UI_SCROLL_REGION_H
#define UI_SCROLL_REGION_H

// Always reserves a classic gutter even without overflow. Use it for content
// whose extent depends on viewport width (wrapped text, terminal grids).
// Auto is for an independently measured content extent, in pixels.
typedef enum UI_ScrollAxisPolicy
{
  UI_ScrollAxisPolicy_Off,
  UI_ScrollAxisPolicy_Always,
  UI_ScrollAxisPolicy_Auto,
} UI_ScrollAxisPolicy;

typedef struct UI_ScrollRegionParams UI_ScrollRegionParams;
struct UI_ScrollRegionParams
{
  Rng2F32 rect; // available rectangle, local to the parent
  UI_ScrollBarStyle style;
  UI_ScrollAxisPolicy axis[Axis2_COUNT];
  Vec2F32 content_dim_px; // used only by Auto axes; not scroll-position units
  F32 gutter_px;
  F32 overlay_rest_px;
  F32 overlay_hover_px;
  F32 overlay_inset_px;
  // Overlay bars keep their look but reserve their strip and stay shown,
  // where the system wants scroll bars always shown.
  B32 overlay_reserve;
  // The content box takes the whole rect, and reserved gutters lie over its
  // edges: for content that keeps clear of them itself where it scrolls, and
  // runs to the edge where it doesn't (a table's headers beside a bar kept
  // to its span).
  B32 content_under_gutters;
};

typedef struct UI_ScrollRegion UI_ScrollRegion;
struct UI_ScrollRegion
{
  UI_ScrollRegionParams params;
  Rng2F32 viewport; // what's left beside the reserved gutters
  Rng2F32 content;  // the content box: the viewport, or the rect if content_under_gutters
  B32 bar_enabled[Axis2_COUNT];
};

typedef struct UI_ScrollRegionAxis UI_ScrollRegionAxis;
struct UI_ScrollRegionAxis
{
  UI_ScrollPt position;
  Rng1S64 range; // inclusive legal positions, NOT the content's index range
  S64 visible;  // visible extent, in the same units as range and position
};

typedef struct UI_ScrollRegionSignal UI_ScrollRegionSignal;
struct UI_ScrollRegionSignal
{
  UI_Box *content_box;
  UI_ScrollPt2 position; // requested positions; content owns their application
};

// An axis in no units, for content that scrolls by something other than a
// UI_ScrollPt: its thumb; `activity`, any value that changes when the content
// scrolls (an overlay bar shows for a while after it does); what a drag
// begins from (see ui_scroll_bar_styled); and `span`, the part of the
// viewport along the axis that scrolls (from its start; empty: all of it),
// which the bar keeps to, clear of content that stays put.
typedef struct UI_ScrollRegionThumbAxis UI_ScrollRegionThumbAxis;
struct UI_ScrollRegionThumbAxis
{
  UI_ScrollThumb thumb;
  F32 activity;
  void *drag_origin;
  U64 drag_origin_size;
  Rng1F32 span;
};

typedef struct UI_ScrollRegionThumbSignal UI_ScrollRegionThumbSignal;
struct UI_ScrollRegionThumbSignal
{
  UI_Box *content_box;
  UI_ScrollBarSignal bar[Axis2_COUNT]; // content owns their application
};

// Defaults read UI style/font metrics, never shell settings. Layout itself is
// pure, so content can measure against viewport before building the region.
internal UI_ScrollRegionParams ui_scroll_region_params(Rng2F32 rect, UI_ScrollAxisPolicy x, UI_ScrollAxisPolicy y);
internal UI_ScrollRegion ui_scroll_region_layout(UI_ScrollRegionParams params);
internal Rng2F32 ui_scroll_region_bar_rect(UI_ScrollRegion *region, Axis2 axis, F32 thickness, B32 other_bar_visible);
// Creates bars and a clipped content box together, in the required paint/hit
// order. key is the stable content identity. Does not consume wheel/keyboard
// input or change content offsets. Callers signal content_box themselves.
internal UI_ScrollRegionSignal ui_scroll_region_build(UI_Box *parent, UI_Key key, UI_ScrollRegion *region,
                                                      UI_ScrollRegionAxis axes[Axis2_COUNT], UI_BoxFlags content_flags);
// The same over axes in no units; ui_scroll_region_build is this over
// UI_ScrollPts.
internal UI_ScrollRegionThumbSignal ui_scroll_region_build_thumbs(UI_Box *parent, UI_Key key, UI_ScrollRegion *region,
                                                                  UI_ScrollRegionThumbAxis axes[Axis2_COUNT], UI_BoxFlags content_flags);

#endif
