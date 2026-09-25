// GM tools: "Generate <kind>" / "Edit" forms that write a custom card into one shared homebrew pack the app manages
// itself (created the first time any of them is used). Editing an existing card (a book one included) writes a
// replacement the same way a homerule replaces a rule: the original file is never touched, and the card reads
// "Changed by My Homebrew" from then on. Kin and abilities so far. A card of the GM's own pack can be deleted, and an edit of
// someone else's card reverted to the original; a card of any other pack has nothing to delete here (remove its pack).
#pragma once

#include <set>
#include <string>

#include "game/model.h"
#include "ui/filedialog.h"
#include "ui/module.h"

namespace gm {

class HomebrewForms {
public:
    explicit HomebrewForms(Host& host) : host_(host) {}

    static bool supports(Kind k) { return k == Kind::Kin || k == Kind::Ability; }
    // Delete (a card the GM made) / Revert (an edit of another pack's card): what the button is called, empty if it does not apply.
    static const char* deleteLabel(const Entry& e);
    void askDelete(const Entry& e);            // opens the confirmation on the next draw()
    // `existing`: null for a new card, the card being edited otherwise. The form opens on the next draw().
    void open(Kind k, const Entry* existing);
    // Once per frame, at the same ID-stack level every frame (OpenPopup and BeginPopupModal must see the same one).
    void draw();

private:
    std::string customPackDir() const;
    std::string copyIntoCustomPack(const std::string& absolutePath);

    void drawDelete();
    void openKin(const Entry* existing);
    void drawKin();
    bool saveKin();
    void openAbility(const Entry* existing);
    void drawAbility();
    bool saveAbility();
    bool saveNewAbility();

    Host& host_;

    Kind deleteKind_ = Kind::Kin;
    std::string deleteKey_, deleteTitle_;
    bool deleteRevert_ = false, wantDeleteOpen_ = false;
    std::string deleteError_;

    std::string kinOriginalKey_, kinName_, kinDescription_, kinMovement_, kinNames_;
    std::set<std::string> kinAbilitiesSelected_;
    std::string kinImagePicked_, kinImageExisting_, kinError_;
    FileDialog kinImageDialog_;
    bool kinNewAbilityOpen_ = false;
    std::string newAbilityName_, newAbilityWp_, newAbilityDescription_, newAbilityError_;
    bool kinWantOpen_ = false;

    std::string abilityOriginalKey_, abilityName_, abilityRequirement_, abilityWp_, abilityDescription_, abilityError_;
    int abilityHeroic_ = 1;
    bool abilityWantOpen_ = false;
};

}  // namespace gm
