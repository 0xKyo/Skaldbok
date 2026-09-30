#include "ui/ui_common.h"

#include "parsing/fts.h"

namespace gm::ui {

// Palette: parchment-and-teal, echoing the books.
const ImVec4 kInk = ImVec4(0.200f, 0.161f, 0.122f, 1.0f);              // dark brown ink
const ImVec4 kAccent = ImVec4(0.137f, 0.412f, 0.353f, 1.0f);       // dragon green, for text on paper
const ImVec4 kAccentDim = ImVec4(0.184f, 0.478f, 0.408f, 1.0f);
const ImVec4 kGrey = ImVec4(0.490f, 0.424f, 0.329f, 1.0f);           // faded ink
const ImVec4 kGold = ImVec4(0.478f, 0.294f, 0.165f, 1.0f);           // the brown of the sheet's small labels
const ImVec4 kRed = ImVec4(0.745f, 0.180f, 0.149f, 1.0f);

float U(float v) { return v * ImGui::GetFontSize() / 16.0f; }

void bigText(const char* text, float scale, ImVec4 color) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

std::string lowered(std::string s) { return lowerCopy(std::move(s)); }

}  // namespace gm::ui
