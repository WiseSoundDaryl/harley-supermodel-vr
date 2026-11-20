#ifndef INCLUDED_DESKTOP_EYE_LAYOUT_H
#define INCLUDED_DESKTOP_EYE_LAYOUT_H

#include <algorithm>

namespace New3D {

// Pure presentation geometry. The native UI rectangle keeps Model 3's aspect;
// only the world rectangle expands horizontally for a wider destination eye.
// The scratch framebuffer remains the full host size for existing render passes.
struct DesktopEyeLayout
{
  struct Rect { int x = 0, y = 0, width = 0, height = 0; } world, nativeUI;

  static DesktopEyeLayout Calculate(int targetWidth, int targetHeight, bool wide, bool mono)
  {
    DesktopEyeLayout layout;
    if (targetWidth < 2 || targetHeight < 1) return layout;
    // Keep the old float/truncation path exactly when wide-screen is off.
    const float scale = std::min(targetWidth / 496.0f, targetHeight / 384.0f);
    layout.nativeUI.width = std::max(1, int(496.0f * scale));
    layout.nativeUI.height = std::max(1, int(384.0f * scale));
    layout.nativeUI.x = (targetWidth - layout.nativeUI.width) / 2;
    layout.nativeUI.y = (targetHeight - layout.nativeUI.height) / 2;
    layout.world = layout.nativeUI;
    const int displayWidth = mono ? targetWidth : targetWidth / 2;
    if (wide && double(displayWidth) / targetHeight > 496.0 / 384.0) {
      layout.world.width = std::min(targetWidth,
        std::max(layout.nativeUI.width, int(double(layout.nativeUI.height) * displayWidth / targetHeight)));
      layout.world.x = (targetWidth - layout.world.width) / 2;
    }
    return layout;
  }

  float HorizontalExpansion() const
  {
    return nativeUI.width > 0 ? float(world.width) / nativeUI.width : 1.0f;
  }

  // Add equally to both sides so the original frustum center and every vertical
  // slope survive. This also handles asymmetric native world projections.
  static void ExpandHorizontal(float expansion, float& left, float& right)
  {
    if (expansion <= 1.0f) return;
    const float extra = (right - left) * (expansion - 1.0f) * 0.5f;
    left -= extra;
    right += extra;
  }
};

} // namespace New3D

#endif
