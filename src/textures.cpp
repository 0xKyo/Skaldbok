#include "textures.h"

#include <SDL3/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

namespace gm {

SDL_Texture* textureFromMemory(SDL_Renderer* renderer, const unsigned char* data, size_t size, int* width, int* height) {
    int w = 0, h = 0, n = 0;
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &n, 4);
    if (!pixels) return nullptr;
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (tex) {
        SDL_UpdateTexture(tex, nullptr, pixels, w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);       // the alpha channel is what makes an icon transparent
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        if (width) *width = w;
        if (height) *height = h;
    }
    stbi_image_free(pixels);
    return tex;
}

const TextureCache::Tex* TextureCache::get(const std::string& path) {
    if (auto it = slots_.find(path); it != slots_.end()) {
        order_.splice(order_.begin(), order_, it->second.lru);
        return &it->second.tex;
    }
    if (failed_.count(path)) return nullptr;

    size_t size = 0;
    void* file = SDL_LoadFile(path.c_str(), &size);
    if (!file) {
        failed_[path] = true;
        return nullptr;
    }
    int w = 0, h = 0, n = 0;
    unsigned char* pixels =
        stbi_load_from_memory(static_cast<const unsigned char*>(file), static_cast<int>(size), &w, &h, &n, 4);
    SDL_free(file);
    if (!pixels) {
        failed_[path] = true;
        return nullptr;
    }
    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (tex) {
        SDL_UpdateTexture(tex, nullptr, pixels, w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    }
    stbi_image_free(pixels);
    if (!tex) {
        failed_[path] = true;
        return nullptr;
    }

    while (slots_.size() >= capacity_ && !order_.empty()) {      // evict the least recently used
        const std::string victim = order_.back();
        order_.pop_back();
        SDL_DestroyTexture(slots_[victim].tex.tex);
        slots_.erase(victim);
    }
    order_.push_front(path);
    Slot slot;
    slot.tex = {tex, w, h};
    slot.lru = order_.begin();
    return &(slots_[path] = slot).tex;
}

void TextureCache::clear() {
    for (auto& [_, s] : slots_) SDL_DestroyTexture(s.tex.tex);
    slots_.clear();
    order_.clear();
}

}  // namespace gm
