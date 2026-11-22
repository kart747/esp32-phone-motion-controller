// dashboard - HTTP glue between WebServer and the pure api_handler.
//
// All routing, authentication and validation live in lib/motion/api_handler.cpp
// (unit-tested on the laptop). This file only moves bytes and turns an applied
// settings change into Commands for the process task, so the processing task
// stays the single owner of configuration.
//
// The web task owns this object: it copies the latest published snapshot in
// with poll(), which then services any pending request from those copies. The
// process task never touches this state, so no lock is needed here.
#pragma once

#include <stdint.h>

#include "api_handler.h"
#include "controller.h"
#include "settings_store.h"

class Dashboard {
public:
    using SubmitFn = bool (*)(const Command &);

    bool begin(SubmitFn submit);

    // Service pending HTTP traffic against a copy of the current state.
    void poll(const StatusSnapshot &status, const SavedSettings &settings, bool savedDirty);

private:
    SubmitFn     submit_ = nullptr;
    // Copies held for the duration of one poll(); only the web task touches them.
    StatusSnapshot status_;
    SavedSettings  settings_;
    bool           savedDirty_ = true;
};