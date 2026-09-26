#include "ui/homebrew_forms.h"

#include <cstdlib>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "parsing/content.h"
#include "parsing/fsutil.h"
#include "parsing/jsonutil.h"
#include "parsing/packs.h"
#include "ui/ui_common.h"

namespace gm {
namespace {

using namespace ui;

std::vector<std::string> splitBy(const std::string& text, char sep) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(sep, pos);
        if (end == std::string::npos) end = text.size();
        std::string piece = text.substr(pos, end - pos);
        while (!piece.empty() && (piece.front() == ' ' || piece.front() == '\t')) piece.erase(piece.begin());
        while (!piece.empty() && (piece.back() == ' ' || piece.back() == '\t' || piece.back() == '\r')) piece.pop_back();
        if (!piece.empty()) out.push_back(piece);
        pos = end + 1;
        if (end >= text.size()) break;
    }
    return out;
}

// "<pack>/<kind>/" of the cards the GM created in the app's own pack.
std::string ownPrefix(Kind kind) { return std::string(kCustomPack) + "/" + kindKey(kind) + "/"; }

// The slug a card of the file is keyed by: its "id" or, without one, its name (see slugOf, the loader's own rule).
std::string slugOfCard(const json& o) {
    const std::string id = jsonStr(o, "id");
    return slugOf(id.empty() ? jsonStr(o, "name") : id);
}

// Loads <dir>/<file>.yaml (or starts it), giving back the root; its list of cards is root[file].
json loadCustomFile(const std::string& dir, const char* file) {
    json root = json::object();
    if (const auto text = fs::readFile(dir + "/" + file + ".yaml")) {
        json parsed;
        if (jsonParse(*text, parsed, nullptr) && parsed.is_object()) root = parsed;
    }
    if (!root.contains("format")) root["format"] = 1;
    if (!root.contains("name")) root["name"] = kCustomPackName;
    if (!root.contains(file) || !root[file].is_array()) root[file] = json::array();
    return root;
}

bool writeCustomFile(Host& host, const std::string& dir, const char* file, const json& root, std::string& error) {
    SDL_CreateDirectory(dir.c_str());
    if (!fs::writeFile(dir + "/" + file + ".yaml", jsonToYaml(root))) {
        error = "Could not save: " + std::string(SDL_GetError());
        return false;
    }
    host.contentChanged();
    return true;
}

// Saves a card in the app's own pack (created the first time). `originalKey` is empty for a new card, otherwise the key of the card
// being edited. Editing never piles entries up: a card of this pack is updated where it is (its key kept, the "id" written down so a
// new name cannot change it), and an edit of another pack's card is ONE entry with "replaces", however many times it is saved.
bool saveCustomEntry(Host& host, const std::string& dir, Kind kind, json entry, const std::string& originalKey, std::string& error) {
    const char* file = kindFile(kind);
    json root = loadCustomFile(dir, file);
    json& cards = root[file];
    size_t at = cards.size();                                  // where the entry goes
    if (!originalKey.empty()) {
        json kept = json::array();
        size_t firstEdit = std::string::npos;
        for (const json& o : cards) {                          // the earlier edits of that card go: this one replaces them all
            if (jsonStr(o, "replaces") == originalKey) {
                if (firstEdit == std::string::npos) firstEdit = kept.size();
                continue;
            }
            kept.push_back(o);
        }
        cards = kept;
        at = firstEdit != std::string::npos ? firstEdit : cards.size();
        if (originalKey.starts_with(ownPrefix(kind))) {        // a card made here: update it in place
            const std::string slug = originalKey.substr(ownPrefix(kind).size());
            for (json& o : cards)
                if (jsonStr(o, "replaces").empty() && slugOfCard(o) == slug) {
                    entry["id"] = slug;
                    o = entry;
                    return writeCustomFile(host, dir, file, root, error);
                }
        }
        entry["replaces"] = originalKey;                       // (also when a card made here cannot be found)
    }
    cards.insert(cards.begin() + static_cast<std::ptrdiff_t>(at), std::move(entry));
    return writeCustomFile(host, dir, file, root, error);
}

// Takes a card out of the app's own pack: the card itself if the GM made it, and every edit of it. For the edit of another pack's card
// that leaves the original.
bool removeCustomEntry(Host& host, const std::string& dir, Kind kind, const std::string& key, std::string& error) {
    const char* file = kindFile(kind);
    json root = loadCustomFile(dir, file);
    json kept = json::array();
    const bool own = key.starts_with(ownPrefix(kind));
    const std::string slug = own ? key.substr(ownPrefix(kind).size()) : std::string();
    size_t removed = 0;
    for (const json& o : root[file]) {
        const bool edit = jsonStr(o, "replaces") == key;
        const bool made = own && jsonStr(o, "replaces").empty() && slugOfCard(o) == slug;
        if (edit || made) ++removed;
        else kept.push_back(o);
    }
    if (!removed) {
        error = "It is not in your homebrew pack.";
        return false;
    }
    root[file] = kept;
    return writeCustomFile(host, dir, file, root, error);
}

}  // namespace

void HomebrewForms::open(Kind k, const Entry* existing) {
    if (k == Kind::Kin) openKin(existing);
    else if (k == Kind::Ability) openAbility(existing);
}

void HomebrewForms::draw() {
    drawKin();
    drawAbility();
    drawDelete();
}

std::string HomebrewForms::customPackDir() const { return host_.paths().userPacksDir() + "/custom"; }

// Copies a picked file into the pack's own images/ folder and returns the "images/<name>" reference for it.
std::string HomebrewForms::copyIntoCustomPack(const std::string& absolutePath) {
    const std::string dir = customPackDir();
    SDL_CreateDirectory(dir.c_str());
    SDL_CreateDirectory((dir + "/images").c_str());
    const size_t slash = absolutePath.find_last_of("/\\");
    const std::string base = slash == std::string::npos ? absolutePath : absolutePath.substr(slash + 1);
    if (const auto data = fs::readFile(absolutePath)) fs::writeFile(dir + "/images/" + base, *data);
    return "images/" + base;
}

// ---- Delete / Revert ------------------------------------------------------------------------------------------

const char* HomebrewForms::deleteLabel(const Entry& e) {
    if (!supports(e.kind)) return "";
    if (e.key.starts_with(ownPrefix(e.kind))) return "Delete";                        // made here (and maybe edited since)
    return e.editedBy == kCustomPackName ? "Revert to original" : "";                // an edit of another pack's card
}

void HomebrewForms::askDelete(const Entry& e) {
    deleteKind_ = e.kind;
    deleteKey_ = e.key;
    deleteTitle_ = e.title;
    deleteRevert_ = !e.key.starts_with(ownPrefix(e.kind));
    deleteError_.clear();
    wantDeleteOpen_ = true;
}

void HomebrewForms::drawDelete() {
    const char* title = deleteRevert_ ? "Revert to original?" : "Delete?";
    if (wantDeleteOpen_) {
        ImGui::OpenPopup(title);                     // deferred to here: OpenPopup must see the same ID stack as BeginPopupModal
        wantDeleteOpen_ = false;
    }
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (deleteRevert_) ImGui::Text("Your changes to \"%s\" are removed and the original comes back.", deleteTitle_.c_str());
    else ImGui::Text("\"%s\" is deleted from your homebrew pack.", deleteTitle_.c_str());
    ImGui::TextColored(kGrey, "Characters that use it keep working: they remember the names.");
    if (!deleteError_.empty()) ImGui::TextColored(kRed, "%s", deleteError_.c_str());
    if (ImGui::Button(deleteRevert_ ? "Revert" : "Delete")) {
        if (removeCustomEntry(host_, customPackDir(), deleteKind_, deleteKey_, deleteError_)) ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---- Kin ------------------------------------------------------------------------------------------------------

void HomebrewForms::openKin(const Entry* existing) {
    kinOriginalKey_ = existing ? existing->key : "";
    kinName_ = existing ? existing->title : "";
    // the description as written: the card's body also carries its innate abilities' texts, which would be saved (and added again) as part of it
    kinDescription_ = !existing ? "" : existing->props.count("description") ? existing->prop("description") : existing->body;
    kinMovement_ = existing ? existing->prop("movement") : "";
    kinAbilitiesSelected_.clear();
    if (existing)
        for (const std::string& a : existing->list("innate_abilities")) kinAbilitiesSelected_.insert(a);
    kinNames_.clear();
    if (existing)
        for (const std::string& n : existing->list("names")) kinNames_ += (kinNames_.empty() ? "" : "\n") + n;
    kinImagePicked_.clear();
    kinImageExisting_ = existing ? existing->image : "";
    kinError_.clear();
    kinNewAbilityOpen_ = false;
    kinWantOpen_ = true;
}

void HomebrewForms::drawKin() {
    if (kinWantOpen_) {
        ImGui::OpenPopup("Generate Kin");    // deferred to here: OpenPopup must see the same ID stack as BeginPopupModal
        kinWantOpen_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(U(520), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Generate Kin", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextColored(kGrey, kinOriginalKey_.empty() ? "A new, custom kin - it goes on your own homebrew pack, nothing here is changed."
                                                      : "Your own version replaces this kin everywhere; the original is never touched.");
    ImGui::Spacing();
    ImGui::TextColored(kGold, "Name");
    inputStr("##kname", kinName_);
    ImGui::TextColored(kGold, "Movement");
    inputStr("##kmove", kinMovement_, U(100));
    ImGui::TextColored(kGold, "Description");
    inputMultiline("##kdesc", kinDescription_, ImVec2(-FLT_MIN, U(90)));

    ImGui::TextColored(kGold, "Innate abilities");
    ImGui::BeginChild("##kabilitylist", ImVec2(-FLT_MIN, U(120)), ImGuiChildFlags_Borders);
    for (const Entry& a : host_.content().entries(Kind::Ability)) {
        if (a.prop("type") != "kin") continue;   // only innate abilities are offered here - Kin never has a heroic one
        bool on = kinAbilitiesSelected_.count(a.title) != 0;
        if (ImGui::Checkbox((a.title + "##ka" + std::to_string(a.id)).c_str(), &on)) {
            if (on) kinAbilitiesSelected_.insert(a.title);
            else kinAbilitiesSelected_.erase(a.title);
        }
    }
    ImGui::EndChild();
    if (!kinNewAbilityOpen_) {
        if (ImGui::SmallButton("+ New innate ability…")) {
            kinNewAbilityOpen_ = true;
            newAbilityName_.clear();
            newAbilityWp_.clear();
            newAbilityDescription_.clear();
            newAbilityError_.clear();
        }
    } else {
        ImGui::Spacing();
        ImGui::TextColored(kGold, "New innate ability");
        ImGui::Text("Name");
        inputStr("##nabname", newAbilityName_);
        ImGui::Text("Willpower points");
        inputStr("##nabwp", newAbilityWp_, U(100));
        ImGui::Text("Description");
        inputMultiline("##nabdesc", newAbilityDescription_, ImVec2(-FLT_MIN, U(70)));
        if (!newAbilityError_.empty()) ImGui::TextColored(kRed, "%s", newAbilityError_.c_str());
        if (ImGui::SmallButton("Add")) {
            if (saveNewAbility()) {
                kinAbilitiesSelected_.insert(newAbilityName_);
                kinNewAbilityOpen_ = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel##nab")) kinNewAbilityOpen_ = false;
        ImGui::Spacing();
    }

    ImGui::TextColored(kGold, "First names (one per line)");
    inputMultiline("##knames", kinNames_, ImVec2(-FLT_MIN, U(90)));
    ImGui::TextColored(kGold, "Image");
    const std::string& preview = kinImagePicked_.empty() ? kinImageExisting_ : kinImagePicked_;
    if (!preview.empty())
        if (const TextureCache::Tex* t = host_.textures().get(preview)) {
            const float w = U(72), h = w * static_cast<float>(t->h) / static_cast<float>(t->w);
            ImGui::Image(reinterpret_cast<ImTextureID>(t->tex), ImVec2(w, h));
            ImGui::SameLine();
        }
    if (ImGui::Button("Choose image…")) kinImageDialog_.openFile(host_.window(), "Images", "png;jpg;jpeg;bmp;gif");
    std::string picked;
    if (kinImageDialog_.poll(picked) && !picked.empty()) kinImagePicked_ = picked;
    if (!kinError_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kRed, "%s", kinError_.c_str());
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Save")) {
        if (saveKin()) ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

bool HomebrewForms::saveKin() {
    if (kinName_.empty()) {
        kinError_ = "Name is required.";
        return false;
    }
    json entry = json::object();
    entry["name"] = kinName_;
    if (!kinDescription_.empty()) entry["description"] = kinDescription_;
    if (!kinMovement_.empty()) entry["movement"] = std::atoi(kinMovement_.c_str());
    if (!kinAbilitiesSelected_.empty()) entry["innate_abilities"] = std::vector<std::string>(kinAbilitiesSelected_.begin(), kinAbilitiesSelected_.end());
    const std::vector<std::string> names = splitBy(kinNames_, '\n');
    if (!names.empty()) entry["names"] = names;
    // only a newly chosen picture is written: a card that already has one (the book's own, or an earlier edit's) keeps
    // it as it is, exactly like leaving any other field untouched does
    if (!kinImagePicked_.empty()) entry["image"] = copyIntoCustomPack(kinImagePicked_);
    return saveCustomEntry(host_, customPackDir(), Kind::Kin, std::move(entry), kinOriginalKey_, kinError_);
}

// ---- Abilities ------------------------------------------------------------------------------------------------

void HomebrewForms::openAbility(const Entry* existing) {
    abilityOriginalKey_ = existing ? existing->key : "";
    abilityName_ = existing ? existing->title : "";
    abilityHeroic_ = !existing || existing->prop("type") != "kin";
    abilityRequirement_.clear();
    abilityWp_ = existing ? existing->prop("wp_cost") : "";
    abilityDescription_ = existing ? existing->body : "";
    if (existing)
        for (const Field& f : existing->fields)
            if (f.label == "Requirement") abilityRequirement_ = f.value;
    abilityError_.clear();
    abilityWantOpen_ = true;
}

void HomebrewForms::drawAbility() {
    if (abilityWantOpen_) {
        ImGui::OpenPopup("Generate Ability");    // deferred: see drawKin()
        abilityWantOpen_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(U(480), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Generate Ability", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextColored(kGrey, abilityOriginalKey_.empty() ? "A new, custom ability - it goes on your own homebrew pack, nothing here is changed."
                                                          : "Your own version replaces this ability everywhere; the original is never touched.");
    ImGui::Spacing();
    ImGui::TextColored(kGold, "Name");
    inputStr("##aname", abilityName_);
    ImGui::TextColored(kGold, "Type");
    ImGui::RadioButton("Heroic", &abilityHeroic_, 1);   // an int (1/0): ImGui::RadioButton wants an int*, not a bool*
    ImGui::SameLine();
    ImGui::RadioButton("Innate", &abilityHeroic_, 0);
    ImGui::TextColored(kGold, "Requirement");
    inputStr("##areq", abilityRequirement_);
    ImGui::TextColored(kGold, "Willpower points");
    inputStr("##awp", abilityWp_, U(100));
    ImGui::TextColored(kGold, "Description");
    inputMultiline("##adesc", abilityDescription_, ImVec2(-FLT_MIN, U(120)));
    if (!abilityError_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kRed, "%s", abilityError_.c_str());
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Save")) {
        if (saveAbility()) ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

bool HomebrewForms::saveAbility() {
    if (abilityName_.empty()) {
        abilityError_ = "Name is required.";
        return false;
    }
    json entry = json::object();
    entry["name"] = abilityName_;
    entry["type"] = abilityHeroic_ ? "heroic" : "kin";
    if (!abilityRequirement_.empty()) entry["requirement"] = abilityRequirement_;
    if (!abilityWp_.empty()) entry["wp_cost"] = abilityWp_;
    if (!abilityDescription_.empty()) entry["description"] = abilityDescription_;
    return saveCustomEntry(host_, customPackDir(), Kind::Ability, std::move(entry), abilityOriginalKey_, abilityError_);
}

// The Kin form's own quick "+ New innate ability…": the same ability data, always Innate (the only kind Kin offers).
bool HomebrewForms::saveNewAbility() {
    if (newAbilityName_.empty()) {
        newAbilityError_ = "Name is required.";
        return false;
    }
    json entry = json::object();
    entry["name"] = newAbilityName_;
    entry["type"] = "kin";
    if (!newAbilityWp_.empty()) entry["wp_cost"] = newAbilityWp_;
    if (!newAbilityDescription_.empty()) entry["description"] = newAbilityDescription_;
    return saveCustomEntry(host_, customPackDir(), Kind::Ability, std::move(entry), "", newAbilityError_);
}

}  // namespace gm
