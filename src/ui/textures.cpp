#include "ui/textures.h"

#include <cstring>

#include <SDL3/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

namespace gm {

SDL_Surface* surfaceFromFile(const std::string& path) {
    size_t size = 0;
    void* file = SDL_LoadFile(path.c_str(), &size);
    if (!file) return nullptr;
    int w = 0, h = 0, n = 0;
    unsigned char* pixels = stbi_load_from_memory(static_cast<const unsigned char*>(file), static_cast<int>(size), &w, &h, &n, 4);
    SDL_free(file);
    if (!pixels) return nullptr;
    SDL_Surface* surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (surface) std::memcpy(surface->pixels, pixels, static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return surface;
}

}  // namespace gm
