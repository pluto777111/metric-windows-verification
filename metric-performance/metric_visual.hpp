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

// Four lanes, six representative notes per lane. The view keeps 32 timing
// positions and eight pitch positions within each lane's range for the bar.
// The last word carries the counts, so a note at the origin is valid.
constexpr unsigned preview_lanes=4, preview_per_lane=6, preview_time_bins=32;
constexpr unsigned preview_words[]={1,2,4,5,6,7,8};
struct PhraseView { uint32_t packed[7]; };

inline PhraseView phrase_view(const metric_core::Bar& bar,const uint8_t* lanes,bool drums) {
  PhraseView view{};
  unsigned counts[preview_lanes]{}, low[preview_lanes]={127,127,127,127}, high[preview_lanes]{};
  for(unsigned i=0;i<bar.count;++i) {
    const unsigned lane=lanes[i], pitch=bar.events[i].pitch;
    ++counts[lane];
    if(pitch<low[lane])low[lane]=pitch;
    if(pitch>high[lane])high[lane]=pitch;
  }
  unsigned seen[preview_lanes]{}, selected[preview_lanes]{};
  for(unsigned i=0;i<bar.count;++i) {
    const unsigned lane=lanes[i], order=seen[lane]++;
    const unsigned capacity=counts[lane]<preview_per_lane ? counts[lane] : preview_per_lane;
    const unsigned next=selected[lane];
    if(next==capacity || order!=next*(counts[lane]-1)/(capacity>1 ? capacity-1 : 1))continue;
    const auto& note=bar.events[i];
    const unsigned span=high[lane]-low[lane];
    const unsigned height=drums || !span ? 4 : ((note.pitch-low[lane])*7+span/2)/span;
    const unsigned onset=note.tick*preview_time_bins/metric_core::BAR_TICKS;
    const unsigned cell=lane*preview_per_lane+selected[lane]++;
    view.packed[cell/4]|=(onset | height<<5)<<(8*(cell%4));
  }
  for(unsigned lane=0;lane<preview_lanes;++lane)view.packed[6]|=selected[lane]<<(8*lane);
  return view;
}
}
