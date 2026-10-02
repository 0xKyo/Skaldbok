// A backup of the GM's work, to move it to another computer: every file of the GM's folder (characters, chat, campaigns, sessions, the boards and their
// pictures, adventure notes, homebrew...) in ONE JSON document: {"format": "skaldbok-backup", "version": 1, "created": ISO, "files": {"characters/x.json": <base64>}}.
// What belongs to the computer (settings, the web links and tokens) stays out; the other computer makes its own. Importing REPLACES what the folder had.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "web/web_board.h"

namespace gm {

std::string base64Encode(std::string_view bytes);
std::optional<std::string> base64Decode(std::string_view text);       // nullopt when it is not base64 (padding and line breaks are fine)

// The whole backup of the folder.
json backupExport(const std::string& prefsDir, const std::string& now);
// Checks the whole document first (nothing is touched when anything is wrong: 400), then removes what the folder had and writes the backup's files.
// `body["files"]` is the number of files written.
BoardResult backupImport(const std::string& prefsDir, const json& backup);

}  // namespace gm
