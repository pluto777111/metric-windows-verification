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
                require(std::abs(left - 2 * right) < 2048, "Stereo gain must stay linked");
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
    settings[0].controls.post_cutoff = full / 2;
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
            controller.click(0, false) == InputResult::page_changed &&
            controller.panel(panel) && std::strcmp(panel.encoders[0].label, "Post Cutoff") == 0,
            "Clicking encoder one must show Post Cutoff");
    const auto before = controller.project().track[0].controls;
    require(controller.turn(0, false, -8, 1) == InputResult::settings_changed &&
            controller.project().track[0].controls.post_cutoff < before.post_cutoff &&
            controller.project().track[0].controls.character == before.character,
            "Post Cutoff must have independent storage");
    require(controller.turn(0, true, 5, 0) == InputResult::handled &&
            controller.click(3, true) == InputResult::stock,
            "Shift must stay out of the OTT toggle");
    require(controller.click(1, false) == InputResult::page_changed &&
            controller.click(2, false) == InputResult::page_changed &&
            controller.panel(panel) && std::strcmp(panel.encoders[0].label, "Post Cutoff") == 0 &&
            std::strcmp(panel.encoders[1].label, "Upward") == 0 &&
            std::strcmp(panel.encoders[2].label, "Downward") == 0 && !panel.enabled,
            "A refinement click must switch only its own encoder");
    require(controller.turn(1, false, 127, 3) == InputResult::settings_changed &&
            controller.project().track[0].controls.upward == full,
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
    controller.select(8, Page::envelope);
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

void test_loud_hit_after_quiet_material() {
    for (int mix : {25, 100}) {
        ott::Processor processor;
        ott::Controls controls;
        controls.mix = static_cast<opxy::i32>(static_cast<opxy::i64>(full) * mix / 100);
        for (unsigned block = 0; block < 130; ++block) {
            Block input{};
            for (unsigned frame = 0; frame < opxy::block_frames; ++frame) {
                const double phase = 6.283185307 * 879 * (block * opxy::block_frames + frame) / 44100;
                input[2 * frame] = input[2 * frame + 1] = static_cast<opxy::i32>(
                    full * (block < 128 ? .015 : .88) * std::sin(phase));
            }
            processor.process(input.data(), controls);
            for (auto sample : input)
                require(sample != full && sample != -full - 1,
                        "The first loud blocks must not carry upward gain from quiet material");
        }
    }
}

int main() {
    ott::Processor processor;
    Block input, output;
    std::uint32_t seed = 17;
    for (auto& sample : input) {
        seed = seed * 1664525u + 1013904223u;
        sample = static_cast<opxy::i32>(seed);
    }
    ott::Controls controls, dry, wet;
    dry.upward = dry.downward = 0;
    wet.upward = wet.downward = full;
    wet.low_protect = 0;
    controls.mix = 0;
    output = input;
    processor.process(output.data(), controls);
    require(input == output, "Mix zero must pass the original samples");
    processor.reset();
    processor.process(output.data(), dry);
    require(input == output, "Neutral controls must be transparent");
    for (int n = 0; n < 512; ++n) {
        std::rotate(input.begin(), input.begin() + 1, input.end());
        output = input;
        controls = wet;
        controls.character = controls.attack = controls.time = n % 2 ? full : 0;
        controls.trim = n % 2 ? full : 0;
        controls.post_cutoff = n % 3 ? 0 : full;
        controls.mix = n % 3 ? full : full / 2;
        processor.process(output.data(), controls);
    }
    processor.reset();
    output.fill(0);
    processor.process(output.data(), wet);
    require(std::all_of(output.begin(), output.end(), [](opxy::i32 x) { return x == 0; }),
            "Reset and silence must clear retained audio");

    for (double hz : {90., 900., 7500.}) {
        require(level(hz, .008, wet) > 1.4 * level(hz, .008, dry), "Quiet band must be boosted");
        require(level(hz, .7, wet) < .9 * level(hz, .7, dry), "Loud band must be attenuated");
    }
    ott::Controls audition, bypass;
    bypass.mix = 0;
    const double quiet_high = level(7500., .008, bypass);
    require(level(7500., .008, audition) > 4.75 * quiet_high,
            "The default full Mix must lift sustained quiet highs by at least 13.5 dB");
    audition.mix = full / 4;
    require(level(7500., .008, audition) > 1.9 * quiet_high,
            "The quiet high-band lift must remain audible at quarter Mix");
    audition.mix = full;
    audition.depth = full / 2;
    const double half_depth = level(7500., .008, audition);
    audition.depth = 0;
    require(level(7500., .008, audition) == quiet_high && half_depth > 2 * quiet_high,
            "Depth must scale the dynamics and leave an exact dry signal at zero with neutral trims");
    audition.low_trim = full;
    audition.high_trim = 0;
    require(level(45., .025, audition) > 3 * level(45., .025, bypass) &&
            level(10000., .025, audition) < .55 * level(10000., .025, bypass),
            "Low and High must have independent band trims even at zero Depth");
    audition.high_trim = full;
    const double limited = level(10000., .95, audition);
    require(limited > .5 && limited < .891251,
            "The enabled insert must limit boosted stereo peaks without collapsing the output");
    ott::Processor limiter;
    Block over{};
    for (unsigned n = 0; n < 24; ++n) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            over[2 * i] = i % 2 ? full : -full;
            over[2 * i + 1] = over[2 * i] / 2;
        }
        limiter.process(over.data(), audition);
        for (unsigned i = 0; i < opxy::block_frames; ++i)
            require(std::abs(static_cast<double>(over[2 * i])) <= .891250938 * full &&
                    std::abs(static_cast<double>(over[2 * i]) - 2. * over[2 * i + 1]) < 2048,
                    "Every processed sample peak must meet the ceiling with linked stereo gain");
    }
    controls = wet;
    controls.low_protect = full;
    require(level(45., .7, controls) > 1.1 * level(45., .7, wet), "Low Protect must preserve loud bass");
    controls = dry;
    controls.trim = full;
    require(level(900., .2, controls) > 1.35 * level(900., .2, dry), "Trim must raise wet output");
    controls = wet;
    controls.character = full;
    const double bright = level(7500., .008, controls);
    controls.character = 0;
    require(bright > 1.05 * level(7500., .008, controls), "Character must brighten the high band");
    controls = wet;
    controls.post_cutoff = full / 2;
    require(level(7500., .2, controls) < .15 * level(7500., .2, wet),
            "Post cutoff must attenuate the compressed high band");
    require(level(45., .2, controls) > .98 * level(45., .2, wet),
            "Post cutoff must preserve low frequencies");
    controls.mix = 0;
    output = input;
    processor.process(output.data(), controls);
    require(output == input, "Mix zero must bypass the post cutoff");
    test_loud_hit_after_quiet_material();
    test_track_inserts(input);
    test_device_controls(input);
    std::cout << "OTT host checks passed\n";
}
