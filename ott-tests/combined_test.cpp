#include "firmware_runtime.hpp"
#include "control.cpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using Block = std::array<opxy::i32, 2 * opxy::block_frames>;
using Tracks = std::array<Block, ott::TrackInserts::track_count>;
using ott::u32;

Tracks run_mix(ott::AudioInsert& insert, const Tracks& input) {
    Tracks actual = input;
    for (u32 track = 0; track < actual.size(); ++track) {
        insert.start_track(track);
        assert(insert.process(track, actual[track].data(), 1) == 1);
    }
    return actual;
}

int main() {
    metric::ControlState metric_state{};
    metric::control_reset(metric_state);
    opxy::control::Input input{};
    opxy::control::Overlay overlay{};
    input.track = 12;
    input.values[12][7] = 0x18000000;
    input.transport.playing = 1;
    metric::control(metric_state, input, overlay);
    const auto bar = metric_state.bar;

    Tracks mixer{};
    unsigned fed = 0;
    for (unsigned i = 0; i < bar.count; ++i) {
        const auto& note = bar.events[i];
        const auto lane = metric_state.lanes[i];
        if (fed & (1u << lane)) continue;
        fed |= 1u << lane;
        const double step = 6.283185307 * 440.0 * std::exp2((static_cast<int>(note.pitch) - 69) / 12.0) / 44100;
        for (u32 frame = 0; frame < opxy::block_frames; ++frame) {
            auto sample = static_cast<opxy::i32>(std::sin(step * (frame + 1)) * note.velocity * 6000000);
            mixer[lane][2 * frame] = sample;
            mixer[lane][2 * frame + 1] = sample / 2;
        }
        mixer[lane + 4] = mixer[lane];
    }
    assert(fed == 15 && "METRIC must supply all four lanes");

    ott::DeviceControls ui;
    ott::AudioInsert audio;
    auto send = [&](u32 id, opxy::i32 value) { assert(audio.receive(id, value)); };
    assert(!audio.receive(ott::message_id - 1, 0));
    assert(!audio.receive(ott::last_parameter_id + 1, 0));
    ott::publish(ui.project(), 0, true, 1, send);
    assert(run_mix(audio, mixer) == mixer);

    auto switch_track = [&](u32 track) {
        ui.select(track, ott::Page::envelope);
        assert(ui.module(2, false) == ott::InputResult::page_changed);
        return ui.click(3, false);
    };
    assert(switch_track(0) == ott::InputResult::settings_changed);
    assert(ui.click(0, false) == ott::InputResult::page_changed);
    assert(ui.click(0, false) == ott::InputResult::page_changed);
    assert(ui.turn(0, false, -24, 0) == ott::InputResult::settings_changed);
    assert(ui.turn(3, false, -10, 0) == ott::InputResult::settings_changed);
    assert(ui.turn(1, false, 5, 0) == ott::InputResult::settings_changed);
    assert(ui.turn(2, false, 10, 0) == ott::InputResult::settings_changed);
    ott::publish(ui.project(), 1, false, 2, send);
    auto one = run_mix(audio, mixer);
    assert(one[0] != mixer[0]);
    for (u32 track = 1; track < mixer.size(); ++track) assert(one[track] == mixer[track]);

    for (u32 track = 1; track < 4; ++track) {
        assert(switch_track(track) == ott::InputResult::settings_changed);
        ott::publish(ui.project(), track + 1, false, 2 + track, send);
    }
    assert(switch_track(4) == ott::InputResult::insert_limit);
    auto four = run_mix(audio, mixer);
    for (u32 track = 0; track < mixer.size(); ++track) assert((four[track] != mixer[track]) == (track < 4));

    ott::Byte bytes[ott::project_bytes]{}, roundtrip[ott::project_bytes]{};
    assert(ott::encode_project(ui.project(), bytes) == sizeof bytes);
    ott::ProjectSettings restored{};
    assert(ott::decode_project(bytes, sizeof bytes, restored));
    constexpr u32 old_stride = 1 + 4 * ott::legacy_parameter_count;
    ott::Byte legacy[1 + ott::TrackInserts::track_count * old_stride] = {1};
    for (u32 track = 0; track < ott::TrackInserts::track_count; ++track)
        std::memcpy(legacy + 1 + track * old_stride,
                    bytes + 1 + track * (1 + 4 * ott::parameter_count), old_stride);
    ott::ProjectSettings migrated{};
    assert(ott::decode_project(legacy, sizeof legacy, migrated));
    assert(migrated.track[0].enabled &&
           migrated.track[0].controls.post_cutoff == restored.track[0].controls.post_cutoff &&
           migrated.track[0].controls.depth == ott::Controls{}.depth &&
           migrated.track[0].controls.low_trim == ott::Controls{}.low_trim &&
           migrated.track[0].controls.high_trim == ott::Controls{}.high_trim);
    auto corrupt = restored;
    corrupt.track[4].enabled = true;
    ott::encode_project(corrupt, roundtrip);
    assert(!ott::decode_project(roundtrip, sizeof roundtrip, restored));
    ott::encode_project(restored, roundtrip);
    assert(std::memcmp(bytes, roundtrip, sizeof bytes) == 0);
    assert(ui.load(restored));
    ott::AudioInsert recalled;
    ott::publish(ui.project(), 0, true, 10, [&](u32 id, opxy::i32 value) { assert(recalled.receive(id, value)); });
    const auto first_recalled = run_mix(recalled, mixer);
    ott::publish(ui.project(), 0, true, 10, send);
    assert(run_mix(audio, mixer) == first_recalled && "Recall must reset each insert's audio state");

    // An incomplete transaction leaves the previously applied project intact.
    ott::publish({}, 0, true, 11, [&](u32 id, opxy::i32 value) {
        if (id != ott::message_id + 3) send(id, value);
    });
    assert(run_mix(audio, mixer) == run_mix(recalled, mixer));
    ott::publish({}, 0, true, 12, send);
    assert(run_mix(audio, mixer) == mixer);

    metric::control(metric_state, input, overlay);
    assert(metric_state.bar.count == bar.count);
    for (unsigned i = 0; i < bar.count; ++i) {
        const auto& current = metric_state.bar.events[i];
        const auto& original = bar.events[i];
        assert(current.pitch == original.pitch && current.tick == original.tick && current.target == original.target);
    }
    std::puts("METRIC + OTT: bypass, one insert, four inserts, cap, project recall and partial transactions passed");
}
