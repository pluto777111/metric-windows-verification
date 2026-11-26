#pragma once

namespace metric {

enum class OutputMode : unsigned { drum, tonal, scale_lock };

constexpr unsigned scale_count=16;
constexpr unsigned scale_masks[scale_count]={
    0xab5, 0x5ad, 0x6ad, 0x6b5, 0x5ab, 0x9ad, // existing firmware indices
    0xad5, 0x56b, 0xaad, 0x5b3, 0x295, 0x4a9, 0x4e9, 0x555, 0x6db, 0xb6d
};
inline bool in_scale(unsigned pitch,unsigned root,unsigned scale) {
    return (scale_masks[scale%scale_count] >> ((pitch+12-root%12)%12)) & 1u;
}
inline unsigned output_pitch(OutputMode mode, unsigned degree, unsigned root, unsigned scale) {
    constexpr unsigned drums[7]={36,38,42,46,45,49,51};
    degree%=7;
    if(mode==OutputMode::drum)return drums[degree];
    const unsigned base=48+root%12;
    if(mode==OutputMode::scale_lock) {
        for(unsigned step=0;step<24;++step) {
            if(!in_scale(base+step,root,scale)) continue;
            if(!degree) return base+step;
            --degree;
        }
    }
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
