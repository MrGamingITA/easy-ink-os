// Accesso dell'interprete dei temi (ui/theme_draw) alle funzioni di disegno di screens.cpp, che restano private lì.
#pragma once
#include "core/model.h"
#include "gfx/canvas.h"
#include "gfx/text.h"

namespace ui {
namespace kit {

// T() delle schermate di serie: arrotonda x e y, accorcia con fitText se max > 0, passa dal TextHook dei test.
void text(Surface &c, const char *s, double x, double y, int size, int weight, bool pix, Align a, bool ink, int max);
// avatarS() + accOverlay(): con ink falso inchiostro e carta si scambiano (anche nei punti dell'accessorio).
void mascot(Surface &c, const cal::Model &m, const cal::Session *s, int ox, int oy, int sc, cal::MascotPose p, int f,
            bool accessory, bool ink);

}  // namespace kit
}  // namespace ui
