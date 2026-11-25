#pragma once

namespace metric {

enum class OutputMode : unsigned { drum, tonal, scale_lock };

// Audition mapping: six supported scales, rooted in the octave above C3.
// Rhythm output uses drum note numbers and never goes through a scale.
inline unsigned output_pitch(OutputMode mode, unsigned degree, unsigned root, unsigned scale) {
    constexpr unsigned drums[7]={36,38,42,46,45,49,51};
    constexpr unsigned tones[6][7]={
        {0,2,4,5,7,9,11}, // major
        {0,2,3,5,7,8,10}, // natural minor
        {0,2,3,5,7,9,10}, // dorian
        {0,2,4,5,7,9,10}, // mixolydian
        {0,1,3,5,7,8,10}, // phrygian
        {0,2,3,5,7,8,11}  // harmonic minor
    };
    degree%=7;
    if(mode==OutputMode::drum)return drums[degree];
    const unsigned base=48+root%12;
    if(mode==OutputMode::scale_lock)return base+tones[scale%6][degree];
    return base+degree;
}

inline unsigned root_from_percent(unsigned value) {
    return value>=100 ? 11 : value*12/100;
}

inline unsigned scale_from_bars(unsigned bars) {
    unsigned index=0;
    while(bars>1 && index<5) {bars>>=1;++index;}
    return index;
}

} // namespace metric
