#include "device_controls.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

using Block = std::array<opxy::i32, 2 * opxy::block_frames>;
constexpr auto full = opxy::q31::maximum;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

double level(double hz, double amplitude, const ott::Controls& controls) {
    ott::Processor processor;
    Block block;
    double power = 0;
    unsigned measured = 0;
    for (int n = 0; n < 256; ++n) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            const auto frame = n * opxy::block_frames + i;
            const auto sample = static_cast<opxy::i32>(
                full * amplitude * std::sin(6.283185307179586 * hz * frame / 44100.0));
            block[2 * i] = sample;
            block[2 * i + 1] = sample / 2;
        }
        processor.process(block.data(), controls);
        if (n >= 192) {
            for (unsigned i = 0; i < opxy::block_frames; ++i) {
                const double left = block[2 * i], right = block[2 * i + 1];
                require(std::abs(left - 2 * right) < .0001 * full, "Stereo gain must stay linked");
                power += left * left;
                ++measured;
            }
        }
    }
    return std::sqrt(power / measured) / full;
}

void test_track_inserts(const Block& input) {
    ott::TrackInserts inserts;
    ott::TrackSettings settings[ott::TrackInserts::track_count];
    const auto configure = [&] {
        require(inserts.configure(settings), "Valid settings must be accepted");
    };
    settings[0].enabled = true;
    settings[0].controls.time = full;
    settings[0].controls.upward = full;
    settings[0].controls.downward = full;
    settings[0].controls.level_match = 0;
    configure();
    Block track_zero, track_seven, silence{};

    for (unsigned block = 0; block < 24; ++block) {
        track_zero = input;
        require(inserts.process(0, track_zero.data(), 4) == 4,
                "An active insert must preserve a nonzero voice count");
    }
    require(track_zero != input, "An active track must be processed");
    track_seven = input;
    require(inserts.process(7, track_seven.data(), 3) == 3 && track_seven == input,
            "A disabled track must pass through");
    settings[7] = settings[0];
    configure();
    track_seven = silence;
    require(inserts.process(7, track_seven.data(), 0) == 0 && track_seven == silence,
            "One track must not inherit a different track's history");
    track_seven = input;
    inserts.process(7, track_seven.data(), 1);
    Block first_block = track_seven;

    settings[0].enabled = false;
    configure();
    settings[0].enabled = true;
    configure();
    track_zero = input;
    inserts.process(0, track_zero.data(), 1);
    require(track_zero == first_block, "Re-enabling an insert must start from fresh state");

    bool tail = false;
    opxy::u32 mixed = 0;
    for (int block = 0; block < 2048; ++block) {
        track_zero = silence;
        mixed = inserts.process(0, track_zero.data(), 0);
        tail = tail || mixed != 0;
    }
    require(tail && mixed == 0 && track_zero == silence,
            "A crossover tail must remain audible and eventually reach exact silence");
    track_zero = input;
    inserts.process(0, track_zero.data(), 1);
    require(track_zero == first_block, "A settled insert must wake from fresh state");

    for (unsigned index : {2u, 5u}) settings[index] = settings[0];
    configure();
    settings[6] = settings[0];
    settings[0].controls.upward = 0;
    require(!inserts.configure(settings), "A fifth enabled insert must be rejected");
    track_seven = input;
    require(inserts.process(6, track_seven.data(), 2) == 2 && track_seven == input,
            "Rejecting a configuration must leave the fifth track bypassed");
    inserts.reset();
    track_zero = input;
    inserts.process(0, track_zero.data(), 1);
    require(track_zero == first_block,
            "Reset must clear audio history and preserve the last accepted settings");
    require(inserts.process(8, track_zero.data(), 2) == 2 && track_zero == first_block,
            "An invalid track must pass through");

    settings[0].enabled = false;
    configure();
    Block track_six = input;
    inserts.process(6, track_six.data(), 1);
    require(track_six == first_block, "Disabling one insert must free a slot for another track");
}

void test_device_controls(const Block& input) {
    ott::DeviceControls controller;
    ott::Panel panel{};
    using ott::InputResult;
    using ott::Page;
    controller.select(0, Page::other);
    require(controller.module(2, false) == InputResult::stock,
            "The first M2 press belongs to stock");
    controller.select(0, Page::envelope);
    require(controller.module(2, false) == InputResult::page_changed && controller.page() == Page::ott,
            "M2 on Envelope must open OTT");
    require(controller.panel(panel) && !panel.enabled && panel.track_number == 1 &&
            std::strcmp(panel.encoders[0].label, "Time") == 0 &&
            std::strcmp(panel.encoders[1].label, "Low") == 0 &&
            std::strcmp(panel.encoders[2].label, "High") == 0 &&
            std::strcmp(panel.encoders[3].label, "Depth") == 0, "Show the four main controls");
    require(controller.click(0, false) == InputResult::page_changed &&
            controller.panel(panel) && std::strcmp(panel.encoders[0].label, "Level Match") == 0,
            "Clicking encoder one must show Level Match");
    const auto before = controller.project().track[0].controls;
    require(controller.turn(0, false, -8, 1) == InputResult::settings_changed &&
            controller.project().track[0].controls.level_match < before.level_match &&
            controller.project().track[0].controls.time == before.time,
            "Level Match must have independent storage");
    require(controller.turn(0, true, 5, 0) == InputResult::settings_changed &&
            controller.click(3, true) == InputResult::handled,
            "Shift must stay out of the OTT toggle");
    require(controller.click(1, false) == InputResult::page_changed &&
            controller.click(2, false) == InputResult::page_changed &&
            controller.panel(panel) && std::strcmp(panel.encoders[0].label, "Level Match") == 0 &&
            std::strcmp(panel.encoders[1].label, "Mix") == 0 &&
            std::strcmp(panel.encoders[2].label, "Stereo") == 0 && !panel.enabled,
            "A refinement click must switch only its own encoder");
    require(panel.alternate[0] && panel.alternate[1] && panel.alternate[2] && !panel.alternate[3] &&
            std::strcmp(panel.primary[0].label, "Time") == 0 &&
            std::strcmp(panel.secondary[0].label, "Level Match") == 0,
            "Panel must expose each encoder's two rows and its independent highlight");
    require(controller.click(0, false) == InputResult::page_changed &&
            controller.click(2, false) == InputResult::page_changed &&
            controller.panel(panel) && std::strcmp(panel.encoders[0].label, "Time") == 0 &&
            std::strcmp(panel.encoders[2].label, "High") == 0,
            "A second click must return to the main control without opening another choice");
    require(controller.turn(1, true, 127, 3) == InputResult::settings_changed &&
            controller.project().track[0].controls.low_protect == full,
            "Large turns must clamp without overflow");
    require(controller.turn(3, false, -15, 0) == InputResult::settings_changed &&
            controller.panel(panel) && !panel.enabled && panel.encoders[3].value < full,
            "Turning white edits Depth while bypassed");
    const auto depth = panel.encoders[3].value;
    for (bool enabled : {true, false, true}) {
        require(controller.click(3, false) == InputResult::settings_changed &&
                controller.panel(panel) && panel.enabled == enabled &&
                std::strcmp(panel.encoders[3].label, "Depth") == 0 && panel.encoders[3].value == depth,
                "Every unshifted white click toggles bypass and leaves Depth alone");
    }
    require(controller.module(3, false) == InputResult::stock &&
            controller.click(3, true) == InputResult::stock,
            "M3 and subsequent Filter shortcuts must belong to stock");
    require(controller.module(2, false) == InputResult::page_changed, "Reopen OTT from Envelope");
    require(controller.module(2, false) == InputResult::page_changed && controller.page() == Page::envelope,
            "M2 on OTT must return to Envelope");

    for (unsigned track = 1; track < 5; ++track) {
        controller.select(track, Page::envelope);
        require(controller.module(2, false) == InputResult::page_changed, "Open OTT on the selected track");
        const auto expected = track == 4 ? InputResult::insert_limit : InputResult::settings_changed;
        require(controller.click(3, false) == expected, "Enable must enforce the four-insert cap");
    }
    require(controller.panel(panel) && panel.enabled_count == 4 && !panel.enabled,
            "The fifth track's panel must report four slots in use");
    require(controller.module(2, true) == InputResult::stock && controller.page() == Page::envelope,
            "Shift+M2 must fall through to stock");
    controller.select(9, Page::envelope);
    require(controller.module(2, false) == InputResult::stock, "Aux tracks must remain stock");

    const auto saved = controller.project();
    ott::DeviceControls restored;
    require(restored.load(saved), "A valid project must restore");
    restored.select(0, Page::envelope);
    restored.module(2, false);
    require(restored.panel(panel) && panel.enabled && panel.enabled_count == 4 &&
            std::strcmp(panel.encoders[0].label, "Time") == 0 && panel.encoders[3].value == depth,
            "Project activation must restore settings and reset temporary page state");
    ott::TrackInserts audio;
    require(audio.configure(restored.project().track), "The audio role must accept the restored settings");
    Block wet = input, untouched = input;
    for (unsigned block = 0; block < 3; ++block) {
        wet = input;
        audio.process(0, wet.data(), 1);
    }
    require(wet != input, "The hardware control snapshot must affect the audio track");
    require(audio.process(4, untouched.data(), 1) == 1 && untouched == input,
            "The rejected fifth insert must stay dry");
}

void test_limiter() {
    ott::Processor processor;
    ott::Controls controls;
    controls.low_trim = controls.high_trim = full;
    controls.level_match = 0;
    bool reached_ceiling = false;
    for (unsigned block = 0; block < 140; ++block) {
        Block input{};
        for (unsigned frame = 0; frame < opxy::block_frames; ++frame) {
            const double phase = 6.283185307179586 * 879 * (block * opxy::block_frames + frame) / 44100;
            input[2 * frame] = static_cast<opxy::i32>(
                full * (block < 128 ? .0002 : .99) * std::sin(phase));
            input[2 * frame + 1] = input[2 * frame] / 2;
        }
        processor.process(input.data(), controls);
        for (unsigned frame = 0; frame < opxy::block_frames; ++frame) {
            const double left = input[2 * frame], right = input[2 * frame + 1];
            require(std::abs(left) <= .891250938 * full &&
                    std::abs(left - 2 * right) < .001 * full,
                    "The linked limiter must cap transients without shifting the stereo image");
            reached_ceiling = reached_ceiling || std::abs(left) > .88 * full;
        }
    }
    require(reached_ceiling, "The limiter test must actually hit the ceiling");
}

void test_mono_transition(bool matching) {
    ott::Processor adaptive, stereo;
    ott::Controls controls;
    controls.level_match = matching ? full : 0;
    for (unsigned block = 0; block < 768; ++block) {
        Block input{};
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            const double phase = 6.283185307179586 * 230. * (block * opxy::block_frames + i) / 44100.;
            input[2 * i] = static_cast<opxy::i32>(full * .09 * std::sin(phase));
            input[2 * i + 1] = block < 384 ? input[2 * i] :
                static_cast<opxy::i32>(full * .08 * std::sin(4.5 * phase + .7));
        }
        Block always_stereo = input;
        // Force the stereo path with a bit below both internal input shifts.
        if (!block) always_stereo[1] ^= 1;
        adaptive.process(input.data(), controls);
        stereo.process(always_stereo.data(), controls);
        require(input == always_stereo, "Mono processing and the change to stereo must match the full path");
    }
}

void test_level_match(double hz, double amplitude, bool pulses, const ott::Controls& auto_controls) {
    ott::TrackInserts matched;
    ott::TrackSettings settings[ott::TrackInserts::track_count]{};
    settings[0].enabled = true;
    settings[0].controls = auto_controls;
    require(matched.configure(settings), "One insert must configure");
    ott::Processor uncorrected;
    auto manual = auto_controls;
    manual.level_match = 0;
    double dry_energy = 0, wet_energy = 0, off_energy = 0;
    for (unsigned block = 0; block < 4096; ++block) {
        Block dry{};
        for (unsigned frame = 0; frame < opxy::block_frames; ++frame) {
            const double t = (block * opxy::block_frames + frame) / 44100.;
            const double phase = 6.283185307179586 * t;
            const double amp = pulses ? (block % 512 < 300 ? amplitude : 0) : amplitude;
            dry[2 * frame] = static_cast<opxy::i32>(full * amp *
                (.75 * std::sin(hz * phase) + .20 * std::sin(6800 * phase)));
            dry[2 * frame + 1] = static_cast<opxy::i32>(full * amp *
                (.65 * std::sin(hz * phase + .4) + .20 * std::sin(3700 * phase)));
        }
        auto wet = dry;
        auto off = dry;
        matched.process(0, wet.data(), pulses && block % 512 >= 300 ? 0 : 1);
        uncorrected.process(off.data(), manual);
        if (block >= 3072) {
            for (unsigned i = 0; i < dry.size(); ++i) {
                dry_energy += double(dry[i]) * double(dry[i]);
                wet_energy += double(wet[i]) * double(wet[i]);
                off_energy += double(off[i]) * double(off[i]);
            }
        }
        if (pulses && block % 512 == 511) {
            require(!matched.needs_tail(0), "Silence must stop keeping the track awake");
            settings[0].enabled = false;
            require(matched.configure(settings), "Bypass the matched insert");
            Block bypass = {123456, -98765};
            const Block original = bypass;
            matched.process(0, bypass.data(), 1);
            require(bypass == original, "Match must pass audio through while OTT is bypassed");
            settings[0].enabled = true;
            require(matched.configure(settings), "Re-enable the matched insert");
            uncorrected.reset();
        }
    }
    const auto error = 10 * std::log10(wet_energy / dry_energy);
    const auto off_error = 10 * std::log10(off_energy / dry_energy);
    std::cout << "Level Match " << hz << " Hz: " << error
              << " dB from input, " << off_error << " dB with matching off\n";
    const double expected = off_error - std::max(-12., std::min(12., off_error));
    require(std::abs(error - expected) < .5 && std::abs(off_error) > 1.,
            "Auto must match input level up to the 12 dB correction limit");
}

int main() {
    ott::Processor processor;
    Block input, output;
    std::uint32_t seed = 17;
    for (auto& sample : input) {
        seed = seed * 1664525u + 1013904223u;
        sample = static_cast<opxy::i32>(seed);
    }
    ott::Controls controls;
    for (unsigned n = 0; n < 1024; ++n) {
        for (auto& sample : output) {
            seed = seed * 1664525u + 1013904223u;
            sample = n & 8 ? static_cast<opxy::i32>(seed) : n & 16 ? full : -full;
        }
        controls.depth = n & 2 ? full : 0;
        controls.time = n & 4 ? full : 0;
        controls.upward = n & 8 ? full : 0;
        controls.downward = n & 16 ? full : 0;
        controls.low_trim = controls.high_trim = n & 32 ? full : 0;
        controls.level_match = n & 64 ? full : 0;
        processor.process(output.data(), controls);
        for (const auto sample : output)
            require(std::abs(static_cast<double>(sample)) <= .891250938 * full,
                    "Full-range controls and input must stay inside the limiter ceiling");
    }
    processor.reset();
    output.fill(0);
    processor.process(output.data(), controls);
    require(std::all_of(output.begin(), output.end(), [](opxy::i32 x) { return x == 0; }),
            "Reset and silence must clear retained audio");

    ott::Controls neutral, wet;
    neutral.character = wet.character = ott::Drum;
    neutral.depth = 0;
    neutral.level_match = wet.level_match = 0;
    require(level(7500., .0008, wet) > 4 * level(7500., .0008, neutral),
            "Full Depth must lift quiet highs");
    require(level(7500., .7, wet) < .9 * level(7500., .7, neutral),
            "Full Depth must compress loud highs");
    auto trims = neutral;
    trims.low_trim = full;
    trims.high_trim = 0;
    require(level(22., .025, trims) > 3.5 * level(22., .025, neutral) &&
            level(10000., .025, trims) < .4 * level(10000., .025, neutral),
            "Low and High must have independent band trims even at zero Depth");
    ott::Controls auto_controls;
    auto_controls.character = ott::Drum;
    test_level_match(660., .012, false, auto_controls);
    test_level_match(180., .25, false, auto_controls);
    test_level_match(180., .72, false, auto_controls);
    auto_controls.depth = full / 4;
    auto_controls.low_trim = (full / 5) * 4;
    test_level_match(34., .025, true, auto_controls);
    test_mono_transition(false);
    test_mono_transition(true);
    test_limiter();
    test_track_inserts(input);
    test_device_controls(input);
    std::cout << "Faust OTT host checks passed\n";
}
