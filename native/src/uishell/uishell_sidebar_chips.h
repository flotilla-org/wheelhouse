#ifndef UISHELL_SIDEBAR_CHIPS_H
#define UISHELL_SIDEBAR_CHIPS_H
#include <stddef.h>

typedef struct UIShell_ChipMeasure {
  float width;
  int attention;
  int folded;
  int unopened_workspace;
} UIShell_ChipMeasure;

typedef struct UIShell_ChipLayout {
  size_t tier;
  size_t folded;
  float name_width; // Resolved name budget, retained for diagnostic tests.
  float chip_width;
  float content_width; // Unclipped content budget, retained for diagnostic tests.
} UIShell_ChipLayout;

// Width excludes the kind/disclosure and fixed trailing status slots. Names
// give way first; unopened workspace actions fold before quiet subjects.
// Within each class, fold from the end without reordering surviving chips.
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
  for(int priority = 1; priority >= 0; priority--)
  for(size_t i = count; i > 0 && name+total > width; i--)
  {
    if(chips[i-1].attention || chips[i-1].unopened_workspace != priority) { continue; }
    chips[i-1].folded = 1;
    total -= chips[i-1].width;
    if(result.folded++ == 0) { total += overflow_width; }
  }
  // If attention alone exceeds the viewport, retain it in a horizontally
  // scrollable chip area. It must not displace the trailing status slot.
  // At widths below the normal name floor, give the chip viewport room to
  // scroll. Retaining a name budget here previously made it zero pixels wide.
  if(width <= name && total > 0) { name = 0; }
  float available = width > name ? width-name : 0;
  if(total > available && available < overflow_width)
  { available = width < overflow_width ? width : overflow_width; }
  result.chip_width = total < available ? total : available;
  result.content_width = total;
  result.name_width = width > result.chip_width ? width-result.chip_width : 0;
  return result;
}
#endif
