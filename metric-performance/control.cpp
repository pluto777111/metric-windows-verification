#include <opxy/track_control.hpp>
#include "metric_engine.hpp"
#include "core.h"
#include "native_performance.hpp"

namespace metric {
using opxy::u32;
using opxy::i32;
constexpr u32 kVersion = 0x4d545237; // MTR7

struct ControlState {
  u32 version;
  u32 next_tick, last_tick;
  u32 current_bar;
  u32 playing, generated;
  u32 values[8];
  metric_core::Settings settings;
  metric_core::Bar bar;
  uint8_t lanes[metric_core::MAX_EVENTS];
  u32 event_count;
  u32 sent_a, sent_b; // session-local routed note counters
  Performance performance;
};

inline u32 decode(i32 raw,u32 choices) {
  const u32 q15=raw<0?0u:(static_cast<u32>(raw)>>16);
  if(choices) { const u32 v=(q15*choices)>>15; return v<choices?v:choices-1; }
  return (q15*100u+16384u)>>15;
}
inline void update(ControlState& st, const opxy::control::Input& in) {
  for(u32 i=0;i<8;++i) st.values[i]=decode(in.values[in.track][i],
    i==2?16u:i==3?7u:i==4?3u:i==6||i==7?8u:0u);
  auto& s=st.settings;
  s.morph=static_cast<uint8_t>(st.values[0]);
  s.density=static_cast<uint8_t>(st.values[1]);
  s.rotation=static_cast<uint8_t>(st.values[2]);
  static const uint8_t phrase[7]={1,2,4,8,16,32,64};
  s.phrase_bars=phrase[st.values[3]];
  s.ratio=static_cast<uint8_t>(st.values[4]);
  s.ratchet=static_cast<uint8_t>(st.values[5]);
  s.target_a=static_cast<uint8_t>(st.values[6]);
  s.target_b=static_cast<uint8_t>(st.values[7]);
  s.pitch_a=36; s.pitch_b=42;
}
inline unsigned adjust(unsigned value, i32 detents, unsigned maximum) {
  const i32 next=static_cast<i32>(value)+detents;
  return next<0 ? 0u : next>static_cast<i32>(maximum) ? maximum : static_cast<unsigned>(next);
}
inline void edit_performance(Performance& p, const opxy::control::Input& in) {
  for (u32 i=0; i<in.turn_count && i<opxy::control::turn_capacity; ++i) {
    const auto& turn=in.turns[i];
    const unsigned knob=turn.slot%4;
    const auto detents=turn.direction;
    if (p.page==4) {
      if (knob==0) p.anchor_cursor=adjust(p.anchor_cursor,detents,3);
      if (knob==1) {
        const unsigned bit=1u<<p.anchor_cursor;
        if(detents>0) p.anchor_mask|=bit;
        else if(detents<0) p.anchor_mask&=~bit;
      }
      if (knob==2) p.anchor_quality[p.anchor_cursor]=
        static_cast<int>(adjust(p.anchor_quality[p.anchor_cursor]+1,detents,2))-1;
      if (knob==3) p.evolve=adjust(p.evolve,detents*4,100);
    } else if (p.page==3) {
      if (in.shift || turn.slot>=4) {
        if (knob==0) p.target_c=adjust(p.target_c,detents,7);
        if (knob==1) p.target_d=adjust(p.target_d,detents,7);
      } else {
        if (knob==0) p.output=adjust(p.output,detents,2);
        if (knob==1) p.root=adjust(p.root,detents,11);
        if (knob==2) p.scale=adjust(p.scale,detents,scale_count-1);
        if (knob==3) p.source_mode=adjust(p.source_mode,detents,2);
      }
    } else if (in.shift || turn.slot>=4) {
      if (knob==0) p.interplay=adjust(p.interplay,detents,2);
      if (knob==1) p.destination=adjust(p.destination,detents,1);
      if (knob==2) p.journey=adjust(p.journey,detents,2);
      if (knob==3) p.pace=adjust(p.pace-1,detents,15)+1;
    } else {
      if (knob==0) p.distance=adjust(p.distance,detents*4,100);
      if (knob==1) p.pitch_motion=adjust(p.pitch_motion,detents*4,100);
      if (knob==2) p.freedom=adjust(p.freedom,detents*4,100);
      if (knob==3) p.tension=adjust(p.tension,detents*4,100);
    }
  }
}
inline void protect_programmed_notes(ControlState& st,const opxy::control::Input& in) {
  bool dropped[metric_core::MAX_EVENTS]{};
  unsigned checked=0;
  opxy::control::PatternEvent stock[120];
  for(unsigned i=0;i<st.bar.count;++i) {
    const unsigned target=st.bar.events[i].target;
    const unsigned bit=1u<<target;
    if(checked&bit) continue;
    checked|=bit;
    u32 count=0;
    if(!in.read_pattern(target,stock,120,count) || count>120) continue;
    for(unsigned j=i;j<st.bar.count;++j) {
      const auto& event=st.bar.events[j];
      if(event.target!=target) continue;
      for(unsigned k=0;k<count;++k) {
        if(stock[k].pitch!=event.pitch) continue;
        constexpr unsigned cycle=metric_core::BAR_TICKS;
        const unsigned after_stock=(event.tick+cycle-stock[k].tick)%cycle;
        const unsigned before_stock=(stock[k].tick+cycle-event.tick)%cycle;
        if(!after_stock || after_stock<stock[k].length || before_stock<event.duration) {
          dropped[j]=true;
          break;
        }
      }
    }
  }
  unsigned keep=0;
  for(unsigned i=0;i<st.bar.count;++i) if(!dropped[i]) {
    st.bar.events[keep]=st.bar.events[i];
    st.lanes[keep++]=st.lanes[i];
  }
  st.bar.count=static_cast<uint16_t>(keep);
}
inline void ensure_bar(ControlState& st,u32 index,const opxy::control::Input& in) {
  if(st.generated && st.current_bar==index) return;
  st.bar.count=0;
  const auto& p=st.performance;
  if(p.source_mode!=1)
    render_generated_lanes(p,st.settings,index,st.bar,st.lanes);
  if(p.source_mode!=0) {
    if(in.read_pattern) {
      if(st.settings.target_a!=st.settings.target_b) {
        opxy::control::PatternEvent notes[120];
        u32 count=0;
        if(in.read_pattern(st.settings.target_a,notes,120,count) && count<=120)
          render_programmed_phrase(p,st.settings,index,notes,count,st.bar,st.lanes);
      }
    } else if(p.source_mode==1) {
      render_shared_phrase(st.performance,st.settings,index,st.bar,st.lanes);
    }
  }
  if(in.read_pattern) protect_programmed_notes(st,in);
  st.current_bar=index;
  st.generated=1;
}
inline void control_reset(ControlState& st) {
  st.version=kVersion; st.next_tick=0; st.last_tick=0;
  st.current_bar=0; st.playing=0; st.generated=0; st.event_count=0;
  st.sent_a=0; st.sent_b=0;
  for(u32 i=0;i<8;++i) st.values[i]=0;
  st.settings={0,50,0,4,0,1,36,42,0,0};
  st.bar.count=0;
  st.performance.page=1;
  st.performance.audition_ready=false;
  st.performance.distance=0; st.performance.tension=0; st.performance.interplay=0;
  st.performance.freedom=25; st.performance.target=2; st.performance.destination=1;
  st.performance.journey=0; st.performance.pace=1;
  st.performance.output=2; st.performance.root=0; st.performance.scale=1;
  st.performance.source_mode=0; st.performance.target_c=2; st.performance.target_d=3;
  st.performance.pitch_motion=0; st.performance.evolve=0; st.performance.anchor_cursor=0;
  st.performance.anchor_mask=9;
  for(unsigned i=0;i<4;++i) st.performance.anchor_quality[i]=i==2 ? 1 : 0;
  auto& punch=st.performance.phrases.input.punch;
  for (auto& key : punch.keys) key=metric_punch::Key{};
  punch.sequence=0;
  punch.latch_mode=false;
  punch.quantize=metric_punch::STEP;
  st.performance.phrases.rhythm=st.settings;
  st.performance.phrases.scheduled=metric_punch::Scheduled{};
  st.performance.phrases.input.armed=false;
  st.performance.phrases.input.first_note=60;
  for (auto& down : st.performance.phrases.input.observed_down) down=false;
}
inline u32 control_save(const ControlState&, opxy::u8* data,u32 capacity) {
  if(capacity<4)return 0;
  for(u32 i=0;i<4;++i)data[i]=static_cast<opxy::u8>(kVersion>>(8*i));
  return 4;
}
inline void control_load(ControlState& st,const opxy::u8*,u32) {control_reset(st);}

// Note playback is advanced in transport-tick order and never scheduled
// from the audio thread. M1's eight saved native controls remain OS-owned.
inline void control(ControlState& st,const opxy::control::Input& in,opxy::control::Overlay& overlay) {
  update(st,in);
  // The previous callback decides which page owned any queued turns.
  if (st.performance.page>=2) edit_performance(st.performance,in);
  if (in.page_key>=1 && in.page_key<=4) st.performance.page=in.page_key;
  st.settings=effective_settings(st.performance,st.settings);
  const u32 now=in.transport.ticks96;
  overlay.own_turns=st.performance.page>=2;
  if(!in.transport.playing) {
    st.playing=0; st.generated=0; st.next_tick=now;
  } else {
    if(!st.playing) { st.sent_a=0; st.sent_b=0; }
    if(!st.playing || now<st.last_tick || ((now>=st.next_tick) && (now-st.next_tick>metric_core::BAR_TICKS))) {
      st.next_tick=now; st.generated=0;
    }
    st.playing=1;
    const u32 tick_us=600000000u/((in.transport.bpm_x10?in.transport.bpm_x10:1200u)*96u);
    while(st.next_tick<=now) {
      const u32 bar_index=st.next_tick/metric_core::BAR_TICKS;
      ensure_bar(st,bar_index,in);
      const u32 bar_start=bar_index*metric_core::BAR_TICKS;
      const u32 last=bar_start+metric_core::BAR_TICKS-1;
      const u32 until=now<last?now:last;
      for(u32 i=0;i<st.bar.count;++i) {
        const auto& e=st.bar.events[i];
        const u32 at=bar_start+e.tick;
        if(at<st.next_tick||at>until||in.muted)continue;
        const u32 duration=e.duration*tick_us;
        opxy::control::play(overlay,e.target,e.pitch,e.velocity,
           duration<opxy::control::longest_note_us?duration:opxy::control::longest_note_us);
        ++st.event_count;
        if(st.lanes[i]==0) ++st.sent_a;
        else if(st.lanes[i]==1) ++st.sent_b;
      }
      st.next_tick=until+1;
    }
  }
  st.last_tick=now;
  // Status values for the machine display, decoded from the same native
  // parameters that are used by the rhythmic engine.
  overlay.display[0]=static_cast<i32>(now);
  overlay.display[1]=static_cast<i32>(st.values[0] | st.values[1]<<8 | st.values[2]<<16 | st.values[3]<<24);
  overlay.display[2]=static_cast<i32>(st.values[4] | st.values[5]<<8 | st.values[6]<<16 | st.values[7]<<24);
  overlay.display[3]=static_cast<i32>(st.playing);
  overlay.display[4]=static_cast<i32>(st.event_count);
  overlay.display[5]=static_cast<i32>(st.sent_a);
  overlay.display[6]=static_cast<i32>(st.sent_b);
  overlay.display[7]=static_cast<i32>(in.muted);
  overlay.display[8]=static_cast<i32>(in.transport.bpm_x10);
  overlay.display[9]=st.performance.page;
  const auto& p=st.performance;
  overlay.display[10]=p.distance | p.pitch_motion<<8 | p.freedom<<16 | p.tension<<24;
  overlay.display[11]=p.interplay | p.destination<<8 | p.journey<<16 | p.pace<<24;
  overlay.display[12]=p.output | p.root<<8 | p.scale<<16 | p.source_mode<<24;
  overlay.display[13]=in.shift;
  overlay.display[14]=p.target_c | p.target_d<<8;
  overlay.display[15]=p.anchor_cursor | ((p.anchor_mask>>p.anchor_cursor)&1u)<<8 |
    static_cast<unsigned>(p.anchor_quality[p.anchor_cursor]+1)<<16 | p.evolve<<24;
}
}
