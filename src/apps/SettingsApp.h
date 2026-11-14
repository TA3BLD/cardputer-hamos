#pragma once
#include <string>
#include <vector>
#include "core/App.h"
#include "core/AprsIs.h"
#include "core/Config.h"
#include "core/Settings.h"
#include "core/LoraAprs.h"
#include "core/Mesh.h"
#include "core/Rig.h"
#include "core/Net.h"
#include "core/Ui.h"
#include "core/Web.h"
#include "core/Wsjtx.h"

// Edits the values in /HamOS/config.txt.
class SettingsApp : public App {
public:
    const char* name() const override { return "Settings"; }
    uint16_t color() const override { return 0x8410; }

    void onEnter() override {
        editing_ = false;
        status_.clear();
    }

    bool onBack() override {
        if (!editing_) return false;
        editing_ = false;   // cancel edit, stay in Settings
        dirty = true;
        return true;
    }

    void onKey(const KeyEvent& k) override {
        const Field& f = fields()[selected_];
        if (editing_) {
            if (k.enter) {
                if (input_.value.empty()) config::erase(f.key);     // empty = back to the default
                else config::set(f.key, input_.value);
                status_ = config::save() ? "Saved" : "No SD card: not saved";
                settings::apply(f.apply);
                editing_ = false;
            } else if (!input_.handle(k)) {
                return;
            }
            dirty = true;
        } else if (k.enter) {
            input_ = ui::TextInput();
            input_.value = config::get(f.key, f.def);
            input_.maxLen = f.maxLen;
            input_.upper = f.upper;
            editing_ = true;
            status_.clear();
            dirty = true;
        } else if (ui::listNav(k, selected_, (int)fields().size())) {
            dirty = true;
        }
    }

    void draw() override {
        if (editing_) {
            ui::clearBody();
            input_.draw(ui::BODY_Y + 20, fields()[selected_].label);
            ui::footer("ENTER save   ` cancel");
            return;
        }
        std::vector<std::string> rows;
        for (const Field& f : fields()) {
            std::string v = config::get(f.key, f.def);
            if (f.secret && !v.empty()) v = "********";
            rows.push_back(std::string(f.label) + ": " + (v.empty() ? "-" : v));
        }
        ui::list(rows, selected_, scroll_);
        std::string hint = !status_.empty() ? status_ : web::running() ? web::address() : "ENTER edit   ` back";
        ui::footer(hint.c_str());
    }

private:
    typedef settings::Field Field;
    static const std::vector<Field>& fields() { return settings::fields(); }

    int selected_ = 0, scroll_ = 0;
    bool editing_ = false;
    ui::TextInput input_;
    std::string status_;
};
