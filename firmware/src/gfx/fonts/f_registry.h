// Generato da tools/fonts/gen.py: non modificare a mano.
// Registro dei font: (size, weight, pix) -> dati. Ordinato per pix, size, weight.
#pragma once
#include "font_types.h"
#include "f_kern.h"
#include "f_ui500_10.h"
#include "f_ui600_10.h"
#include "f_ui700_10.h"
#include "f_ui500_11.h"
#include "f_ui600_11.h"
#include "f_ui500_12.h"
#include "f_ui600_12.h"
#include "f_ui700_12.h"
#include "f_ui600_13.h"
#include "f_ui700_13.h"
#include "f_ui700_14.h"
#include "f_ui700_15.h"
#include "f_ui700_16.h"
#include "f_ui700_18.h"
#include "f_ui700_20.h"
#include "f_ui700_22.h"
#include "f_ui700_26.h"
#include "f_ui700_40.h"
#include "f_ui700_44.h"
#include "f_ui700_46.h"
#include "f_pix400_8.h"
#include "f_pix700_8.h"

namespace fonts {

static const Font kFonts[] = {
  {10, 500, false, false, 136, ui500_10_entries, ui500_10_glyphs, ui500_10_bitmap, &kern_500},
  {10, 600, false, true, 136, ui600_10_entries, ui600_10_glyphs, ui600_10_bitmap, &kern_600},
  {10, 700, false, true, 136, ui700_10_entries, ui700_10_glyphs, ui700_10_bitmap, &kern_700},
  {11, 500, false, false, 136, ui500_11_entries, ui500_11_glyphs, ui500_11_bitmap, &kern_500},
  {11, 600, false, true, 136, ui600_11_entries, ui600_11_glyphs, ui600_11_bitmap, &kern_600},
  {12, 500, false, false, 136, ui500_12_entries, ui500_12_glyphs, ui500_12_bitmap, &kern_500},
  {12, 600, false, false, 136, ui600_12_entries, ui600_12_glyphs, ui600_12_bitmap, &kern_600},
  {12, 700, false, true, 136, ui700_12_entries, ui700_12_glyphs, ui700_12_bitmap, &kern_700},
  {13, 600, false, false, 136, ui600_13_entries, ui600_13_glyphs, ui600_13_bitmap, &kern_600},
  {13, 700, false, true, 136, ui700_13_entries, ui700_13_glyphs, ui700_13_bitmap, &kern_700},
  {14, 700, false, true, 136, ui700_14_entries, ui700_14_glyphs, ui700_14_bitmap, &kern_700},
  {15, 700, false, false, 136, ui700_15_entries, ui700_15_glyphs, ui700_15_bitmap, &kern_700},
  {16, 700, false, true, 136, ui700_16_entries, ui700_16_glyphs, ui700_16_bitmap, &kern_700},
  {18, 700, false, false, 136, ui700_18_entries, ui700_18_glyphs, ui700_18_bitmap, &kern_700},
  {20, 700, false, false, 136, ui700_20_entries, ui700_20_glyphs, ui700_20_bitmap, &kern_700},
  {22, 700, false, true, 136, ui700_22_entries, ui700_22_glyphs, ui700_22_bitmap, &kern_700},
  {26, 700, false, false, 48, ui700_26_entries, ui700_26_glyphs, ui700_26_bitmap, &kern_700},
  {40, 700, false, false, 48, ui700_40_entries, ui700_40_glyphs, ui700_40_bitmap, &kern_700},
  {44, 700, false, false, 48, ui700_44_entries, ui700_44_glyphs, ui700_44_bitmap, &kern_700},
  {46, 700, false, true, 48, ui700_46_entries, ui700_46_glyphs, ui700_46_bitmap, &kern_700},
  {8, 400, true, true, 134, pix400_8_entries, pix400_8_glyphs, pix400_8_bitmap, nullptr},
  {8, 700, true, true, 134, pix700_8_entries, pix700_8_glyphs, pix700_8_bitmap, nullptr},
};
static const int kFontCount = sizeof(kFonts) / sizeof(kFonts[0]);

}  // namespace fonts
