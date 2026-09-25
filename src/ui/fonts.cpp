#include "ui/fonts.h"

#include <initializer_list>

#include <SDL3/SDL.h>
#include <imgui.h>

namespace gm {
namespace {

bool exists(const char* path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

const char* firstExisting(std::initializer_list<const char*> candidates) {
    for (const char* c : candidates)
        if (exists(c)) return c;
    return nullptr;
}

}  // namespace

// The books use typographic characters (– — ’ “ ” × ✦ ≤) that ImGui's built-in font lacks. Use a system
// font when there is one; the app stays fully usable (with '?' glyphs) if none is found.
void setupFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* text = firstExisting({
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/Library/Fonts/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    });
    if (!text) {
        io.Fonts->AddFontDefault();
        return;
    }
    io.Fonts->AddFontFromFileTTF(text);
    // dingbats (✦) come from a symbol font on Windows; DejaVu already has them
    const char* symbols = firstExisting({"C:\\Windows\\Fonts\\seguisym.ttf"});
    if (symbols) {
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(symbols, 0.0f, &merge);
    }
}

}  // namespace gm
