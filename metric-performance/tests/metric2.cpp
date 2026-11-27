#include "../control.cpp"
#include <cassert>

using namespace metric;
int main() {
    metric_core::Bar source{};
    uint8_t lanes[metric_core::MAX_EVENTS]{};
    source.count = 5;
    source.events[0] = {0, 0, 48, 100, 40};
    source.events[1] = {36, 0, 50, 90, 36};
    source.events[2] = {110, 0, 60, 94, 30};
    source.events[3] = {111, 1, 48, 105, 36};
    source.events[4] = {220, 0, 64, 100, 30};
    lanes[3] = 1;
    const auto original = metric2::hash(source, lanes);
    uint16_t cues[] = {make_cue(pin_pitch | pin_rhythm, 1, 100)};
    CueWindow pinned{cues, 384, 0, 384};
    Performance p{};
    p.evolve = p.pitch_motion = p.freedom = 95;
    p.output = 1;
    uint8_t a_lanes[metric_core::MAX_EVENTS]{}, b_lanes[metric_core::MAX_EVENTS]{};
    metric2::State a{}, b{};
    unsigned prior = 0;
    for (unsigned i = 0; i < 8; ++i) {
        auto candidate = source, check = source;
        for (unsigned j = 0; j < source.count; ++j) a_lanes[j] = b_lanes[j] = lanes[j];
        metric2::develop(a, candidate, a_lanes, p, {}, i, 4);
        metric2::develop(b, check, b_lanes, p, {}, i, 4);
        const auto heard = metric2::hash(candidate, a_lanes);
        assert(heard == metric2::hash(check, b_lanes));
        if (i % 4 == 0) assert(heard == original);
        else {
            assert(heard != original && heard != prior);
            assert(candidate.events[0].tick == 0 && candidate.events[candidate.count-1].tick == 220);
            for (unsigned j = 1; j < candidate.count; ++j)
                assert(candidate.events[j].tick >= candidate.events[j-1].tick);
        }
        prior = heard;
    }
    a.clear();
    auto anchored = source;
    for (unsigned j = 0; j < source.count; ++j) a_lanes[j] = lanes[j];
    metric2::develop(a, anchored, a_lanes, p, pinned, 1, 4);
    assert(anchored.count == source.count);
    for (unsigned j = 0; j < source.count; ++j) {
        assert(anchored.events[j].pitch == source.events[j].pitch);
        assert(anchored.events[j].tick == source.events[j].tick);
    }

    ControlState st{}, recalled{};
    control_reset(st);
    assert(!st.metric2_enabled);
    st.performance.page = 4;
    opxy::control::Input in{};
    in.track = 12;
    in.turn_count = 1;
    in.turns[0] = {0, 0, 0};
    opxy::control::Overlay display{};
    control(st, in, display);
    assert(st.metric2_enabled);
    assert((display.display[13] & 16) != 0);
    unsigned char bytes[10]{};
    assert(control_save(st, bytes, sizeof bytes) == sizeof bytes);
    control_load(recalled, bytes, sizeof bytes);
    assert(recalled.metric2_enabled);
    bytes[0] = static_cast<unsigned char>(kPreviousVersion);
    control_load(recalled, bytes, sizeof bytes);
    assert(!recalled.metric2_enabled);
    control(st, in, display);
    assert(!st.metric2_enabled);
}
