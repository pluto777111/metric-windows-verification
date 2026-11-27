#pragma once

#include "native_performance.hpp"

namespace metric {
namespace metric2 {

using metric_core::BAR_TICKS;
using metric_core::Bar;
using metric_core::Event;
using metric_core::MAX_EVENTS;

// The source bar is immutable. The working phrase retains onset, interval,
// accent and gate relationships in the existing scheduler's event format.
struct State {
    Bar phrase;
    uint8_t lanes[MAX_EVENTS];
    unsigned source, phrase_hash, bar;
    unsigned rhythm, pitch;
    unsigned history[4];
    unsigned visits;
    bool valid;

    void clear() {
        valid = false;
        visits = rhythm = pitch = 0;
        for (auto& value : history) value = 0;
    }
};

inline unsigned hash(const Bar& bar, const uint8_t* lanes) {
    unsigned value = 2166136261u;
    for (unsigned i = 0; i < bar.count; ++i) {
        const auto& e = bar.events[i];
        value = (value ^ (e.tick | e.pitch << 9 | e.target << 16 | lanes[i] << 20)) * 16777619u;
        value = (value ^ (e.duration | e.velocity << 16)) * 16777619u;
    }
    return value;
}

struct Disturbance {
    unsigned index = MAX_EVENTS, previous = 0, next = 0;
    unsigned collisions = 0, repetition = 0, space = 0;
};

inline Disturbance relationships(const State& state, unsigned serial) {
    Disturbance result;
    for (const unsigned memory : state.history)
        result.repetition += memory != 0 && memory == state.phrase_hash;
    unsigned best = 0;
    const auto& phrase = state.phrase;
    constexpr unsigned cells = BAR_TICKS / 12;
    uint8_t density[cells]{}, own_density[4][cells]{}, next_note[MAX_EVENTS]{};
    unsigned adjacent[4] = {MAX_EVENTS, MAX_EVENTS, MAX_EVENTS, MAX_EVENTS};
    for (unsigned i = phrase.count; i-- > 0;) {
        const unsigned lane = state.lanes[i], cell = phrase.events[i].tick / 12;
        next_note[i] = static_cast<uint8_t>(adjacent[lane]);
        adjacent[lane] = i;
        ++density[cell];
        ++own_density[lane][cell];
    }
    for (auto& index : adjacent) index = MAX_EVENTS;
    for (unsigned i = 0; i < phrase.count; ++i) {
        const auto& e = phrase.events[i];
        const unsigned lane = state.lanes[i];
        const unsigned previous = adjacent[lane], next = next_note[i];
        adjacent[lane] = i;
        if (e.tick % 96 == 0 || previous == MAX_EVENTS || next == MAX_EVENTS ||
            phrase.events[previous].tick >= e.tick || phrase.events[next].tick <= e.tick) continue;
        const unsigned cell = e.tick / 12;
        const unsigned collisions = density[cell] - own_density[lane][cell];
        const unsigned space = phrase.events[next].tick - phrase.events[previous].tick;
        const unsigned score = collisions * BAR_TICKS + space + (i + serial) % 11;
        if (score > best) {
            best = score;
            result.index = i;
            result.previous = previous;
            result.next = next;
            result.collisions = collisions;
            result.space = space;
        }
    }
    return result;
}

inline bool occupied(const Bar& phrase, unsigned index, unsigned onset) {
    for (unsigned j = 0; j < phrase.count; ++j)
        if (j != index && phrase.events[j].target == phrase.events[index].target &&
            phrase.events[j].tick == onset) return true;
    return false;
}

inline void transform(State& state, const Performance& p, const CueWindow& cues,
                      unsigned amount, unsigned serial) {
    // H examines heard phrase history, voice coincidence and available space.
    const Disturbance move = relationships(state, serial);
    if (move.index == MAX_EVENTS) return;
    auto& bar = state.phrase;
    Event& note = bar.events[move.index];
    const Event before = bar.events[move.previous], after = bar.events[move.next];
    const unsigned cue = cues.at(note.tick);
    if (cue && !cue_evolve(cue)) return;

    // G increases movement under repetition and emphasizes space under collision.
    state.rhythm = (state.rhythm + amount + 8 * move.repetition + 12 * (move.collisions != 0)) / 2;
    state.pitch = (state.pitch + p.pitch_motion + amount / 3 + 4 * move.repetition) / 2;
    if (!(cue & pin_rhythm) && state.rhythm >= 12) {
        const int direction = note.tick - before.tick < after.tick - note.tick ? 1 : -1;
        const int onset = static_cast<int>(note.tick) + direction * static_cast<int>(state.rhythm / 7);
        if (onset > before.tick && onset < after.tick &&
            !(cues.at(onset) & pin_rhythm) && !occupied(bar, move.index, onset))
            note.tick = static_cast<uint16_t>(onset);
    }

    const auto output = static_cast<OutputMode>(p.output);
    if (output != OutputMode::drum && !(cue & pin_pitch) && state.pitch >= 15) {
        // Move within the surrounding interval when changing pitch. Keep its
        // melodic direction and project back through the existing scale lock.
        const int original = note.pitch;
        const int toward = static_cast<int>(after.pitch) - original;
        const int from = original - static_cast<int>(before.pitch);
        if ((from > 0 && toward > 0) || (from < 0 && toward < 0)) {
            const int middle = (static_cast<int>(before.pitch) + after.pitch) / 2;
            int pitch = original + (middle - original) * static_cast<int>(state.pitch) / 100;
            if (pitch == original && middle != original) pitch += middle > original ? 1 : -1;
            if (output == OutputMode::scale_lock) pitch = locked_pitch(pitch, p.root, p.scale);
            if ((pitch > before.pitch && pitch < after.pitch) ||
                (pitch < before.pitch && pitch > after.pitch)) note.pitch = static_cast<uint8_t>(pitch);
        }
    }

    // F redistributes emphasis and may split one long interior span with a
    // quieter neighboring pitch. The phrase keeps the same transport length.
    if (p.tension || amount >= 24) {
        const unsigned accent = (p.tension + amount) / 10;
        const unsigned boosted = note.velocity + accent;
        note.velocity = static_cast<uint8_t>(move.collisions ?
            (note.velocity > accent ? note.velocity - accent : 1) : boosted > 127 ? 127 : boosted);
    }
    const Event moved = note;
    const auto lane = state.lanes[move.index];
    unsigned position = move.index;
    while (position && bar.events[position - 1].tick > moved.tick) {
        bar.events[position] = bar.events[position - 1];
        state.lanes[position] = state.lanes[position - 1];
        --position;
    }
    while (position + 1 < bar.count && bar.events[position + 1].tick < moved.tick) {
        bar.events[position] = bar.events[position + 1];
        state.lanes[position] = state.lanes[position + 1];
        ++position;
    }
    bar.events[position] = moved;
    state.lanes[position] = lane;
    auto& active = bar.events[position];
    if (!(cue & (pin_rhythm | pin_pitch)) && amount >= 60 && p.freedom >= 40 &&
        move.space >= 72 && after.tick > active.tick + 48 && bar.count < MAX_EVENTS) {
        const unsigned gap = after.tick - active.tick;
        const unsigned onset = active.tick + gap / 2;
        if ((cues.at(onset) & (pin_rhythm | pin_pitch)) || occupied(bar, position, onset)) return;
        unsigned pitch = output == OutputMode::drum ? after.pitch : (active.pitch + after.pitch) / 2;
        if (output == OutputMode::scale_lock) pitch = locked_pitch(pitch, p.root, p.scale);
        const unsigned duration = active.duration < gap / 3 ? active.duration : gap / 3;
        if (active.duration > gap / 2) active.duration = static_cast<uint16_t>(gap / 2);
        append_performance_event(bar, state.lanes,
            {static_cast<uint16_t>(onset), active.target, static_cast<uint8_t>(pitch),
             static_cast<uint8_t>((active.velocity + 1) / 2), static_cast<uint16_t>(duration)},
            lane, false);
    }
}

inline void develop(State& state, Bar& bar, uint8_t* lanes, const Performance& p,
                    const CueWindow& cues, unsigned index, unsigned phrase_length) {
    const unsigned depth = p.evolve > p.distance ? p.evolve : p.distance;
    const unsigned amount = depth > p.pitch_motion ? depth : p.pitch_motion;
    if (!amount && !p.tension) {
        state.clear();
        return;
    }
    unsigned source = hash(bar, lanes);
    if (cues.active && cues.step_ticks) {
        source ^= cues.phase * 16777619u;
        for (unsigned step = 0; step < cues.cycle / cues.step_ticks; ++step) {
            source = (source ^ cues.active[step]) * 16777619u;
        }
    }
    const bool next_bar = !state.valid || state.bar != index;
    const bool restart = !state.valid || source != state.source ||
        (next_bar && (index != state.bar + 1 || index % (phrase_length > 1 ? phrase_length : 4) == 0));
    if (restart) {
        if (!state.valid || index < state.bar || index > state.bar + 1) state.clear();
        state.phrase = bar;
        for (unsigned i = 0; i < bar.count; ++i) state.lanes[i] = lanes[i];
        state.source = source;
        state.phrase_hash = source;
    }
    if (restart || next_bar) {
        const unsigned pace = p.pace ? p.pace : 1;
        const bool home = index % (phrase_length > 1 ? phrase_length : 4) == 0;
        if (!home && index % pace == 0)
            transform(state, p, cues, amount, index);
        state.history[state.visits++ % 4] = state.phrase_hash;
        state.phrase_hash = hash(state.phrase, state.lanes);
    }
    state.bar = index;
    state.valid = true;
    bar = state.phrase;
    for (unsigned i = 0; i < bar.count; ++i) lanes[i] = state.lanes[i];
}

}
}
