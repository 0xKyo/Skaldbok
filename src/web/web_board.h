// The master's boards (the Master tab). Sessions belong to a campaign (a name, and the characters that play it); a campaign has many sessions. The Screen is a canvas where the GM pins things from anywhere (a rule, a table, an NPC, a picture...), writes
// notes and links them with arrows; each Session is another canvas of the same kind, kept apart, that is written while a game is played. A board is
// one JSON document: {"items": [...], "arrows": [...], "view": {x, y, zoom}}. The server only stores them in the GM's folder (never in a pack), after
// checking they are the right shape and not huge. A session has a name, the moment it was started and the moment it was ended ("" while it is the
// active one: only one session is active at a time); an ended session cannot be changed until it is opened again.
#pragma once

#include <string>
#include <string_view>

#include "parsing/jsonutil.h"

namespace gm {

struct BoardResult {
    int status = 200;
    std::string error;
    json body;                 // what to answer, when there is something
};

// ---- the Screen ----
// The board as it was saved; an empty board when there is none.
json boardLoad(const std::string& prefsDir);
// Saves the board. Only items, arrows and view are kept; a board with too many things, or too big, is refused.
BoardResult boardSave(const std::string& prefsDir, const json& body);

// ---- the campaigns ----
// {"campaigns": [{id, name, created, characters: [ids], sessions}]}, newest first. Sessions made before there were campaigns are put in one called "Campaign 1".
json campaignList(const std::string& prefsDir);
BoardResult campaignCreate(const std::string& prefsDir, const std::string& name, const std::string& now);
// Changes the name and / or the characters of a campaign ({name?, characters?: [ids]}).
BoardResult campaignSave(const std::string& prefsDir, const std::string& id, const json& body);
// Deletes a campaign and all its sessions.
BoardResult campaignDelete(const std::string& prefsDir, const std::string& id);

// ---- the sessions ----
// {"sessions": [{id, campaign, name, created, ended, items}], "active": id or ""}, newest first.
json sessionList(const std::string& prefsDir);
// One session with its board: {id, campaign, name, created, ended, reopened, board}. Null when there is none.
json sessionGet(const std::string& prefsDir, const std::string& id);
// Starts a session of a campaign: the one that was active (if any, in any campaign) is ended first. `now` is the date-time to write down (ISO, UTC).
BoardResult sessionCreate(const std::string& prefsDir, const std::string& campaign, const std::string& name, const std::string& now);
// Changes the name and / or the board of a session ({name?, board?}); the board of an ended session is refused (409).
BoardResult sessionSave(const std::string& prefsDir, const std::string& id, const json& body);
BoardResult sessionEnd(const std::string& prefsDir, const std::string& id, const std::string& now);
// Makes an ended session the active one again (the active one, if any, is ended first).
BoardResult sessionOpen(const std::string& prefsDir, const std::string& id, const std::string& now);

// Deletes a session with everything on it (the active one too: then there is none active). The pictures the GM brought stay in their folder.
BoardResult sessionDelete(const std::string& prefsDir, const std::string& id);

// ---- the pictures of the boards (pasted or dropped by the GM) ----
// Keeps a picture (a PNG, JPEG, GIF or WebP file, 8 MB at most) and says its id in `result.body["id"]`.
BoardResult boardImageSave(const std::string& prefsDir, std::string_view bytes);
// The file of a picture; "" when there is none (the id comes from the request: only the ones this server made are served).
std::string boardImagePath(const std::string& prefsDir, const std::string& id);

}  // namespace gm
