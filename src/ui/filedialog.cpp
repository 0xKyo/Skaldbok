#include "ui/filedialog.h"

#include <SDL3/SDL.h>

namespace gm {

void FileDialog::onResult(void* self, const char* const* files, int) {
    FileDialog* d = static_cast<FileDialog*>(self);
    std::lock_guard<std::mutex> lock(d->mutex_);
    d->result_ = (files && files[0]) ? files[0] : "";
    d->ready_ = true;
}

void FileDialog::openFile(SDL_Window* window, const char* filterName, const char* patterns) {
    if (busy_) return;
    busy_ = true;
    filterName_ = filterName ? filterName : "";
    patterns_ = patterns ? patterns : "";
    const bool filtered = !patterns_.empty();
    filter_ = {filterName_.c_str(), patterns_.c_str()};            // kept as members: the dialog reads them until it closes
    SDL_ShowOpenFileDialog(&FileDialog::onResult, this, window, filtered ? &filter_ : nullptr, filtered ? 1 : 0, nullptr, false);
}

void FileDialog::openFolder(SDL_Window* window) {
    if (busy_) return;
    busy_ = true;
    SDL_ShowOpenFolderDialog(&FileDialog::onResult, this, window, nullptr, false);
}

void FileDialog::saveFile(SDL_Window* window, const char* filterName, const char* patterns, const std::string& defaultName) {
    if (busy_) return;
    busy_ = true;
    filterName_ = filterName ? filterName : "";
    patterns_ = patterns ? patterns : "";
    defaultName_ = defaultName;
    const bool filtered = !patterns_.empty();
    filter_ = {filterName_.c_str(), patterns_.c_str()};
    SDL_ShowSaveFileDialog(&FileDialog::onResult, this, window, filtered ? &filter_ : nullptr, filtered ? 1 : 0,
                           defaultName_.empty() ? nullptr : defaultName_.c_str());
}

bool FileDialog::poll(std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_) return false;
    path = result_;
    ready_ = false;
    busy_ = false;
    return true;
}

}  // namespace gm
