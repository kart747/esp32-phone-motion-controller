// controller - the single owner of user-facing configuration.
//
// The processing task is the ONLY writer of sport mode, the swing-output
// switch and the tunables. Serial, the physical buttons and (Phase 7) the
// dashboard all submit Commands, which are applied here in one place. That
// removes the data race where the serial task used to write settings while the
// processing task read them mid-swing, and it gives every caller the same
// validation and the same reply.
//
// Hardware-free so the whole command surface can be unit-tested on the laptop.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gestures.h"
#include "tunables.h"

enum class CommandKind : uint8_t {
    Recalibrate = 0,
    ToggleSport,       // tennis <-> cricket
    SetSport,
    ToggleSwingOutput, // enable/disable the click on recognised swings
    SetSwingOutput,
    SetTunable,        // key + value
    ResetTunables,
    RequestStats,
    RequestTunables,
    SaveSettings,      // persist the current settings
};

constexpr size_t COMMAND_KEY_LEN = 16;

struct Command {
    CommandKind kind = CommandKind::RequestStats;
    Sport       sport = Sport::Tennis;
    bool        flag = false;
    float       value = 0.0f;
    char        key[COMMAND_KEY_LEN] = {};
};

// Builders keep the call sites readable and the key copy safe (a queued command
// must not borrow a pointer into a task's line buffer).
Command cmdRecalibrate();
Command cmdToggleSport();
Command cmdSetSport(Sport s);
Command cmdToggleSwingOutput();
Command cmdSetSwingOutput(bool on);
Command cmdSetTunable(const char *key, float value);
Command cmdResetTunables();
Command cmdRequestStats();
Command cmdRequestTunables();
Command cmdSaveSettings();

enum class CommandResult : uint8_t {
    Ok = 0,
    InvalidKey,
    InvalidValue,
    InvalidSnapshot,
    NoChange,
};

const char *commandResultText(CommandResult r);

// What the rest of the firmware may read (never write).
struct ControlState {
    Sport    sport = Sport::Tennis;
    bool     swingOutputEnabled = true;
    Tunables tunables = Tunables::defaults();
};

class Controller {
public:
    void begin();

    // Applies a command. On rejection NOTHING in the state changes.
    CommandResult apply(const Command &cmd);

    const ControlState &state() const { return state_; }

    // True when this command should make the caller drop any in-progress swing
    // capture and cancel a pending click (settings changed underneath it).
    static bool invalidatesSwing(CommandKind kind);

private:
    ControlState state_;
};