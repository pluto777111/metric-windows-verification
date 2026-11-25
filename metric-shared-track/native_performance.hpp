#pragma once

#include "example.hpp"
#include "track_feed.hpp"
#include "output_pitch.hpp"

namespace metric {

struct Performance {
    evolution::Session phrases;
    unsigned page;
    bool audition_ready;
    unsigned distance, tension, interplay, freedom;
    unsigned target, destination, journey, pace;
    unsigned output, root, scale;
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

inline void render_shared_phrase(Performance& performance, const metric_core::Settings& master,
                                 unsigned bar, metric_core::Bar& output, uint8_t* lanes) {
    using namespace evolution;
    if (!performance.audition_ready) {
        performance.phrases.start(home);
        performance.phrases.add_anchor(destination, 0);
        performance.audition_ready = true;
    }
    const unsigned period = performance.pace ? performance.pace : 1;
    if (bar % period == 0) {
        performance.phrases.advance(0, performance.destination,
            {Mode::transform, static_cast<Journey>(performance.journey), performance.distance,
             performance.freedom, performance.tension}, bar * metric_core::BAR_TICKS);
    }
    const auto& frame = performance.phrases.frame();
    const auto& call = performance.target == 1 ? home.call : frame.material.call;
    const auto& answer = performance.target == 0 ? home.answer : frame.material.answer;
    const auto mode = static_cast<OutputMode>(performance.output);

    // The rhythm controls reshape the timing of the shared material. Keep the
    // phrase's ordered pitches on the resulting pulse train.
    metric_core::generate(master, bar, output);
    unsigned lengths[2]={0,0}, indices[2]={0,0};
    unsigned call_ticks[20]; // 16 grid pulses plus at most four ratchets
    for (unsigned i=0;i<output.count;++i) {
        const auto& event=output.events[i];
        const unsigned lane=event.pitch==master.pitch_a ? 0 : 1;
        if (lane==0) call_ticks[lengths[0]]=event.tick;
        ++lengths[lane];
    }
    unsigned keep=0;
    for (unsigned i=0;i<output.count;++i) {
        auto event=output.events[i];
        const unsigned lane=event.pitch==master.pitch_a ? 0 : 1;
        const unsigned at=indices[lane]++;
        const unsigned denominator=lengths[lane]-1;
        const unsigned index=denominator ? (at*(note_count-1)+denominator/2)/denominator : 0;
        const auto& note=lane==0 ? call.notes[index] : answer.notes[index];
        if (lane==1 && int(index)==frame.omitted && performance.target!=0) continue;
        if (lane==1 && performance.interplay==1) {
            bool near=false;
            for (unsigned j=0;j<lengths[0];++j)
                if (distance(event.tick,call_ticks[j])<=12) near=true;
            if (near) continue;
        }
        const auto& original = lane==0 ? home.call.notes[index] : home.answer.notes[index];
        const int offset = (int(note.step)-int(original.step))*24;
        const int answer_delay = lane==1 && performance.interplay==2 ? 48 : 0;
        event.tick=(int(event.tick)+offset+answer_delay+int(metric_core::BAR_TICKS))%metric_core::BAR_TICKS;
        event.pitch=output_pitch(mode,note.degree,performance.root,performance.scale);
        event.velocity=note.accent ? 105 : event.velocity;
        lanes[keep]=static_cast<uint8_t>(lane);
        output.events[keep++]=event;
    }
    output.count=keep;
    for (unsigned i=1;i<keep;++i) {
        const auto event=output.events[i];
        const auto lane=lanes[i];
        unsigned j=i;
        while(j && output.events[j-1].tick>event.tick) {
            output.events[j]=output.events[j-1];
            lanes[j]=lanes[j-1];
            --j;
        }
        output.events[j]=event;
        lanes[j]=lane;
    }
}

inline unsigned locked_pitch(unsigned pitch,unsigned root,unsigned scale) {
    for(unsigned gap=0;gap<=12;++gap) {
        for(unsigned degree=0;degree<7;++degree) {
            const unsigned pc=output_pitch(OutputMode::scale_lock,degree,root,scale)%12;
            if(pitch>=gap && (pitch-gap)%12==pc) return pitch-gap;
        }
        for(unsigned degree=0;degree<7;++degree) {
            const unsigned pc=output_pitch(OutputMode::scale_lock,degree,root,scale)%12;
            if(pitch+gap<128 && (pitch+gap)%12==pc) return pitch+gap;
        }
    }
    return pitch;
}

// The source keeps playing through stock. Render its companion only onto B.
inline void render_programmed_phrase(const Performance& p, const metric_core::Settings& master,
                                     unsigned bar, const opxy::control::PatternEvent* notes,
                                     unsigned count, metric_core::Bar& out, uint8_t* lanes) {
    const unsigned period=p.pace ? p.pace : 1;
    const auto mode=static_cast<OutputMode>(p.output);
    const unsigned rotation=(master.rotation+(bar/period)%metric_core::bounded_phrase(master.phrase_bars))*24;
    unsigned omit=count;
    if(p.tension>=50 && count>2) {
        omit=1;
        for(unsigned i=2;i<count-1;++i)
            if(notes[i].velocity>notes[omit].velocity) omit=i;
    }
    for(unsigned i=0;i<count;++i) {
        if(i==omit) continue;
        const auto& n=notes[i];
        unsigned pitch=n.pitch;
        unsigned offset=rotation;
        if(p.interplay==2) offset+=48;
        if(p.distance && p.target!=0) {
            const unsigned motion=p.distance/10;
            const unsigned direction=(bar/period+i)%2;
            offset+=p.journey==2 && (i*37+bar*11)%100<p.freedom ? motion*2 : motion;
            if(p.destination && mode!=OutputMode::drum && direction && pitch+motion<=127)
                pitch+=motion;
        }
        const unsigned tick=(n.tick+offset)%metric_core::BAR_TICKS;
        if(p.interplay==1) {
            bool near=false;
            for(unsigned j=0;j<count;++j) {
                const unsigned gap=evolution::distance(tick,notes[j].tick);
                if(gap<=12 || gap>=metric_core::BAR_TICKS-12) { near=true; break; }
            }
            if(near) continue;
        }
        if(mode==OutputMode::scale_lock) pitch=locked_pitch(pitch,p.root,p.scale);
        if(mode==OutputMode::drum && pitch!=36 && pitch!=38 && pitch!=42 && pitch!=46)
            pitch=output_pitch(mode,pitch%7,p.root,p.scale);
        metric_core::insert(out,tick,master.target_b,pitch,n.velocity,
                            n.length>1536 ? 1536 : n.length);
    }
    for(unsigned i=0;i<out.count;++i) lanes[i]=1;
}

} // namespace metric
