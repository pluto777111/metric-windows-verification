#pragma once
#include "metric_engine.hpp"

namespace metric {
constexpr unsigned cue_patterns=16, cue_steps=128;
constexpr unsigned cue_present=0x800, pin_pitch=1, pin_rhythm=2;

inline unsigned cue_quality(unsigned cue) { return (cue>>2)&3; }
inline unsigned cue_evolve(unsigned cue) { return (cue>>4)&127; }
inline uint16_t make_cue(unsigned pins,unsigned quality,unsigned evolve) {
    return static_cast<uint16_t>(cue_present | pins | quality<<2 | evolve<<4);
}
inline bool valid_cue(unsigned cue) {
    return (cue&~0xfffu)==0 && (cue&cue_present) && cue_quality(cue)<3 && cue_evolve(cue)<=100;
}

// Resolve once per rendered bar. The last cue carries across the CV loop's
// boundary until the first cue at or after the beginning of the next pass.
inline void active_cues(const uint16_t* cues,uint16_t* active,unsigned steps) {
    uint16_t value=0;
    for(unsigned step=0;step<steps;++step)if(cues[step])value=cues[step];
    for(unsigned step=0;step<steps;++step) {
        if(cues[step])value=cues[step];
        active[step]=value;
    }
}

struct CueWindow {
    const uint16_t* active;
    unsigned cycle, phase, step_ticks;

    unsigned at(int tick) const {
        if(!active || !cycle || !step_ticks)return 0;
        const int position=(static_cast<int>(phase)+tick)%static_cast<int>(cycle);
        return active[(position<0 ? position+static_cast<int>(cycle) : position)/step_ticks];
    }
};
}
