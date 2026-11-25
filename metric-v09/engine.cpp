#include "core.h"

namespace metric {
// SDK audio: mono Q31, 44,100 Hz, 64 frames/block.
// This browser audition uses a fixed 120 BPM independently of the OP-XY transport.
constexpr opxy::u32 kSampleRate = 44100u;
constexpr opxy::u32 kBeatFrames = kSampleRate / 2u;
constexpr opxy::u32 kBarFrames = kBeatFrames * 4u;

inline opxy::u32 percent(opxy::i32 value) {
  const opxy::u32 q15 = value < 0 ? 0u : static_cast<opxy::u32>(value) >> 16;
  const opxy::u32 n = (q15 * 100u + 16384u) >> 15;
  return n > 100u ? 100u : n;
}
inline opxy::u32 enum_index(opxy::i32 value, opxy::u32 choices) {
  const opxy::u32 q15 = value < 0 ? 0u : static_cast<opxy::u32>(value) >> 16;
  const opxy::u32 v = (q15 * choices) >> 15;
  return v >= choices ? choices-1 : v;
}
inline metric_core::Settings settings(opxy::Parameters p) {
  metric_core::Settings s;
  s.morph = static_cast<opxy::u8>(percent(p[0]));
  s.density = static_cast<opxy::u8>(percent(p[1]));
  s.rotation = static_cast<opxy::u8>(enum_index(p[2],16));
  const opxy::u8 phrase[6] = {1,2,4,8,16,32};
  s.phrase_bars = phrase[enum_index(p[3],6)];
  s.ratio = static_cast<opxy::u8>(enum_index(p[4],3));
  s.ratchet = static_cast<opxy::u8>(percent(p[5]));
  s.target_a = static_cast<opxy::u8>(enum_index(p[6],8));
  s.target_b = static_cast<opxy::u8>(enum_index(p[7],8));
  return s;
}
inline opxy::u32 event_frame(opxy::u32 tick) {
  // round 96 PPQN to a 120 BPM sample timeline, safely in 32 bits
  return (tick * kBeatFrames + 48u)/96u;
}
inline void initialize(Voice& v, Shared&, opxy::u32) {
  v.frame=0; v.absolute_bar=0; v.next_event=0; v.phase=0;
  v.noise=0x1234abcdu; v.kick_env=0; v.hat_env=0;
  v.kick_increment=0; v.active=1; v.bar.count=0;
}
inline void start(Voice& v, Shared&, opxy::u32) {
  v.frame=0; v.absolute_bar=0; v.next_event=0; v.phase=0;
  v.noise=0x1234abcdu; v.kick_env=0; v.hat_env=0;
  v.kick_increment=0; v.active=1; v.bar.count=0;
}
inline void destroy(Voice& v, Shared&, opxy::u32) { v.active=0; }

inline void begin_bar(Voice& v, const metric_core::Settings& s) {
  metric_core::generate(s,v.absolute_bar,v.bar);
  v.next_event=0;
}

inline void render(Voice& v, Shared&, opxy::u32, opxy::Output out,
                   opxy::Parameters p, opxy::Pitch) {
  const metric_core::Settings s=settings(p);
  if(v.active && v.bar.count==0 && v.frame==0) begin_bar(v,s);
  for(unsigned i=0;i<opxy::block_frames;++i) {
    if(!v.active) {out[i]=0; continue;}
    // Allow live encoder changes to update the future events in the bar.
    // The already elapsed part of the bar is never replayed.
    if(v.frame==0) begin_bar(v,s);
    while(v.next_event < v.bar.count &&
          event_frame(v.bar.events[v.next_event].tick) <= v.frame) {
      const metric_core::Event& e=v.bar.events[v.next_event++];
      if(e.pitch==s.pitch_a) {
        v.kick_env=static_cast<opxy::i32>(18000u + e.velocity*90u);
        v.kick_increment=10500000u;
        v.phase=0;
      } else {
        v.hat_env=static_cast<opxy::i32>(14000u + e.velocity*65u);
      }
    }
    // Triangular bass thump. The pitch falls during the envelope.
    v.phase+=v.kick_increment;
    const opxy::u32 x=v.phase>>16;
    const opxy::i32 triangle=x<32768u ?
      static_cast<opxy::i32>(x*2u)-32768 :
      98304-static_cast<opxy::i32>(x*2u);
    const opxy::i32 kick=(triangle*v.kick_env)>>3;
    if(v.kick_env>0) {
      v.kick_env=v.kick_env>8 ? v.kick_env-8 : 0;
      if(v.kick_increment>4800000u) v.kick_increment-=800u;
    }
    // Bright hat noise with a fast decay.
    v.noise ^= v.noise<<13;
    v.noise ^= v.noise>>17;
    v.noise ^= v.noise<<5;
    const opxy::i32 white=static_cast<opxy::i32>(
      static_cast<opxy::i16>(v.noise>>16));
    const opxy::i32 hat=(white*v.hat_env)>>4;
    if(v.hat_env>0) v.hat_env=v.hat_env>24 ? v.hat_env-24 : 0;
    // Audible audition gain (+14 dB). Worst-case instantaneous mixed peak
    // stays below the declared 0.5 Q31 output limit (about 0.78B Q31).
    // This does not defeat the host note gate: Play phrase still only sounds
    // for as long as its test note remains active.
    out[i]=(kick+hat)*5;
    ++v.frame;
    if(v.frame>=kBarFrames) {v.frame=0; ++v.absolute_bar;}
  }
}
}
