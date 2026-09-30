#pragma once

#include <string>

struct SDL_Surface;

namespace gm {

// Decodes a PNG/JPEG file into an SDL_Surface the caller owns (SDL_DestroySurface); nullptr if it cannot.
// Used for the window icon, which SDL needs as a surface rather than a renderer-bound texture.
SDL_Surface* surfaceFromFile(const std::string& path);

}  // namespace gm
