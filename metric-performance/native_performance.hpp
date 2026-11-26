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
    unsigned source_mode, target_c, target_d, pitch_motion;
    unsigned evolve, anchor_cursor, anchor_mask;
    int anchor_quality[4];
    int octave;
    unsigned anchor_span;
    bool editing_octave, editing_anchor_scale;
};

inline unsigned shifted_octave(unsigned pitch, int octave) {
    const int shifted=static_cast<int>(pitch)+octave*12;
    return shifted<0 ? pitch%12 : shifted>127 ? 127-(127-pitch)%12 : static_cast<unsigned>(shifted);
}

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
        if(mode!=OutputMode::drum) event.pitch=shifted_octave(event.pitch,performance.octave);
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
        if(pitch>=gap && in_scale(pitch-gap,root,scale)) return pitch-gap;
        if(pitch+gap<128 && in_scale(pitch+gap,root,scale)) return pitch+gap;
    }
    return pitch;
}

inline unsigned evolved_pitch(const Performance& p, unsigned pitch, unsigned bar, unsigned start) {
    const unsigned phase=(bar/(p.anchor_span?p.anchor_span:1))%4;
    if(!p.evolve || (p.anchor_mask & (1u<<phase))) return pitch;
    const int quality=p.anchor_quality[phase];
    const int next=static_cast<int>(pitch)+
        (static_cast<int>(start)-static_cast<int>(pitch)+quality*3)*static_cast<int>(p.evolve)/100;
    return next<0 ? 0u : next>127 ? 127u : static_cast<unsigned>(next);
}

inline void append_performance_event(metric_core::Bar& output, uint8_t* lanes,
                                     const metric_core::Event& event, unsigned lane,
                                     bool retrigger) {
    if(output.count>=metric_core::MAX_EVENTS || !event.duration) return;
    unsigned at=output.count;
    for(unsigned i=0;i<output.count;++i) {
        const auto& other=output.events[i];
        if(other.target==event.target && other.pitch==event.pitch) {
            if(other.tick==event.tick) return;
            if(!retrigger && other.tick<event.tick && event.tick-other.tick<other.duration) return;
            if(!retrigger && other.tick>event.tick && other.tick-event.tick<event.duration) return;
        }
        if(other.tick>event.tick && at==output.count) at=i;
    }
    for(unsigned i=output.count;i>at;--i) {
        output.events[i]=output.events[i-1];
        lanes[i]=lanes[i-1];
    }
    output.events[at]=event;
    lanes[at]=static_cast<uint8_t>(lane);
    ++output.count;
}

inline void render_generated_lanes(const Performance& p,const metric_core::Settings& master,
                                   unsigned bar,metric_core::Bar& out,uint8_t* lanes) {
    constexpr unsigned contours[4][8]={
        {0,0,4,2,0,2,1,0}, {0,2,4,6,4,2,1,0},
        {0,2,0,3,0,2,4,1}, {0,1,3,5,3,2,1,0}
    };
    auto settings=master;
    const auto mode=static_cast<OutputMode>(p.output);
    for(unsigned pair=0;pair<2;++pair) {
        metric_core::Bar pulses;
        if(pair) {
            settings.target_a=p.target_c;
            settings.target_b=p.target_d;
            settings.rotation=(settings.rotation+5)%16;
        }
        metric_core::generate(settings,bar,pulses);
        unsigned order[2]={0,0};
        for(unsigned i=0;i<pulses.count;++i) {
            auto event=pulses.events[i];
            const unsigned part=event.pitch==settings.pitch_a ? 0 : 1;
            const unsigned lane=pair*2+part;
            const unsigned note=order[part]++;
            if(p.tension>=50 && note==2) continue;
            if(p.interplay==1 && part && event.tick%96<12) continue;
            if(p.interplay==2 && part) event.tick=(event.tick+48)%metric_core::BAR_TICKS;
            if(p.distance && note%2)
                event.tick=(event.tick+p.distance/5)%metric_core::BAR_TICKS;
            unsigned degree=contours[lane][note%8];
            if(p.pitch_motion && note%2) degree=(degree+p.pitch_motion/20)%7;
            unsigned pitch=mode==OutputMode::drum ? (lane==0?36:lane==1?42:lane==2?38:46) :
                           output_pitch(mode,degree,p.root,p.scale)+(lane%2?12:0);
            if(mode!=OutputMode::drum)
                pitch=evolved_pitch(p,pitch,bar,48+p.root+(lane%2?12:0));
            if(mode!=OutputMode::drum) pitch=shifted_octave(pitch,p.octave);
            if(mode==OutputMode::scale_lock) pitch=locked_pitch(pitch,p.root,p.scale);
            event.pitch=static_cast<uint8_t>(pitch);
            append_performance_event(out,lanes,event,lane,master.ratchet!=0);
        }
    }
}

// The source keeps playing through stock. Render its companion only onto B.
inline unsigned pattern_phase_at_bar(const opxy::control::PatternTiming& timing,
                                    unsigned now, unsigned bar) {
    const unsigned elapsed=now-bar*metric_core::BAR_TICKS;
    return (timing.phase+timing.cycle-elapsed%timing.cycle)%timing.cycle;
}

inline void render_programmed_phrase(const Performance& p, const metric_core::Settings& master,
                                     unsigned bar, const opxy::control::PatternEvent* notes,
                                     unsigned count, unsigned cycle, unsigned phase,
                                     metric_core::Bar& out, uint8_t* lanes) {
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
            offset+=p.journey==2 && (i*37+bar*11)%100<p.freedom ? motion*2 : motion;
        }
        if(p.pitch_motion && p.target!=0 && p.destination && mode!=OutputMode::drum && (bar/period+i)%2) {
            const unsigned motion=p.pitch_motion/10;
            if(pitch+motion<=127) pitch+=motion;
        }
        const unsigned transformed=(n.tick+offset)%cycle;
        const unsigned first=(transformed+cycle-phase)%cycle;
        if(first>=metric_core::BAR_TICKS) continue;
        if(p.interplay==1) {
            bool near=false;
            for(unsigned j=0;j<count;++j) {
                const unsigned gap=evolution::distance(transformed,notes[j].tick);
                if(gap<=12 || cycle-gap<=12) { near=true; break; }
            }
            if(near) continue;
        }
        if(mode!=OutputMode::drum) pitch=evolved_pitch(p,pitch,bar,notes[0].pitch);
        if(mode!=OutputMode::drum) pitch=shifted_octave(pitch,p.octave);
        if(mode==OutputMode::scale_lock) pitch=locked_pitch(pitch,p.root,p.scale);
        if(mode==OutputMode::drum && pitch!=36 && pitch!=38 && pitch!=42 && pitch!=46)
            pitch=output_pitch(mode,pitch%7,p.root,p.scale);
        const auto length=static_cast<uint16_t>(n.length>1536 ? 1536 : n.length);
        for(unsigned tick=first;tick<metric_core::BAR_TICKS && out.count<metric_core::MAX_EVENTS;tick+=cycle)
            append_performance_event(out,lanes,
                {static_cast<uint16_t>(tick),master.target_b,static_cast<uint8_t>(pitch),n.velocity,length},1,false);
    }
}

} // namespace metric
