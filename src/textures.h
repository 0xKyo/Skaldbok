#pragma once

#include <list>
#include <string>
#include <unordered_map>

struct SDL_Renderer;
struct SDL_Texture;

namespace gm {

// Decodes a PNG/JPEG held in memory (an icon compiled into the app) into a texture the caller owns; nullptr if it cannot.
SDL_Texture* textureFromMemory(SDL_Renderer* renderer, const unsigned char* data, size_t size, int* width = nullptr, int* height = nullptr);

// Loads JPEG/PNG files into SDL textures on demand and keeps only a few of them alive
// (creature art is ~1 MB decoded each; the cache keeps memory small on modest machines).
class TextureCache {
public:
    struct Tex {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
    };

    TextureCache(SDL_Renderer* renderer, size_t capacity = 12) : renderer_(renderer), capacity_(capacity) {}
    ~TextureCache() { clear(); }
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // nullptr if the file is missing or cannot be decoded (the failure is remembered).
    const Tex* get(const std::string& absolutePath);
    void clear();

private:
    struct Slot {
        Tex tex;
        std::list<std::string>::iterator lru;
    };
    SDL_Renderer* renderer_;
    size_t capacity_;
    std::unordered_map<std::string, Slot> slots_;
    std::list<std::string> order_;              // most recently used first
    std::unordered_map<std::string, bool> failed_;
};

}  // namespace gm
