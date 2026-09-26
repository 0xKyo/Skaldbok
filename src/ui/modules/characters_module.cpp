// Characters: the players' sheets, a step-by-step creator that follows the Rulebook's chapter 2, and free editing afterwards.
// The sheets are JSON files (see docs/CHARACTERS.md); everything a character takes from the game content (kin, skills, spells,
// gear) is chosen from what is loaded, so homebrew packs show up in the creator automatically.
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "game/creation.h"
#include "ui/modules/character_sheet.h"
#include "game/encounter.h"
#include "ui/filedialog.h"
#include "parsing/fts.h"
#include "ui/modules/modules.h"
#include "ui/ui_common.h"
#include "game/web_link.h"

namespace gm {
namespace {

using namespace ui;

int carried(const Character& c) { return carriedItems(c); }

// ------------------------------------------------------------------------------------------- module

enum class Step { Kin, Profession, Age, Attributes, Skills, Ability, Gear, Details, Review };
const char* const kStepNames[] = {"Kin", "Profession", "Age", "Attributes", "Skills", "Ability & magic", "Gear", "Name & details", "Review"};
constexpr int kStepCount = 9;

struct Wizard {
    Creation cr;
    int step = 0;
    int furthest = 0;
    int swapA = 0, swapB = 1;
    bool manualScores = false;
    std::string note;                                 // what the last roll gave
};

class CharactersModule : public Module, public ICharacters {
public:
    explicit CharactersModule(Host& host) : Module(host), sheet_(host) {}

    const char* id() const override { return "characters"; }
    const char* title() const override { return "Characters"; }
    const char* group() const override { return "Play"; }
    bool ownsDetail() const override { return true; }
    int badge() const override { return static_cast<int>(host_.characters().all().size()); }

    void openCharacter(const std::string& cid) override {
        selId_ = cid;
        wizard_ = false;
        host_.showModule("characters");
    }

    void onContentChanged() override { bookTables_.clear(); }

    void update() override {
        std::string path;
        if (!dialog_.poll(path)) return;
        if (path.empty()) return;
        std::string err;
        if (dialogAction_ == Action::Import) {
            std::string newId;
            if (host_.characters().importFile(path, &newId, &err)) {
                selId_ = newId;
                wizard_ = false;
                host_.notify("Imported the character");
            } else {
                host_.notify("Could not import: " + err);
            }
        } else if (dialogAction_ == Action::Export) {
            if (const Character* c = host_.characters().find(exportId_)) {
                if (host_.characters().exportFile(*c, path, &err)) host_.notify("Saved " + path);
                else host_.notify("Could not save: " + err);
            }
        }
    }
    bool wantsRedraw() const override { return dialog_.busy(); }

    // For automated screenshots: --demo-character makes a sample and opens it, --new-character opens the creator.
    void startCreator(int step) override {
        beginWizard();
        if (step <= 0) return;
        wz_.cr = sampleCreation();
        wz_.step = std::min(step, kStepCount - 1);
        wz_.furthest = kStepCount - 1;
    }
    void demoCharacter() override {
        Character c = buildCharacter(sampleCreation(), host_.content(), host_.dice());
        c.hp = std::max(1, c.hp - 3);
        host_.characters().save(c);
        selId_ = c.id;
        wizard_ = false;
    }

    void drawList() override {
        if (ImGui::Button("New character")) beginWizard();
        ImGui::SameLine();
        if (ImGui::Button("Random")) {
            Character c = buildCharacter(randomCreation(host_.content(), host_.dice()), host_.content(), host_.dice());
            host_.characters().save(c);
            selId_ = c.id;
            wizard_ = false;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A complete character made with the book's own rolls");
        ImGui::SameLine();
        if (ImGui::Button("Import…")) {
            dialogAction_ = Action::Import;
            dialog_.openFile(host_.window(), "Character (JSON)", "json");
        }
        ImGui::BeginChild("##characters", ImVec2(0, 0));
        const auto& all = host_.characters().all();
        if (all.empty()) ImGui::TextWrapped("No characters yet. Create one with the button above: the creator follows the Rulebook step by step.");
        for (const Character& c : all) {
            const float rowH = lineH() * 2.0f + 12.0f;
            ImGui::PushID(c.id.c_str());
            if (ImGui::Selectable("##c", !wizard_ && c.id == selId_, 0, ImVec2(0, rowH))) {
                selId_ = c.id;
                wizard_ = false;
            }
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(mn.x + 10, mn.y + 6), ImGui::GetColorU32(ImGuiCol_Text), c.displayName().c_str());
            const std::string sub = c.kin.name + " " + c.profession.name + " · " + ageRule(c.age).label;
            dl->AddText(ImVec2(mn.x + 10, mn.y + 6 + lineH() + 2), ImGui::GetColorU32(kGrey), sub.c_str());
            char hp[32];
            std::snprintf(hp, sizeof hp, "HP %d/%d", c.hp, maxHp(c));
            const ImVec2 hs = ImGui::CalcTextSize(hp);
            dl->AddText(ImVec2(mx.x - hs.x - 8, mn.y + 6), ImGui::GetColorU32(c.hp * 2 <= maxHp(c) ? kGold : kGrey), hp);
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void drawDetail() override {
        if (wizard_) {
            drawWizard();
            return;
        }
        if (host_.characters().find(selId_)) {
            drawSheet();
            return;
        }
        ImGui::Spacing();
        bigText("Characters", 1.5f, kAccent);
        ImGui::TextWrapped("Pick a character from the list, or create one. The creator follows chapter 2 of the Rulebook: kin, profession, age, "
                           "attributes (4D6, drop the lowest), trained skills, heroic ability or magic, gear, name and details. Everything you can "
                           "choose comes from the loaded content, so homebrew kin, professions, skills and spells appear there too. Once created "
                           "the sheet can be edited freely; it is saved as a JSON file that other tools can read.");
        ImGui::Spacing();
        if (ImGui::Button("Create a character")) beginWizard();
    }

private:
    enum class Action { None, Import, Export };

    const ContentStore& cs() const { return host_.content(); }

    void beginWizard() {
        wz_ = Wizard{};
        wizard_ = true;
    }

    // A recognisable sample: a human fighter (or a random character if the Core pack is not there).
    Creation sampleCreation() {
        Dice& dice = host_.dice();
        Creation cr;
        const Entry* fighter = host_.content().findByName(Kind::Profession, "Fighter");
        const Entry* human = host_.content().findByName(Kind::Kin, "Human");
        if (!fighter || !human) return randomCreation(host_.content(), dice);
        cr.kinKey = human->key;
        cr.professionKey = fighter->key;
        const int scores[6] = {15, 13, 12, 9, 10, 11};
        std::copy(scores, scores + 6, cr.rolled);
        cr.professionSkills = {"Axes", "Bows", "Brawling", "Crossbows", "Evade", "Swords"};
        cr.freeSkills = {"Awareness", "Healing", "Riding", "Swimming"};
        cr.heroicAbility = "Veteran";
        cr.name = "Brenna";
        cr.nickname = "Grimjaw";
        cr.player = "Sebastián";
        chooseGearSet(cr, *fighter, 0, dice);
        cr.gear[0].choice = 2;
        if (const DataTable* t = host_.content().tableByRole("weakness")) cr.weakness = rollTable(*t, dice);
        return cr;
    }

    // ============================================================================== the sheet
    void drawSheet() {
        CharacterStore& store = host_.characters();
        Character c = *store.find(selId_);              // edited as a copy, saved when something changed
        bool changed = false;
        ImGui::PushID(c.id.c_str());

        if (sheetFor_ != c.id) {
            sheet_.reset();
            sheetFor_ = c.id;
        }

        // ---- actions --------------------------------------------------------------------------------
        IEncounterSink* encounter = serviceOf<IEncounterSink>(host_);
        IMessenger* messenger = serviceOf<IMessenger>(host_);
        if (encounter && ImGui::Button("Add to encounter")) encounter->addPlayer(c);
        if (encounter) ImGui::SameLine();
        if (messenger) {
            if (ImGui::Button("Message…")) messenger->compose("char:" + c.id, "", "");
            ImGui::SameLine();
        }
        if (IMasterScreen* screen = serviceOf<IMasterScreen>(host_)) {
            if (ImGui::Button("Pin")) screen->pinCharacter(c.id);
            ImGui::SameLine();
        }
        if (ImGui::Button("Export…")) {
            dialogAction_ = Action::Export;
            exportId_ = c.id;
            dialog_.saveFile(host_.window(), "Character (YAML)", "yaml", (c.name.empty() ? std::string("character") : c.name) + ".yaml");
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            Character copy = c;
            copy.id.clear();
            copy.createdAt.clear();
            copy.name += " (copy)";
            store.save(copy);
            selId_ = copy.id;
            ImGui::PopID();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete this character?");
        if (ImGui::BeginPopupModal("Delete this character?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s will be removed for good (its file is deleted).", c.displayName().c_str());
            if (ImGui::Button("Delete")) {
                store.remove(c.id);
                host_.parties().forgetCharacter(c.id);
                host_.messages().forget(c.id);
                selId_.clear();
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                ImGui::PopID();
                return;
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Separator();

        ImGui::BeginChild("##sheet", ImVec2(0, 0));

        changed |= sheet_.draw(c);

        // ---- the player's web page ----------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Player's web page")) {
            const std::string link = webLinkFor(host_.paths().prefDir, c.id);
            if (link.empty()) {
                ImGui::TextWrapped("The web server makes each player's personal link. Start skaldbok_web once (or run  skaldbok_web --links ) and it appears here.");
            } else {
                ImGui::TextWrapped("The player sees this sheet, the party and the rules at this address. The link is theirs alone:");
                ImGui::TextColored(kAccent, "%s", link.c_str());
                if (ImGui::SmallButton("Copy link")) {
                    SDL_SetClipboardText(link.c_str());
                    host_.notify("Link for " + c.name + " copied");
                }
                if (messenger) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Send it in a message")) messenger->compose("char:" + c.id, "", "Your character page: " + link);
                }
            }
        }

        ImGui::EndChild();
        if (changed) store.save(c);
        ImGui::PopID();
    }

    // ============================================================================== the creator
    // A step is finished when the choices it asks for are made.
    bool stepDone(int step) const {
        const Creation& cr = wz_.cr;
        const ContentStore& content = cs();
        const Entry* prof = content.entry(Kind::Profession, content.idByKey(Kind::Profession, cr.professionKey));
        switch (static_cast<Step>(step)) {
            case Step::Kin: return content.idByKey(Kind::Kin, cr.kinKey) != 0;
            case Step::Profession: return prof && (prof->list("schools").empty() || !cr.school.empty());
            case Step::Age: return true;
            case Step::Attributes:
                for (int v : cr.rolled)
                    if (v < 3) return false;
                return true;
            case Step::Skills: {
                if (!prof) return false;
                const size_t need = std::min<size_t>(kProfessionSkills, professionSkillOptions(*prof, cr.school).size());
                return cr.professionSkills.size() == need &&
                       static_cast<int>(cr.freeSkills.size()) == ageRule(cr.age).trainedSkills - static_cast<int>(need);
            }
            case Step::Ability: {
                if (!prof) return false;
                if (!prof->list("schools").empty()) {
                    int spells = 0, tricks = 0;
                    for (const std::string& k : cr.spells)
                        if (const Entry* e = content.entry(Kind::Spell, content.idByKey(Kind::Spell, k))) (e->prop("trick") == "1" ? tricks : spells)++;
                    return spells == std::atoi(prof->prop("magic.spells").c_str()) && tricks == std::atoi(prof->prop("magic.tricks").c_str());
                }
                return prof->list("heroic_abilities").empty() || !cr.heroicAbility.empty();
            }
            case Step::Gear: return !prof || prof->list("starting_gear").empty() || cr.gearSet >= 0;
            case Step::Details: return !trimmed(cr.name).empty();
            case Step::Review: return validateCreation(cr, content).empty();
        }
        return false;
    }

    void drawWizard() {
        const ContentStore& content = host_.content();
        if (content.entries(Kind::Kin).empty() || content.entries(Kind::Profession).empty()) {
            ImGui::TextWrapped("There is no kin or profession loaded, so a character cannot be created. Check that the Core pack (data/packs/core) is in place.");
            if (ImGui::Button("Back")) wizard_ = false;
            return;
        }
        // the steps on the left
        ImGui::BeginChild("##steps", ImVec2(U(170), 0));
        bigText("New character", 1.2f, kAccent);
        for (int i = 0; i < kStepCount; ++i) {
            const bool reachable = i <= wz_.furthest;
            ImGui::BeginDisabled(!reachable);
            char label[64];
            std::snprintf(label, sizeof label, "%s %d. %s", (i < wz_.step || (i <= wz_.furthest && stepDone(i) && i != wz_.step)) ? "+" : " ", i + 1, kStepNames[i]);
            if (ImGui::Selectable(label, wz_.step == i)) wz_.step = i;
            ImGui::EndDisabled();
        }
        ImGui::Spacing();
        if (ImGui::Button("Cancel")) wizard_ = false;
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##wizardmain", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        const float footer = ImGui::GetFrameHeightWithSpacing() + U(8);
        ImGui::BeginChild("##wizardbody", ImVec2(0, -footer));
        bigText(kStepNames[wz_.step], 1.5f, ImGui::GetStyle().Colors[ImGuiCol_Text]);
        switch (static_cast<Step>(wz_.step)) {
            case Step::Kin: stepKin(); break;
            case Step::Profession: stepProfession(); break;
            case Step::Age: stepAge(); break;
            case Step::Attributes: stepAttributes(); break;
            case Step::Skills: stepSkills(); break;
            case Step::Ability: stepAbility(); break;
            case Step::Gear: stepGear(); break;
            case Step::Details: stepDetails(); break;
            case Step::Review: stepReview(); break;
        }
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::BeginDisabled(wz_.step == 0);
        if (ImGui::Button("< Back")) --wz_.step;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (wz_.step < kStepCount - 1) {
            ImGui::BeginDisabled(!stepDone(wz_.step));
            if (ImGui::Button("Next >")) {
                ++wz_.step;
                wz_.furthest = std::max(wz_.furthest, wz_.step);
            }
            ImGui::EndDisabled();
            if (!stepDone(wz_.step) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Finish this step first");
        } else {
            const auto problems = validateCreation(wz_.cr, content);
            ImGui::BeginDisabled(!problems.empty());
            if (ImGui::Button("Create character")) {
                Character c = buildCharacter(wz_.cr, content, host_.dice());
                if (host_.characters().save(c)) {
                    selId_ = c.id;
                    wizard_ = false;
                    host_.notify("Created " + c.displayName());
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::SameLine(0, 20);
        ImGui::TextColored(kGrey, "%s", wz_.note.c_str());
        ImGui::EndChild();
    }

    // Clicking a row selects it; the selected one is highlighted. Returns true when clicked.
    bool choiceRow(const std::string& title, const std::string& detail, bool selected) {
        ImGui::PushID(title.c_str());
        const float h = lineH() * 2.0f + U(12);
        const bool clicked = ImGui::Selectable("##choice", selected, 0, ImVec2(0, h));
        const ImVec2 mn = ImGui::GetItemRectMin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ImVec2(mn.x + 10, mn.y + 5), ImGui::GetColorU32(selected ? kAccent : ImGui::GetStyle().Colors[ImGuiCol_Text]), title.c_str());
        dl->AddText(ImVec2(mn.x + 10, mn.y + 5 + lineH() + 2), ImGui::GetColorU32(kGrey), detail.c_str());
        ImGui::PopID();
        return clicked;
    }

    static std::string firstSentence(const std::string& s, size_t max = 150) {
        std::string t = s.substr(0, s.find('\n'));
        if (t.size() > max) t = t.substr(0, max) + "…";
        return t;
    }

    // The book rolls kin and profession on tables of the Rulebook (D12 / D10), which live in Core.
    const DataTable* bookTable(const char* title) {
        auto it = bookTables_.find(title);
        if (it == bookTables_.end()) {
            DataTable t;
            const int rulebook = host_.content().bookId("rulebook");
            for (const DataTable& pt : host_.content().packTables())
                if (pt.title == title && pt.sourceId == rulebook) {
                    t = pt;
                    break;
                }
            it = bookTables_.emplace(title, std::move(t)).first;
        }
        return it->second.rows.empty() ? nullptr : &it->second;
    }

    void stepKin() {
        Creation& cr = wz_.cr;
        const ContentStore& content = host_.content();
        ImGui::TextWrapped("Choose your kin. Each has an innate ability that no other kin can learn.");
        if (ImGui::Button("Roll D12")) {
            const DataTable* t = bookTable("Kin");
            std::string name = t ? rollTable(*t, host_.dice()) : std::string();
            const Entry* e = name.empty() ? nullptr : content.findByName(Kind::Kin, name);
            if (!e) e = &content.entries(Kind::Kin)[static_cast<size_t>(host_.dice().roll(static_cast<int>(content.entries(Kind::Kin).size())) - 1)];
            chooseKin(e->key);
            wz_.note = "Rolled: " + e->title;
        }
        for (const Entry& e : content.entries(Kind::Kin)) {
            const std::string mv = e.prop("movement");
            const SourceInfo* si = content.source(e.sourceId);
            const std::string detail = (mv.empty() ? "" : "Movement " + mv + " · ") + (e.list("innate_abilities").empty() ? "" : "innate: " + e.list("innate_abilities")[0] + " · ") +
                                       (si && si->homebrew ? si->label : firstSentence(e.body, 90));
            if (choiceRow(e.title, detail, cr.kinKey == e.key)) chooseKin(e.key);
        }
        if (const Entry* e = content.entry(Kind::Kin, content.idByKey(Kind::Kin, cr.kinKey))) {
            ImGui::Separator();
            paragraphs(e->body);
        }
    }

    void chooseKin(const std::string& key) {
        wz_.cr.kinKey = key;
        wz_.cr.name.clear();                              // the names offered depend on the kin
    }

    void stepProfession() {
        Creation& cr = wz_.cr;
        const ContentStore& content = host_.content();
        ImGui::TextWrapped("Your profession decides six of your trained skills, your heroic ability and what gear you start with.");
        if (ImGui::Button("Roll D10")) {
            const DataTable* t = bookTable("Profession");
            std::string name = t ? rollTable(*t, host_.dice()) : std::string();
            const Entry* e = name.empty() ? nullptr : content.findByName(Kind::Profession, name);
            if (!e) e = &content.entries(Kind::Profession)[static_cast<size_t>(host_.dice().roll(static_cast<int>(content.entries(Kind::Profession).size())) - 1)];
            chooseProfession(e->key);
            wz_.note = "Rolled: " + e->title;
        }
        for (const Entry& e : content.entries(Kind::Profession)) {
            const SourceInfo* si = content.source(e.sourceId);
            const std::string detail = "Key attribute " + e.prop("key_attribute") + " · " +
                                       (e.list("schools").empty() ? std::to_string(e.list("skills").size()) + " skills" : "magic: " + std::to_string(e.list("schools").size()) + " schools") +
                                       (si && si->homebrew ? " · " + si->label : "");
            if (choiceRow(e.title, detail, cr.professionKey == e.key)) chooseProfession(e.key);
        }
        const Entry* prof = content.entry(Kind::Profession, content.idByKey(Kind::Profession, cr.professionKey));
        if (prof) {
            ImGui::Separator();
            if (!prof->list("schools").empty()) {
                ImGui::TextColored(kGold, "School of magic");
                ImGui::SameLine();
                ImGui::TextColored(kGrey, "(becomes one of your trained skills)");
                for (const std::string& s : prof->list("schools")) {
                    if (ImGui::RadioButton(s.c_str(), cr.school == s)) {
                        cr.school = s;
                        cr.professionSkills = {s};
                        cr.spells.clear();
                    }
                    ImGui::SameLine(0, 16);
                }
                ImGui::NewLine();
            }
            paragraphs(prof->body);
            fieldsTable(prof->fields, "##profields");
        }
    }

    void chooseProfession(const std::string& key) {
        Creation& cr = wz_.cr;
        if (cr.professionKey == key) return;
        cr.professionKey = key;
        cr.school.clear();
        cr.professionSkills.clear();
        cr.freeSkills.clear();
        cr.heroicAbility.clear();
        cr.spells.clear();
        cr.gearSet = -1;
        cr.gear.clear();
        cr.nickname.clear();
    }

    void stepAge() {
        Creation& cr = wz_.cr;
        ImGui::TextWrapped("Older adventurers start with lower attributes but more trained skills. The modifications do not stack.");
        if (ImGui::Button("Roll D6")) {
            const int r = host_.dice().roll(6);
            setAge(r <= 3 ? "young" : r <= 5 ? "adult" : "old");
            wz_.note = "Rolled " + std::to_string(r) + ": " + ageRule(cr.age).label;
        }
        for (const char* a : {"young", "adult", "old"}) {
            const AgeRule& r = ageRule(a);
            if (choiceRow(r.label, r.summary, cr.age == a)) setAge(a);
        }
    }

    void setAge(const std::string& age) {
        wz_.cr.age = ageRule(age).id;
        const Entry* prof = cs().entry(Kind::Profession, cs().idByKey(Kind::Profession, wz_.cr.professionKey));
        const size_t need = prof ? std::min<size_t>(kProfessionSkills, professionSkillOptions(*prof, wz_.cr.school).size()) : kProfessionSkills;
        const size_t wanted = static_cast<size_t>(std::max(0, ageRule(wz_.cr.age).trainedSkills - static_cast<int>(need)));
        if (wz_.cr.freeSkills.size() > wanted) wz_.cr.freeSkills.resize(wanted);
    }

    void stepAttributes() {
        Creation& cr = wz_.cr;
        ImGui::TextWrapped("Roll 4D6 and remove the worst die for each attribute, assigning each score as you roll it. When all six are set you may swap "
                           "two. Then your age adjusts them (never above 18). Or enter the scores by hand if the group uses another method.");
        if (ImGui::Button("Roll all six")) {
            for (int i = 0; i < kAttrCount; ++i) cr.rolled[i] = rollAttribute(host_.dice());
            wz_.note = "Rolled six attributes";
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) std::fill(cr.rolled, cr.rolled + kAttrCount, 0);
        int final[kAttrCount];
        int shown[kAttrCount];
        for (int i = 0; i < kAttrCount; ++i) shown[i] = std::max(3, cr.rolled[i]);
        applyAge(shown, cr.age, final);
        if (ImGui::BeginTable("##rollattrs", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Attribute", ImGuiTableColumnFlags_WidthFixed, U(150));
            ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, U(80));
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, U(84));
            ImGui::TableSetupColumn("With age", ImGuiTableColumnFlags_WidthFixed, U(74));
            ImGui::TableSetupColumn("Base chance", ImGuiTableColumnFlags_WidthFixed, U(96));
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            const Entry* prof = cs().entry(Kind::Profession, cs().idByKey(Kind::Profession, cr.professionKey));
            for (int i = 0; i < kAttrCount; ++i) {
                ImGui::TableNextRow();
                ImGui::PushID(i);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(kGold, "%s", kAttrShort[i]);
                ImGui::SameLine();
                ImGui::TextColored(kGrey, "%s", kAttrLong[i]);
                ImGui::TableSetColumnIndex(1);
                int v = cr.rolled[i];
                if (smallInt("##v", v, 0, 18, 64)) cr.rolled[i] = v > 0 && v < 3 ? 3 : v;
                ImGui::TableSetColumnIndex(2);
                if (ImGui::SmallButton("Roll 4D6")) cr.rolled[i] = rollAttribute(host_.dice());
                ImGui::TableSetColumnIndex(3);
                if (cr.rolled[i] > 0) ImGui::TextColored(final[i] != cr.rolled[i] ? kAccent : ImGui::GetStyle().Colors[ImGuiCol_Text], "%d", final[i]);
                ImGui::TableSetColumnIndex(4);
                if (cr.rolled[i] > 0) ImGui::Text("%d", baseChance(final[i]));
                ImGui::TableSetColumnIndex(5);
                if (prof && prof->prop("key_attribute") == kAttrShort[i]) ImGui::TextColored(kGrey, "key attribute of the profession");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        bool all = true;
        for (int v : cr.rolled) all &= v >= 3;
        if (all) {
            ImGui::Spacing();
            ImGui::TextColored(kGold, "Swap two scores");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(U(80));
            if (ImGui::BeginCombo("##swapa", kAttrShort[wz_.swapA])) {
                for (int i = 0; i < kAttrCount; ++i)
                    if (ImGui::Selectable(kAttrShort[i], wz_.swapA == i)) wz_.swapA = i;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(U(80));
            if (ImGui::BeginCombo("##swapb", kAttrShort[wz_.swapB])) {
                for (int i = 0; i < kAttrCount; ++i)
                    if (ImGui::Selectable(kAttrShort[i], wz_.swapB == i)) wz_.swapB = i;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Swap")) std::swap(cr.rolled[wz_.swapA], cr.rolled[wz_.swapB]);

            ImGui::Spacing();
            const Entry* kin = cs().entry(Kind::Kin, cs().idByKey(Kind::Kin, cr.kinKey));
            Character preview;
            std::copy(final, final + kAttrCount, preview.attr);
            const int move = kin ? std::atoi(kin->prop("movement").c_str()) : 0;
            ImGui::TextColored(kGold, "Derived");
            ImGui::Text("HP %d · WP %d · Movement %s · Damage bonus STR %s / AGL %s · Encumbrance limit %d", maxHp(preview), maxWp(preview),
                        move ? std::to_string(move + movementModifier(final[2])).c_str() : "?", damageBonus(final[0]).empty() ? "–" : damageBonus(final[0]).c_str(),
                        damageBonus(final[2]).empty() ? "–" : damageBonus(final[2]).c_str(), encumbranceLimit(preview));
        }
    }

    void stepSkills() {
        Creation& cr = wz_.cr;
        const ContentStore& content = host_.content();
        const Entry* prof = content.entry(Kind::Profession, content.idByKey(Kind::Profession, cr.professionKey));
        if (!prof) return;
        int final[kAttrCount], shown[kAttrCount];
        for (int i = 0; i < kAttrCount; ++i) shown[i] = std::max(3, cr.rolled[i]);
        applyAge(shown, cr.age, final);
        auto levelOf = [&](const std::string& skill) {
            const Entry* e = content.findByName(Kind::Skill, skill);
            const int a = e ? attrIndex(e->prop("attribute")) : -1;
            return a < 0 ? 0 : std::min(18, 2 * baseChance(final[a]));
        };
        const std::vector<std::string> options = professionSkillOptions(*prof, cr.school);
        const size_t need = std::min<size_t>(kProfessionSkills, options.size());
        const int wanted = ageRule(cr.age).trainedSkills - static_cast<int>(need);
        ImGui::TextWrapped("A trained skill starts at twice its base chance. Choose %d from your profession's list and %d of your own (%s).", static_cast<int>(need), wanted,
                           ageRule(cr.age).label);
        if (ImGui::Button("Choose for me")) {
            cr.professionSkills.clear();
            std::vector<std::string> pool = options;
            if (!cr.school.empty()) {
                cr.professionSkills.push_back(cr.school);
                std::erase(pool, cr.school);
            }
            while (cr.professionSkills.size() < need && !pool.empty()) {
                const size_t i = static_cast<size_t>(host_.dice().roll(static_cast<int>(pool.size())) - 1);
                cr.professionSkills.push_back(pool[i]);
                pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(i));
            }
            cr.freeSkills.clear();
            auto free = selectableSkills(content);
            std::erase_if(free, [&](const Entry* e) { return std::ranges::contains(cr.professionSkills, e->title); });
            while (static_cast<int>(cr.freeSkills.size()) < wanted && !free.empty()) {
                const size_t i = static_cast<size_t>(host_.dice().roll(static_cast<int>(free.size())) - 1);
                cr.freeSkills.push_back(free[i]->title);
                free.erase(free.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
        ImGui::Columns(2, "##skillcols", false);
        ImGui::TextColored(kAccent, "From your profession: %d of %d", static_cast<int>(cr.professionSkills.size()), static_cast<int>(need));
        for (const std::string& s : options) {
            const bool on = std::ranges::contains(cr.professionSkills, s);
            const bool locked = !cr.school.empty() && s == cr.school;        // the school of magic is always trained
            ImGui::BeginDisabled(locked || (!on && cr.professionSkills.size() >= need));
            bool v = on;
            char label[96];
            std::snprintf(label, sizeof label, "%s  (level %d)##p%s", s.c_str(), levelOf(s), s.c_str());
            if (ImGui::Checkbox(label, &v)) {
                if (v) cr.professionSkills.push_back(s);
                else std::erase(cr.professionSkills, s);
                // a skill moved into the profession list cannot also be a free one
                std::erase(cr.freeSkills, s);
            }
            ImGui::EndDisabled();
        }
        ImGui::NextColumn();
        ImGui::TextColored(kAccent, "Your own: %d of %d", static_cast<int>(cr.freeSkills.size()), wanted);
        ImGui::BeginChild("##freeskills", ImVec2(0, 0));
        for (const Entry* e : selectableSkills(content)) {
            const bool fromProf = std::ranges::contains(cr.professionSkills, e->title);
            const bool on = fromProf || std::ranges::contains(cr.freeSkills, e->title);
            ImGui::BeginDisabled(fromProf || (!on && static_cast<int>(cr.freeSkills.size()) >= wanted));
            bool v = on;
            char label[96];
            std::snprintf(label, sizeof label, "%s (%s, level %d)##f%s", e->title.c_str(), e->prop("attribute").c_str(), levelOf(e->title), e->key.c_str());
            if (ImGui::Checkbox(label, &v)) {
                if (v) cr.freeSkills.push_back(e->title);
                else std::erase(cr.freeSkills, e->title);
            }
            ImGui::EndDisabled();
            entryTooltip(e);
        }
        ImGui::EndChild();
        ImGui::Columns(1);
    }

    void stepAbility() {
        Creation& cr = wz_.cr;
        const ContentStore& content = host_.content();
        const Entry* prof = content.entry(Kind::Profession, content.idByKey(Kind::Profession, cr.professionKey));
        const Entry* kin = content.entry(Kind::Kin, content.idByKey(Kind::Kin, cr.kinKey));
        if (!prof) return;
        if (kin && !kin->list("innate_abilities").empty()) {
            ImGui::TextColored(kGold, "Innate ability (from your kin)");
            for (const std::string& n : kin->list("innate_abilities")) {
                const Entry* a = content.findByName(Kind::Ability, n);
                ImGui::BulletText("%s%s", n.c_str(), a && !a->prop("wp_cost").empty() ? ("  (WP " + a->prop("wp_cost") + ")").c_str() : "");
                entryTooltip(a);
            }
            ImGui::Spacing();
        }
        if (!prof->list("schools").empty()) {
            const int nSpells = std::atoi(prof->prop("magic.spells").c_str()), nTricks = std::atoi(prof->prop("magic.tricks").c_str());
            ImGui::TextWrapped("A mage gets no heroic ability at the start; the magic makes up for it. Choose %d rank %s spells and %d magic tricks from your school (%s) or General Magic.",
                               nSpells, prof->prop("magic.spell_rank").c_str(), nTricks, cr.school.c_str());
            for (bool tricks : {false, true}) {
                const int limit = tricks ? nTricks : nSpells;
                int have = 0;
                for (const std::string& k : cr.spells)
                    if (const Entry* e = content.entry(Kind::Spell, content.idByKey(Kind::Spell, k))) have += (e->prop("trick") == "1") == tricks;
                ImGui::TextColored(kAccent, "%s: %d of %d", tricks ? "Magic tricks" : "Spells", have, limit);
                for (const Entry* e : magicChoices(content, *prof, cr.school, tricks)) {
                    const bool on = std::ranges::contains(cr.spells, e->key);
                    ImGui::BeginDisabled(!on && have >= limit);
                    bool v = on;
                    char label[128];
                    std::snprintf(label, sizeof label, "%s  (%s)##%s", e->title.c_str(), e->prop("school").c_str(), e->key.c_str());
                    if (ImGui::Checkbox(label, &v)) {
                        if (v) cr.spells.push_back(e->key);
                        else std::erase(cr.spells, e->key);
                    }
                    ImGui::EndDisabled();
                    entryTooltip(e);
                }
            }
            return;
        }
        const auto& heroics = prof->list("heroic_abilities");
        ImGui::TextWrapped("Your profession gives you a heroic ability. Requirements do not apply to your starting ability.");
        if (heroics.size() == 1) cr.heroicAbility = heroics[0];
        for (const std::string& h : heroics) {
            const Entry* a = content.findByName(Kind::Ability, h);
            if (choiceRow(h, a ? firstSentence(a->body, 140) : "", cr.heroicAbility == h)) cr.heroicAbility = h;
        }
        ImGui::Spacing();
        ImGui::TextColored(kGrey, "Or, with the GM's permission, another heroic ability:");
        ImGui::SetNextItemWidth(U(260));
        if (ImGui::BeginCombo("##otherheroic", !std::ranges::contains(heroics, cr.heroicAbility) && !cr.heroicAbility.empty() ? cr.heroicAbility.c_str() : "Choose…")) {
            for (const Entry& a : content.entries(Kind::Ability))
                if (a.prop("type") == "heroic" && ImGui::Selectable(a.title.c_str(), cr.heroicAbility == a.title)) cr.heroicAbility = a.title;
            ImGui::EndCombo();
        }
        if (const Entry* a = content.findByName(Kind::Ability, cr.heroicAbility)) {
            ImGui::Separator();
            ImGui::TextColored(kAccent, "%s", a->title.c_str());
            fieldsTable(a->fields, "##abfields");
            paragraphs(a->body);
        }
    }

    void stepGear() {
        Creation& cr = wz_.cr;
        const ContentStore& content = host_.content();
        const Entry* prof = content.entry(Kind::Profession, content.idByKey(Kind::Profession, cr.professionKey));
        if (!prof || prof->list("starting_gear").empty()) {
            ImGui::TextWrapped("This profession lists no starting gear. You can add equipment on the sheet afterwards.");
            return;
        }
        ImGui::TextWrapped("Pick one of the three starting sets of your profession, or roll for it. Dice in a set are rolled for you. Where a set offers a choice (A/B/C), pick one.");
        if (ImGui::Button("Roll D6")) {
            const int r = host_.dice().roll(6);
            chooseGearSet(cr, *prof, gearSetForRoll(r), host_.dice());
            wz_.note = "Rolled " + std::to_string(r) + ": set " + std::to_string(cr.gearSet + 1);
        }
        const auto& sets = prof->list("starting_gear");
        for (size_t i = 0; i < sets.size(); ++i)
            if (choiceRow("Set " + std::to_string(i + 1), sets[i], cr.gearSet == static_cast<int>(i))) chooseGearSet(cr, *prof, static_cast<int>(i), host_.dice());
        if (cr.gearSet >= 0) {
            ImGui::Separator();
            ImGui::TextColored(kAccent, "Your gear");
            for (size_t i = 0; i < cr.gear.size(); ++i) {
                GearPick& p = cr.gear[i];
                ImGui::PushID(static_cast<int>(i));
                if (p.diceSides > 0) {
                    ImGui::Text("%d %s", p.rolled, p.what.c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(kGrey, "(D%d)", p.diceSides);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reroll")) p.rolled = host_.dice().roll(p.diceSides);
                } else if (p.options.size() > 1) {
                    ImGui::SetNextItemWidth(U(280));
                    if (ImGui::BeginCombo("##opt", p.options[static_cast<size_t>(p.choice)].c_str())) {
                        for (size_t o = 0; o < p.options.size(); ++o)
                            if (ImGui::Selectable(p.options[o].c_str(), p.choice == static_cast<int>(o))) p.choice = static_cast<int>(o);
                        ImGui::EndCombo();
                    }
                } else {
                    ImGui::BulletText("%s", p.options[0].c_str());
                }
                ImGui::PopID();
            }
        }
    }

    // Buttons for the typical names and a roll button for a table of the pack (weakness, memento, appearance).
    void rollField(const char* role, const char* label, std::string& field) {
        ImGui::TextColored(kGold, "%s", label);
        ImGui::SameLine();
        for (const DataTable* t : cs().tablesByRole(role)) {
            ImGui::PushID(t->key.c_str());
            if (ImGui::SmallButton((std::string("Roll ") + (t->dice.empty() ? "" : t->dice) + (cs().tablesByRole(role).size() > 1 ? " · " + t->title : "")).c_str()))
                field = rollTable(*t, host_.dice());
            ImGui::PopID();
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("Clear")) field.clear();
        inputStr((std::string("##") + role).c_str(), field);
    }

    void stepDetails() {
        Creation& cr = wz_.cr;
        const ContentStore& content = host_.content();
        const Entry* kin = content.entry(Kind::Kin, content.idByKey(Kind::Kin, cr.kinKey));
        const Entry* prof = content.entry(Kind::Profession, content.idByKey(Kind::Profession, cr.professionKey));
        ImGui::TextColored(kGold, "Name");
        inputStr("##name", cr.name, U(300), "Name");
        if (kin && !kin->list("names").empty()) {
            ImGui::TextColored(kGrey, "Typical %s names (click one or roll D6):", kin->title.c_str());
            for (const std::string& n : kin->list("names")) {
                ImGui::PushID(n.c_str());
                if (ImGui::SmallButton(n.c_str())) cr.name = n;
                ImGui::PopID();
                ImGui::SameLine();
            }
            if (ImGui::SmallButton("Roll")) cr.name = kin->list("names")[static_cast<size_t>(host_.dice().roll(static_cast<int>(kin->list("names").size())) - 1)];
            ImGui::NewLine();
        }
        ImGui::Spacing();
        ImGui::TextColored(kGold, "Nickname");
        ImGui::SameLine();
        ImGui::TextColored(kGrey, "(optional)");
        inputStr("##nick", cr.nickname, U(300), "Nickname");
        if (prof && !prof->list("nicknames").empty()) {
            for (const std::string& n : prof->list("nicknames")) {
                ImGui::PushID(n.c_str());
                if (ImGui::SmallButton(n.c_str())) cr.nickname = n;
                ImGui::PopID();
                ImGui::SameLine();
            }
            if (ImGui::SmallButton("Roll")) cr.nickname = prof->list("nicknames")[static_cast<size_t>(host_.dice().roll(static_cast<int>(prof->list("nicknames").size())) - 1)];
            ImGui::NewLine();
        }
        ImGui::Spacing();
        ImGui::TextColored(kGold, "Player");
        ImGui::SameLine();
        ImGui::TextColored(kGrey, "(optional)");
        inputStr("##player", cr.player, U(300), "Who plays this character");
        ImGui::Spacing();
        rollField("weakness", "Weakness (optional)", cr.weakness);
        ImGui::Spacing();
        rollField("memento", "Memento (optional)", cr.memento);
        ImGui::Spacing();
        rollField("appearance", "Appearance", cr.appearance);
    }

    void stepReview() {
        const ContentStore& content = host_.content();
        const auto problems = validateCreation(wz_.cr, content);
        if (!problems.empty()) {
            ImGui::TextColored(kRed, "Not finished yet:");
            for (const std::string& p : problems) ImGui::BulletText("%s", p.c_str());
            return;
        }
        Character preview = buildCharacter(wz_.cr, content, host_.dice());
        ImGui::TextColored(kGrey, "This is how the sheet will start. You can change anything on it afterwards.");
        ImGui::Separator();
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(preview.summary().c_str());
        ImGui::PopTextWrapPos();
    }

    // ---------------------------------------------------------------------------------------------- state
    std::string selId_;
    CharacterSheet sheet_;
    std::string sheetFor_;                                 // whose sheet is on show, so its edit state can be dropped when it changes
    bool wizard_ = false;
    Wizard wz_;
    std::map<std::string, DataTable> bookTables_;
    FileDialog dialog_;
    Action dialogAction_ = Action::None;
    std::string exportId_;
};

}  // namespace

std::unique_ptr<Module> makeCharactersModule(Host& host) { return std::make_unique<CharactersModule>(host); }

}  // namespace gm
