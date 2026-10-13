// What a sheet calls the things of the content, in the language a page is shown in. The files keep the English names (and the keys);
// a page in another language sees and sends back the translated ones. A name that is no card of the content (something written by
// hand) stays as it was written, both ways.
#pragma once

#include <initializer_list>
#include <string>

#include "parsing/content.h"
#include "parsing/jsonutil.h"

namespace gm {

struct ShownNames {
    const ContentStore& en;          // the content in English: what the files are written with
    const ContentStore& shown;       // the same content in the language of the page

    bool same() const { return &en == &shown; }
    // The card a sheet means, in the English content: by its key, else by its name (English, or as shown). Null if there is none.
    const Entry* card(std::initializer_list<Kind> kinds, const std::string& key, const std::string& name) const;
    // The name to show for a card the sheet keeps as (key, English name); the name as written when it is no card.
    std::string show(std::initializer_list<Kind> kinds, const std::string& key, const std::string& name) const;
    // The English name to keep for a card a page sent as (key, shown name); the name as written when it is no card.
    std::string keep(std::initializer_list<Kind> kinds, const std::string& key, const std::string& name) const;
    // A name of any kind of card, as shown (for the messages that name one).
    std::string showAny(const std::string& name) const;
    // A tiny item, "Torch" or "Torch x3", either way.
    std::string showTiny(const std::string& text) const;
    std::string keepTiny(const std::string& text) const;
    // A condition ("Exhausted") as shown, and back.
    std::string showCondition(const std::string& name) const;
    std::string keepCondition(const std::string& name) const;

    // The editable document of a sheet (Character::toJson's fields) with its names as shown; a set of changes a page sent, with its
    // names back in English.
    json showDoc(json doc) const;
    json keepSet(json set) const;
};

}  // namespace gm
