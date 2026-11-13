#pragma once
#include <vector>
#include "core/App.h"
#include "core/Ui.h"

// Home screen: lists the registered apps. main.cpp reads `launch` after a key.
class Launcher : public App {
public:
    explicit Launcher(const std::vector<App*>& apps) : apps_(apps) {}
    const char* name() const override { return "HamOS"; }

    void onKey(const KeyEvent& k) override {
        if (ui::listNav(k, selected_, (int)apps_.size())) dirty = true;
        if (k.enter && !apps_.empty()) launch = apps_[selected_];
    }

    void draw() override {
        std::vector<std::string> names;
        for (App* a : apps_) names.push_back(a->name());
        ui::list(names, selected_, scroll_);
        ui::footer(";/. select   ENTER open");
    }

    App* launch = nullptr;

private:
    const std::vector<App*>& apps_;
    int selected_ = 0, scroll_ = 0;
};
