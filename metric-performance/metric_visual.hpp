#pragma once
#include "metric_engine.hpp"
namespace metric_visual {
// Each lane preserves precise 96 PPQN hit positions, including off-grid
// 3:2 and 5:4 pulses and A-lane ratchets. No 16-step quantization.
struct Lane { uint16_t ticks[16]; uint8_t count; };
struct Pattern { Lane a, b; };
inline Pattern exact(const metric_core::Bar& bar,const metric_core::Settings& s){
  Pattern r={};
  for(unsigned i=0;i<bar.count;++i){
    const metric_core::Event& e=bar.events[i];
    Lane* lane=e.pitch==s.pitch_a?&r.a:(e.pitch==s.pitch_b?&r.b:nullptr);
    if(lane && lane->count<16) lane->ticks[lane->count++]=e.tick;
  }
  return r;
}
// Distance from current transport tick to each lane's next pulse.
// The wrap-around case uses the first hit of the next bar, assuming the
// same displayed pattern; this is for visualization, not event dispatch.
inline unsigned next_in_bar(const Lane& lane,unsigned tick){
  if(!lane.count)return metric_core::BAR_TICKS;
  tick%=metric_core::BAR_TICKS;
  for(unsigned i=0;i<lane.count;++i)
    if(lane.ticks[i]>=tick)return unsigned(lane.ticks[i])-tick;
  return metric_core::BAR_TICKS-tick+lane.ticks[0];
}
// Map an exact tick into the 400-unit timeline region (0..400).
inline unsigned xpos(unsigned tick){return 44u+(tick*400u+192u)/384u;}
}
