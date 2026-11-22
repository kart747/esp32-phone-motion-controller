#include "controller.h"

#include <string.h>

namespace {

Command makeCommand(CommandKind kind) {
    Command c;
    c.kind = kind;
    return c;
}

// Copy a key into the fixed field, refusing anything too long to be a real key.
void setKey(Command &c, const char *key) {
    if (!key) return;
    strncpy(c.key, key, COMMAND_KEY_LEN - 1);
    c.key[COMMAND_KEY_LEN - 1] = '\0';
}

}  // namespace

Command cmdRecalibrate() { return makeCommand(CommandKind::Recalibrate); }

Command cmdToggleSport() { return makeCommand(CommandKind::ToggleSport); }

Command cmdSetSport(Sport s) {
    Command c = makeCommand(CommandKind::SetSport);
    c.sport = s;
    return c;
}

Command cmdToggleSwingOutput() { return makeCommand(CommandKind::ToggleSwingOutput); }

Command cmdSetSwingOutput(bool on) {
    Command c = makeCommand(CommandKind::SetSwingOutput);
    c.flag = on;
    return c;
}

Command cmdSetTunable(const char *key, float value) {
    Command c = makeCommand(CommandKind::SetTunable);
    setKey(c, key);
    c.value = value;
    return c;
}

Command cmdResetTunables() { return makeCommand(CommandKind::ResetTunables); }

Command cmdRequestStats() { return makeCommand(CommandKind::RequestStats); }

Command cmdRequestTunables() { return makeCommand(CommandKind::RequestTunables); }

Command cmdSaveSettings() { return makeCommand(CommandKind::SaveSettings); }

const char *commandResultText(CommandResult r) {
    switch (r) {
        case CommandResult::Ok:              return "ok";
        case CommandResult::InvalidKey:      return "unknown key";
        case CommandResult::InvalidValue:    return "value out of range";
        case CommandResult::InvalidSnapshot: return "settings combination rejected";
        case CommandResult::NoChange:        return "no change";
    }
    return "?";
}

void Controller::begin() { state_ = ControlState{}; }

bool Controller::invalidatesSwing(CommandKind kind) {
    switch (kind) {
        case CommandKind::Recalibrate:
        case CommandKind::ToggleSport:
        case CommandKind::SetSport:
        case CommandKind::SetTunable:
        case CommandKind::ResetTunables:
        case CommandKind::SetSwingOutput:
        case CommandKind::ToggleSwingOutput:
            return true;
        case CommandKind::RequestStats:
        case CommandKind::RequestTunables:
        case CommandKind::SaveSettings:
            return false;
    }
    return false;
}

CommandResult Controller::apply(const Command &cmd) {
    switch (cmd.kind) {
        case CommandKind::Recalibrate:
        case CommandKind::RequestStats:
        case CommandKind::RequestTunables:
        case CommandKind::SaveSettings:
            // Handled by the caller (it owns the pipeline, the log and NVS); the
            // controller has no state to change.
            return CommandResult::Ok;

        case CommandKind::ToggleSport: {
            const Sport next = (state_.sport == Sport::Tennis) ? Sport::Cricket : Sport::Tennis;
            state_.sport = next;
            return CommandResult::Ok;
        }

        case CommandKind::SetSport:
            if (cmd.sport != Sport::Tennis && cmd.sport != Sport::Cricket) {
                return CommandResult::InvalidValue;
            }
            if (cmd.sport == state_.sport) return CommandResult::NoChange;
            state_.sport = cmd.sport;
            return CommandResult::Ok;

        case CommandKind::ToggleSwingOutput:
            state_.swingOutputEnabled = !state_.swingOutputEnabled;
            return CommandResult::Ok;

        case CommandKind::SetSwingOutput:
            if (cmd.flag == state_.swingOutputEnabled) return CommandResult::NoChange;
            state_.swingOutputEnabled = cmd.flag;
            return CommandResult::Ok;

        case CommandKind::SetTunable: {
            // Stage the change on a copy so a rejection cannot half-apply it,
            // then validate the WHOLE snapshot (cross-field rules included).
            Tunables candidate = state_.tunables;
            TunableError terr = TunableError::None;
            if (!tunablesSet(candidate, cmd.key, cmd.value, &terr)) {
                return (terr == TunableError::UnknownKey) ? CommandResult::InvalidKey
                                                          : CommandResult::InvalidValue;
            }
            if (!applyValidated(state_.tunables, candidate)) {
                return CommandResult::InvalidSnapshot;
            }
            return CommandResult::Ok;
        }

        case CommandKind::ResetTunables: {
            const Tunables def = Tunables::defaults();
            // Defaults come from config.h and must pass their own validation.
            if (!applyValidated(state_.tunables, def)) return CommandResult::InvalidSnapshot;
            return CommandResult::Ok;
        }
    }
    return CommandResult::InvalidValue;
}