// Installed content packs: where they live, importing a homebrew folder or .zip, removing one.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "content.h"

namespace gm {

struct ImportResult {
    bool ok = false;
    bool replaced = false;               // a pack with the same id was already installed and has been updated
    std::string packId, packName;
    std::string message;                 // one line for the user, either what went wrong or what was installed
    std::vector<std::string> warnings;   // entries of the pack that were skipped or adjusted
    int entries = 0;
};

class PackManager {
public:
    PackManager(std::string coreDir, std::string userDir);

    const std::string& coreDir() const { return coreDir_; }
    const std::string& userDir() const { return userDir_; }

    // Core first, then every installed homebrew pack (folder name = pack id). Ids in `disabled` are not loaded.
    std::vector<PackSpec> specs(const std::set<std::string>& disabled) const;

    // Installs the pack found at `path`: a folder with a manifest.json (or a folder holding exactly one such
    // folder) or a .zip of one. The pack is checked first; nothing is installed if it does not load.
    ImportResult import(const std::string& path, Database& db);

    bool remove(const std::string& packId, std::string* error);

private:
    std::string coreDir_, userDir_;
};

// Helpers shared with tests.
bool removeTree(const std::string& dir);
bool isSafeArchivePath(const std::string& name);

}  // namespace gm
