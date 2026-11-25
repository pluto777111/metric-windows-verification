#include <opxy/track_control.hpp>
#include "metric_engine.hpp"
#include "core.h"
#include "native_performance.hpp"

namespace metric {
using opxy::u32;
using opxy::i32;
constexpr u32 kVersion = 0x4d545235; // MTR5

struct ControlState {
  u32 version;
  u32 next_tick, last_tick;
  u32 current_bar;
  u32 playing, generated;
  u32 values[8];
  metric_core::Settings settings;
  metric_core::Bar bar;
  u32 event_count;
  u32 sent_a, sent_b; // session-local routed note counters
  metric_core::Settings cached_settings;
  Performance performance;
};

inline u32 decode(i32 raw,u32 choices) {
  const u32 q15=raw<0?0u:(static_cast<u32>(raw)>>16);
  if(choices) { const u32 v=(q15*choices)>>15; return v<choices?v:choices-1; }
  return (q15*100u+16384u)>>15;
}
inline void update(ControlState& st, const opxy::control::Input& in) {
  for(u32 i=0;i<8;++i) st.values[i]=decode(in.values[in.track][i],
    i==2?16u:i==3?6u:i==4?3u:i==6||i==7?8u:0u);
  auto& s=st.settings;
  s.morph=static_cast<uint8_t>(st.values[0]);
  s.density=static_cast<uint8_t>(st.values[1]);
  s.rotation=static_cast<uint8_t>(st.values[2]);
  static const uint8_t phrase[6]={1,2,4,8,16,32};
  s.phrase_bars=phrase[st.values[3]];
  s.ratio=static_cast<uint8_t>(st.values[4]);
  s.ratchet=static_cast<uint8_t>(st.values[5]);
  s.target_a=static_cast<uint8_t>(st.values[6]);
  s.target_b=static_cast<uint8_t>(st.values[7]);
  s.pitch_a=36; s.pitch_b=42;
}
inline bool same_pattern(const metric_core::Settings& a, const metric_core::Settings& b) {
  return a.morph==b.morph && a.density==b.density &&
    a.rotation==b.rotation && a.phrase_bars==b.phrase_bars &&
    a.ratio==b.ratio && a.ratchet==b.ratchet &&
    a.target_a==b.target_a && a.target_b==b.target_b &&
    a.pitch_a==b.pitch_a && a.pitch_b==b.pitch_b;
}
inline void ensure_bar(ControlState& st,u32 index) {
  if(st.generated && st.current_bar==index &&
     same_pattern(st.settings,st.cached_settings)) return;
  if (st.performance.page>=2) {
    if (st.generated && st.current_bar==index) return;
    st.bar.count=0;
    audition_phrase(st.performance,st.settings,index,st.bar);
  } else {
    metric_core::generate(st.settings,index,st.bar);
  }
  st.current_bar=index;
  st.cached_settings=st.settings;
  st.generated=1;
}
inline void control_reset(ControlState& st) {
  st.version=kVersion; st.next_tick=0; st.last_tick=0;
  st.current_bar=0; st.playing=0; st.generated=0; st.event_count=0;
  st.sent_a=0; st.sent_b=0;
  for(u32 i=0;i<8;++i) st.values[i]=0;
  st.settings={0,50,0,4,0,1,36,42,0,0};
  st.cached_settings=st.settings;
  st.bar.count=0;
  st.performance.page=1;
  st.performance.audition_ready=false;
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
  if (in.page_key>=1 && in.page_key<=3 && in.page_key!=st.performance.page) {
    st.performance.page=in.page_key;
    st.performance.audition_ready=false;
    st.generated=0;
  }
  st.settings=effective_settings(st.performance,st.settings);
  const u32 now=in.transport.ticks96;
  overlay.own_turns=false; // native M1 controls (incl. recorded locks)
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
      ensure_bar(st,bar_index);
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
        if(e.pitch==st.settings.pitch_a) ++st.sent_a;
        else if(e.pitch==st.settings.pitch_b) ++st.sent_b;
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
  for(u32 i=10;i<16;++i)overlay.display[i]=0;
}
}
