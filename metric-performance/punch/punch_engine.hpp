#pragma once
#include "../metric_engine.hpp"
// METRIC PUNCH v0.9 standalone state resolver; no SDK dependency, allocations,
// dynamic memory, OS API, or floating point. Does NOT modify firmware.
namespace metric_punch {
using metric_core::Settings;
constexpr uint8_t KEY_COUNT = 24;
constexpr uint16_t MORPH = 1u<<0, DENSITY = 1u<<1, ROTATION = 1u<<2,
 PHRASE=1u<<3, RATIO=1u<<4, RATCHET=1u<<5,
 TARGET_A=1u<<6, TARGET_B=1u<<7, PITCH_A=1u<<8, PITCH_B=1u<<9;
constexpr uint16_t ALL = (1u<<10)-1;
enum Transition : uint8_t { IMMEDIATE=0, STEP=1, BEAT=2, BAR=3 };
struct Key {
  Settings snapshot;
  uint16_t mask;
  bool down;
  bool latched;
  bool rotation_additive;
  uint32_t priority;
};
struct Punch {
  Key keys[KEY_COUNT];
  uint32_t sequence;
  Transition quantize;
  // Defaults to momentary keys. Latch is selected per key.
  bool latch_mode;
};
inline void capture(Punch &p, uint8_t index, const Settings& s, uint16_t mask) {
  if(index>=KEY_COUNT)return;
  p.keys[index].snapshot=s; p.keys[index].mask=mask & ALL;
}
inline void press(Punch &p,uint8_t index) {
  if(index>=KEY_COUNT)return;
  Key& k=p.keys[index];
  if(p.latch_mode) {k.latched=!k.latched;k.down=false; if(k.latched)k.priority=++p.sequence;}
  else if(!k.down){k.down=true;k.priority=++p.sequence;}
}
inline void release(Punch &p,uint8_t index){
  if(index<KEY_COUNT)p.keys[index].down=false;
}
inline void clear(Punch &p){
  for(uint8_t i=0;i<KEY_COUNT;++i){p.keys[i].down=false;p.keys[i].latched=false;}
}
inline bool active(const Key& k){return k.down||k.latched;}
inline uint8_t clamp(uint8_t x,uint8_t hi){return x>hi?hi:x;}
inline Settings resolve(const Punch& p,const Settings& underlying){
  Settings r=underlying;
  const Key* latest[10]={};
  int rotation_delta=0;
  for(uint8_t i=0;i<KEY_COUNT;++i){
    const Key& k=p.keys[i]; if(!active(k))continue;
    if((k.mask&ROTATION) && k.rotation_additive) {
      // Interpret 0..15 as signed step offset in [-8,7].
      int v=k.snapshot.rotation%16;
      rotation_delta+=(v>=8?v-16:v);
    }
    for(uint8_t f=0;f<10;++f){
      const uint16_t bit=uint16_t(1u<<f);
      if(!(k.mask&bit) || (bit==ROTATION && k.rotation_additive))continue;
      if(!latest[f] || k.priority>latest[f]->priority) latest[f]=&k;
    }
  }
  if(latest[0])r.morph=clamp(latest[0]->snapshot.morph,100);
  if(latest[1])r.density=clamp(latest[1]->snapshot.density,100);
  if(latest[2])r.rotation=latest[2]->snapshot.rotation%16;
  if(latest[3])r.phrase_bars=metric_core::bounded_phrase(latest[3]->snapshot.phrase_bars);
  if(latest[4])r.ratio=clamp(latest[4]->snapshot.ratio,2);
  if(latest[5])r.ratchet=clamp(latest[5]->snapshot.ratchet,100);
  if(latest[6])r.target_a=clamp(latest[6]->snapshot.target_a,7);
  if(latest[7])r.target_b=clamp(latest[7]->snapshot.target_b,7);
  if(latest[8])r.pitch_a=clamp(latest[8]->snapshot.pitch_a,127);
  if(latest[9])r.pitch_b=clamp(latest[9]->snapshot.pitch_b,127);
  r.rotation=uint8_t((int(r.rotation)+rotation_delta%16+16)%16);
  return r;
}
inline uint32_t boundary(uint32_t now,Transition q){
  uint32_t grid=q==BAR?metric_core::BAR_TICKS:q==BEAT?metric_core::PPQN:q==STEP?metric_core::PPQN/4:1;
  // Immediate=next tick; others round to next grid boundary unless aligned.
  return ((now+grid-1)/grid)*grid;
}
struct Scheduled {
  Settings live, pending;
  uint32_t apply_at;
  bool pending_valid;
};
inline void request(Scheduled& s,const Punch& p,const Settings& base,uint32_t now){
  Settings next=resolve(p,base);
  if(p.quantize==IMMEDIATE){s.live=next;s.pending_valid=false;return;}
  s.pending=next;s.apply_at=boundary(now,p.quantize);s.pending_valid=true;
  if(s.apply_at<=now){s.live=next;s.pending_valid=false;}
}
inline void advance(Scheduled& s,uint32_t now){
  if(s.pending_valid && now>=s.apply_at){s.live=s.pending;s.pending_valid=false;}
}
} // namespace metric_punch
