#pragma once

#include "example.hpp"
#include "track_feed.hpp"
#include "output_pitch.hpp"

namespace metric {

struct Performance {
    evolution::Session phrases;
    unsigned page;
    bool audition_ready;
};

inline metric_punch::Settings effective_settings(const Performance& performance,
                                                 const metric_punch::Settings& master) {
    return metric_punch::resolve(performance.phrases.input.punch, master);
}

// Feed a validated paired key edge after stock has handled it. This entry
// point is exercised by the host test; the current SDK does not provide that
// event to a METRIC machine's control callback.
inline void observe_punch_key(Performance& performance, unsigned index, bool pressed) {
    if (index >= metric_punch::KEY_COUNT) return;
    if (pressed)
        metric_punch::press(performance.phrases.input.punch, index);
    else
        metric_punch::release(performance.phrases.input.punch, index);
}

inline void audition_phrase(Performance& performance, const metric_core::Settings& master,
                            unsigned bar, metric_core::Bar& output) {
    using namespace evolution;
    if (!performance.audition_ready) {
        performance.phrases.start(home);
        performance.phrases.add_anchor(destination, 0);
        performance.audition_ready = true;
    }
    performance.phrases.advance(0, 1,
        {Mode::transform, Journey::smooth, master.morph, 0, master.density},
        bar * metric_core::BAR_TICKS);
    const auto& frame = performance.phrases.frame();
    const auto mode=static_cast<OutputMode>(master.ratio);
    const unsigned root=root_from_percent(master.ratchet);
    const unsigned scale=scale_from_bars(master.phrase_bars);
    if (performance.page==3) {
        auto rhythm=master;
        rhythm.ratio=0;
        rhythm.ratchet=0;
        rhythm.phrase_bars=4;
        metric_core::generate(rhythm,bar,output);
        // Audition how the rhythm sits around the instrument phrase.
        // The instrument keeps its own timing. Rotation chooses the distance
        // from the nearest phrase onset at which rhythm hits make room.
        unsigned keep=0;
        for (unsigned i=0;i<output.count;++i) {
            auto event=output.events[i];
            bool near=false;
            for (unsigned j=0;j<note_count;++j) {
                if (int(j)==frame.omitted) continue;
                const unsigned onset=Session::onset(frame.material,j)*24;
                if (distance(event.tick,onset)<=master.rotation) near=true;
            }
            if (!near) output.events[keep++]=event;
        }
        output.count=keep;
    }
    for (unsigned index = 0; index < note_count; ++index) {
        const auto& answer = frame.material.answer.notes[index];
        if (int(index) != frame.omitted)
            metric_core::insert(output, Session::onset(frame.material, index) * 24,
                                master.target_b, output_pitch(mode,answer.degree,root,scale),
                                answer.accent ? 105 : 77, 20);
    }
}

} // namespace metric
