// Drawing pieces of the settings screen: the palette and a few helpers.
#pragma once

#include <string>

#include <imgui.h>

namespace gm::ui {

// The sheet's palette: ink, dragon green, faded ink, label brown, logo red.
extern const ImVec4 kInk, kAccent, kAccentDim, kGrey, kGold, kRed;

float U(float v);                                   // pixel sizes written for the default 16px font, scaled with zoom and DPI
void bigText(const char* text, float scale, ImVec4 color);
std::string lowered(std::string s);

}  // namespace gm::ui
