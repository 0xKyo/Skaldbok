// The character creator for the GM's web page. The steps and their rules are the app's (game/creation.h); this only hands the
// browser what each step offers, and turns the choices it sends back into a checked Creation and a sheet.
//
// `content` is the content in English: the choices go back and forth by its keys and English names, which is what the sheet keeps.
// `shown` is the same in the language of the page: what the choices are shown as (the same store for English).
#pragma once

#include <string>

#include "game/creation.h"
#include "parsing/jsonutil.h"

namespace gm {

// Everything the wizard offers: kin, professions (with their gear sets parsed), ages, skills, spells, and the book's tables laid out
// face by face so the browser can roll them. "labels" says how each skill, ability and school is shown.
json creationCatalog(const ContentStore& content, const ContentStore& shown);

// The choices as the browser sends them. Nothing is trusted: what does not exist is left empty and validateCreation says so.
Creation creationFromJson(const json& body, const ContentStore& content);

// The same choices in the shape the browser sends them (creationFromJson reads it back): what "Random" hands the wizard to fill in.
json creationToJson(const Creation& c);

// The texts a random creation rolled on the English tables (weakness, memento, appearance, name, nickname), as the same rows read in
// the page's language: they are the character's own words from then on.
void localizeCreation(Creation& c, const ContentStore& content, const ContentStore& shown);

// What the review step shows: what is missing, the derived numbers and the sheet as plain text.
json creationPreview(const Creation& c, const ContentStore& content, const ContentStore& shown, Dice& dice);

}  // namespace gm
