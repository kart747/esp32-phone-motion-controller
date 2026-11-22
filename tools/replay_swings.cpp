// replay_swings - run a recording from tools/serial_logger.py through the REAL
// parser, orientation pipeline and gesture detector, on the laptop.
//
// Why C++: re-implementing the classifier in Python would test a copy, not the
// firmware. This links the same lib/motion sources the ESP32 builds, so a
// threshold change is evaluated against recorded data without reflashing.
//
// What it does NOT exercise: the UDP transport, the BLE adapters and task
// scheduling. Sample timing comes from the recording.
//
// The recording carries the tunables that were in force (`T,key=value`
// records), so a replay reproduces the device's settings instead of silently
// using the compiled-in defaults. The swings file carries the HUMAN label the
// operator supplied (`--label`), which is what accuracy must be measured
// against; the firmware's own verdict is kept only for comparison.
//
// Build (from the repo root); these are the sources the ESP32 builds:
//   g++ -std=c++17 -O2 -I firmware/include -I firmware/lib/motion
//       tools/replay_swings.cpp
//       firmware/lib/motion/parser.cpp firmware/lib/motion/pipeline.cpp
//       firmware/lib/motion/imu_filter.cpp firmware/lib/motion/sample_clock.cpp
//       firmware/lib/motion/gestures.cpp firmware/lib/motion/tunables.cpp
//       -o /tmp/replay_swings
//
// Usage:
//   /tmp/replay_swings data/<session>_samples.csv data/<session>_swings.csv [key=value ...]
//
// Trailing key=value arguments (tunable names as used by the serial `set`
// command, e.g. max_tilt=75) override the recorded settings, so a threshold
// can be tried against old recordings without reflashing.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "config.h"
#include "gestures.h"
#include "parser.h"
#include "pipeline.h"
#include "tunables.h"

namespace {

// The recorded S row carries already-parsed values. Rebuild the phone's own CSV
// line (six columns, no timestamp) so the real parser runs on it.
std::string toPhoneCsv(double ax, double ay, double az, double gx, double gy, double gz) {
    char buf[192];
    snprintf(buf, sizeof(buf), "%.6g,%.6g,%.6g,%.6g,%.6g,%.6g", ax, ay, az, gx, gy, gz);
    return buf;
}

std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ',') { out.push_back(cur); cur.clear(); }
        else if (c != '\r' && c != '\n') cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <samples.csv> [swings.csv] [key=value ...]\n", argv[0]);
        return 2;
    }

    // ---- expected results, from the operator's labels
    std::vector<std::string> expectLabel;    // human label, e.g. "forehand"
    std::vector<std::string> deviceGesture;  // what the device said at record time
    std::string recordedSport = "tennis";
    if (argc > 2 && !strchr(argv[2], '=')) {
        FILE *ex = fopen(argv[2], "r");
        if (ex) {
            char line[512];
            bool first = true;
            while (fgets(line, sizeof(line), ex)) {
                if (first) { first = false; continue; }  // header
                const auto f = split(line);
                // label,session,<W fields...>  where gesture/reason are last
                if (f.size() >= 12) {
                    expectLabel.push_back(f[0]);
                    deviceGesture.push_back(f[11]);
                }
            }
            fclose(ex);
        }
    }

    // ---- tunables in force during the recording
    Tunables tun = Tunables::defaults();
    long tunableRecords = 0;
    // A second pass over the samples is not needed: the T rows appear before
    // the swing data, so scan them first.
    {
        FILE *pre = fopen(argv[1], "r");
        if (!pre) { perror(argv[1]); return 1; }
        char line[512];
        bool first = true;
        while (fgets(line, sizeof(line), pre)) {
            if (first) { first = false; continue; }
            if (line[0] != 'T') continue;
            // T,<key>=<value>  (already stripped of the leading "T," by the logger)
            const auto f = split(line);
            if (f.size() < 2) continue;
            const std::string kv = f[1];
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = kv.substr(0, eq);
            const std::string val = kv.substr(eq + 1);
            if (key == "sport") { recordedSport = val; continue; }
            const float v = strtof(val.c_str(), nullptr);
            TunableError terr = TunableError::None;
            if (tunablesSet(tun, key.c_str(), v, &terr)) tunableRecords++;
        }
        fclose(pre);
    }
    for (int i = 2; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        if (!eq) continue;
        const std::string key(argv[i], static_cast<size_t>(eq - argv[i]));
        TunableError terr = TunableError::None;
        if (!tunablesSet(tun, key.c_str(), strtof(eq + 1, nullptr), &terr)) {
            fprintf(stderr, "override %s rejected: %s\n", argv[i], tunableErrorText(terr));
            return 2;
        }
        printf("override: %s\n", argv[i]);
    }
    const TunableError tunErr = validate(tun);
    if (tunErr != TunableError::None) {
        printf("WARNING: recorded tunables are inconsistent (%s); using config defaults\n",
               tunableErrorText(tunErr));
        tun = Tunables::defaults();
    }

    FILE *in = fopen(argv[1], "r");
    if (!in) { perror(argv[1]); return 1; }

    const CsvLayout layout = CsvLayout::fromConfig();
    OrientationPipeline pipe;
    pipe.begin();
    GestureDetector det;
    det.setSport(!strcmp(recordedSport.c_str(), "cricket") ? Sport::Cricket : Sport::Tennis);
    PipelineOutput out;
    SwingFeatures swing;

    char line[512];
    long sampleRows = 0, rejected = 0, swings = 0, hits = 0, calibratingSamples = 0;
    long match = 0, miss = 0, extra = 0;
    float worstDt = 0.0f;
    std::map<std::string, int> tally;

    bool header = true;
    while (fgets(line, sizeof(line), in)) {
        if (header) { header = false; continue; }  // label,session,rx_ms,...
        const auto f = split(line);
        if (f.size() < 15) continue;
        if (f[0] == "T") continue;                 // tunable rows already handled

        ImuSample s;
        s.hasTimestamp = false;                    // the phone sends none
        s.rxMs = static_cast<uint32_t>(strtoul(f[2].c_str(), nullptr, 10));
        const std::string csv = toPhoneCsv(atof(f[3].c_str()), atof(f[4].c_str()),
                                           atof(f[5].c_str()), atof(f[6].c_str()),
                                           atof(f[7].c_str()), atof(f[8].c_str()));
        if (parseCsvLine(csv.c_str(), csv.size(), layout, s) != ParseStatus::Ok) {
            rejected++;
            continue;
        }
        sampleRows++;
        pipe.feed(s, out);
        if (out.calibrating) { calibratingSamples++; det.reset(); continue; }
        if (out.dtS > worstDt) worstDt = out.dtS;

        GestureInput gi;
        gi.gyro = out.gyro;
        gi.accel = s.accel;
        gi.up = out.up;
        gi.gravityMag = out.gravityMag;
        gi.pitchDeg = out.pitchDeg;
        gi.rollDeg = out.rollDeg;
        gi.dt = out.dtS;
        gi.nowMs = s.rxMs;

        if (out.timingGap) det.reset();
        if (det.update(gi, tun, swing)) {
            const std::string got = gestureName(swing.gesture);
            tally[got]++;
            const bool hit = swing.gesture != Gesture::None;
            if (hit) hits++;

            // Accuracy is against the operator's label, not the device's own
            // earlier verdict. A "still" or "noise" session expects no stroke.
            const size_t i = static_cast<size_t>(swings);
            const std::string want = i < expectLabel.size() ? expectLabel[i] : "";
            const bool expectStroke = !want.empty() && want != "still" && want != "idle" &&
                                      want != "noise" && want != "unlabelled";
            swings++;

            const char *verdict = "";
            if (want.empty()) {
                verdict = "";                      // no reference for this event
            } else if (!expectStroke) {
                // A false activation on a "no stroke" session is a real error.
                verdict = hit ? "FALSE-POSITIVE" : "ok(none)";
                if (hit) miss++;
            } else if (got == want) {
                verdict = "MATCH";
                match++;
            } else {
                verdict = "MISS";
                miss++;
            }
            printf("%4ld  got=%-9s label=%-9s device@record=%-9s peak=%5.1f "
                   "ints=[%+.2f %+.2f %+.2f] %s\n",
                   swings, got.c_str(), want.empty() ? "-" : want.c_str(),
                   i < deviceGesture.size() ? deviceGesture[i].c_str() : "-", swing.peakGyro,
                   swing.intYaw, swing.intPitch, swing.intRoll, verdict);
        }
    }
    fclose(in);

    // Strokes the operator made that the detector never reported at all.
    for (size_t i = static_cast<size_t>(swings); i < expectLabel.size(); i++) {
        const std::string want = expectLabel[i];
        if (want.empty() || want == "still" || want == "idle" || want == "noise") continue;
        extra++;
        printf("     MISSED: label=%s produced no event\n", want.c_str());
    }

    printf("\n--- summary ---\n");
    printf("sport=%s tunables=%ld record(s)\n", recordedSport.c_str(), tunableRecords);
    printf("samples=%ld rejected=%ld calibrating=%ld\n", sampleRows, rejected, calibratingSamples);
    printf("swings=%ld recognised=%ld\n", swings, hits);
    if (!expectLabel.empty()) {
        printf("against human labels: match=%ld miss=%ld never-detected=%ld\n", match, miss, extra);
    }
    printf("worst sample interval: %.3f s (DT_MAX_S = %.2f)\n", worstDt, DT_MAX_S);
    for (const auto &kv : tally) printf("  %-9s %d\n", kv.first.c_str(), kv.second);
    return 0;
}