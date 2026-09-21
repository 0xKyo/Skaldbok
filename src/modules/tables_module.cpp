// Tables: every table of the books (with a roll button for the dice ones) and the tables of homebrew packs.
#include <cstdio>

#include <imgui.h>

#include "modules/modules.h"
#include "ui_common.h"

namespace gm {
namespace {

using namespace ui;

class TablesModule : public Module {
public:
    explicit TablesModule(Host& host) : Module(host) { onContentChanged(); }

    const char* id() const override { return "tables"; }
    const char* title() const override { return "Tables"; }
    const char* summary() const override { return "The books' random and reference tables, and those of homebrew packs, with a roll button."; }
    const char* group() const override { return "Reference"; }
    int badge() const override { return static_cast<int>(list_.all.size()); }
    bool handles(Kind k) const override { return k == Kind::Table; }

    bool findByName(const std::string& want, Selection& out) const override {
        for (const ListItem& it : list_.all)
            if (lowered(it.name) == want) {
                out = {Kind::Table, it.id};
                return true;
            }
        return false;
    }

    void onContentChanged() override {
        std::vector<ListItem> items = host_.db().listTables();
        for (const DataTable& t : host_.content().packTables()) {
            if (!t.browse) continue;
            ListItem it;
            it.id = t.id;
            it.kind = Kind::Table;
            it.name = t.title;
            it.sub = (t.dice.empty() ? "" : t.dice + " · ") + std::to_string(t.rows.size()) + " rows";
            it.sourceId = t.sourceId;
            items.push_back(std::move(it));
        }
        list_.set(std::move(items));
        loaded_ = 0;
    }

    void onSelect(const Selection& s) override {
        list_.ensureVisible(host_, s);
        load(s.id);
    }

    void drawList() override {
        if (const ListItem* it = list_.draw(host_, "Filter tables…", host_.selection())) host_.goTo(Kind::Table, it->id);
    }

    void drawSelection(const Selection& s) override {
        if (loaded_ != s.id) load(s.id);
        if (!ok_) return;
        detailHeader(host_, table_.title, table_.dice.empty() ? "table" : table_.dice + " table", table_.sourceId, table_.ref, table_.pageNote);
        const int sides = table_.dieSides();
        if (sides > 0) {
            char label[32];
            std::snprintf(label, sizeof label, "Roll %s", table_.dice.c_str());
            if (ImGui::Button(label)) {
                lastRoll_ = host_.dice().roll(sides);
                rolledRow_ = table_.rowForRoll(lastRoll_);
                host_.flashRoll();
            }
            if (lastRoll_ > 0) {
                ImGui::SameLine();
                ImGui::TextColored(kGold, "Rolled %d", lastRoll_);
            }
        }
        ImGui::Spacing();
        tableGrid(table_, rolledRow_);
    }

private:
    void load(int id) {
        loaded_ = id;
        rolledRow_ = -1;
        lastRoll_ = 0;
        if (id >= kPackTableBase) {
            const DataTable* t = host_.content().packTable(id);
            ok_ = t != nullptr;
            if (t) table_ = *t;
        } else {
            ok_ = host_.db().table(id, table_);
        }
    }

    ListView list_;
    DataTable table_;
    bool ok_ = false;
    int loaded_ = 0;
    int rolledRow_ = -1, lastRoll_ = 0;
};

}  // namespace

std::unique_ptr<Module> makeTablesModule(Host& host) { return std::make_unique<TablesModule>(host); }

}  // namespace gm
