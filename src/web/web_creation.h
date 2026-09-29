// The character creator for the GM's web page. The steps and their rules are the app's (game/creation.h); this only hands the
// browser what each step offers, and turns the choices it sends back into a checked Creation and a sheet.
#pragma once

#include <string>

#include "game/creation.h"
#include "parsing/jsonutil.h"

namespace gm {

// Everything the wizard offers: kin, professions (with their gear sets parsed), ages, skills, spells, and the book's tables laid out
// face by face so the browser can roll them.
json creationCatalog(const ContentStore& content);

// The choices as the browser sends them. Nothing is trusted: what does not exist is left empty and validateCreation says so.
Creation creationFromJson(const json& body, const ContentStore& content);

// The same choices in the shape the browser sends them (creationFromJson reads it back): what "Random" hands the wizard to fill in.
json creationToJson(const Creation& c);

// What the review step shows: what is missing, the derived numbers and the sheet as plain text.
json creationPreview(const Creation& c, const ContentStore& content, Dice& dice);

}  // namespace gm
