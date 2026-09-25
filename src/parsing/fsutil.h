// Small file helpers shared by every part of the app that reads or writes the user's files (SDL underneath, so paths are UTF-8).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gm::fs {

std::optional<std::string> readFile(const std::string& path);
// Writes the whole file through a temporary neighbour and a rename, so a reader (the web server) never sees half a file.
bool writeFile(const std::string& path, std::string_view data);
bool isFile(const std::string& path);
std::vector<std::string> listDir(const std::string& dir);          // names only, no "." or ".."
std::string withoutTrailingSlash(std::string path);                 // "dir/" and "dir\" become "dir" (a lone "/" stays)
// A new id that sorts by time (in order, for one program, even within a millisecond) and cannot collide between programs:
// "<prefix>-<12 hex digits of ms>-<4 of sequence>-<4 random>".
std::string stampedId(std::string_view prefix);
// Ids that end up in file names: letters, digits, '-' and '_' only (no path tricks).
bool safeId(std::string_view id);

}  // namespace gm::fs

namespace gm {

std::string nowIso();                                                 // 2026-09-20T14:03:09Z (UTC)

}  // namespace gm
