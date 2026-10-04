#ifndef UISHELL_SIDEBAR_CHIPS_H
#define UISHELL_SIDEBAR_CHIPS_H
#include <stddef.h>

typedef struct UIShell_ChipMeasure {
  float width;
  int attention;
  int folded;
} UIShell_ChipMeasure;

typedef struct UIShell_ChipLayout {
  size_t tier;
  size_t folded;
  float name_width; // Resolved name budget, retained for diagnostic tests.
  float chip_width;
  float content_width; // Unclipped content budget, retained for diagnostic tests.
} UIShell_ChipLayout;

// Width excludes the kind/disclosure and fixed trailing status slots. Names
// give way first; only quiet chips fold, from the end of catalog order.
static UIShell_ChipLayout
uishell_chip_layout(float width, const float names[3], float minimum_name,
                    UIShell_ChipMeasure *chips, size_t count, float overflow_width)
{
  UIShell_ChipLayout result = {0};
  float total = 0;
  for(size_t i = 0; i < count; i++) { chips[i].folded = 0; total += chips[i].width; }
  while(result.tier < 2 && names[result.tier]+total > width) { result.tier++; }
  float name = names[result.tier];
  if(name+total > width) { name = minimum_name < name ? minimum_name : name; }
  for(size_t i = count; i > 0 && name+total > width; i--)
  {
    if(chips[i-1].attention) { continue; }
    chips[i-1].folded = 1;
    total -= chips[i-1].width;
    if(result.folded++ == 0) { total += overflow_width; }
  }
  // If attention alone exceeds the viewport, retain it in a horizontally
  // scrollable chip area. It must not displace the trailing status slot.
  float available = width > name ? width-name : 0;
  result.chip_width = total < available ? total : available;
  result.content_width = total;
  result.name_width = width > result.chip_width ? width-result.chip_width : 0;
  return result;
}
#endif
