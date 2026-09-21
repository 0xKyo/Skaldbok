// The system's open/save/folder dialogs, made safe to use from a frame loop.
// SDL may call the result back from another thread, so the answer is parked here and collected with poll().
#pragma once

#include <mutex>
#include <string>

#include <SDL3/SDL_dialog.h>

struct SDL_Window;

namespace gm {

class FileDialog {
public:
    // `patterns` is a ';' separated list of extensions without dots ("png;jpg"), or empty for any file.
    void openFile(SDL_Window* window, const char* filterName, const char* patterns);
    void openFolder(SDL_Window* window);
    void saveFile(SDL_Window* window, const char* filterName, const char* patterns, const std::string& defaultName);

    bool busy() const { return busy_; }
    // True once, on the frame after the user chose something. `path` is empty if the dialog was cancelled.
    bool poll(std::string& path);

private:
    static void onResult(void* self, const char* const* files, int filter);
    std::mutex mutex_;
    std::string result_;
    std::string filterName_, patterns_, defaultName_;
    SDL_DialogFileFilter filter_{};
    bool ready_ = false;
    bool busy_ = false;
};

}  // namespace gm
