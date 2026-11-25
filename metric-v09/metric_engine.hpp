#pragma once
// METRIC v0.4.1: freestanding portable deterministic rhythm core.
// All calculations are integer-only; no heap allocation or audio DSP.
// Open-XY preview uses a freestanding compiler without C++ standard headers.
// These fixed-width unsigned types are valid for its 8/16/32-bit target ABI.
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

namespace metric_core {
constexpr uint32_t PPQN = 96;
constexpr uint32_t BAR_TICKS = PPQN * 4;
constexpr uint32_t MAX_EVENTS = 128;

struct Settings {
  uint8_t morph;          // 0..100
  uint8_t density;       // 0..100
  uint8_t rotation;       // 0..15
  uint8_t phrase_bars;    // 1,2,4,8,16,32
  uint8_t target_a;       // 0..7 instrument track
  uint8_t target_b;
  uint8_t pitch_a;       // MIDI kick
  uint8_t pitch_b;       // MIDI closed hat
  uint8_t ratio;         // 0=Euclidean, 1=3:2, 2=5:4
  uint8_t ratchet;       // 0..100, extra subdivisions on lane A
};
struct Event { uint16_t tick; uint8_t target, pitch, velocity; uint16_t duration; };
struct Bar { uint16_t count; Event events[MAX_EVENTS]; };

inline uint8_t clamp100(uint8_t x) {return x>100?100:x;}
inline uint8_t bounded_phrase(uint8_t n) {return n==1||n==2||n==4||n==8||n==16||n==32?n:4;}
inline bool euclid(uint32_t step,uint32_t hits,uint32_t steps,uint32_t rotation=0) {
  if (!steps || !hits) return false;
  if (hits>=steps) return true;
  const uint32_t k=(step+steps-rotation%steps)%steps;
  return ((k+1)*hits)/steps != (k*hits)/steps;
}
inline void insert(Bar& bar, uint16_t tick, uint8_t target, uint8_t pitch, uint8_t velocity, uint16_t duration) {
  if (bar.count>=MAX_EVENTS || tick>=BAR_TICKS) return;
  uint16_t i=bar.count++;
  while (i>0 && bar.events[i-1].tick>tick) {bar.events[i]=bar.events[i-1];--i;}
  bar.events[i]={tick,target,pitch,velocity,duration};
}
inline void generate(const Settings& s, uint32_t absolute_bar, Bar& out) {
  out.count=0;
  const uint32_t morph=clamp100(s.morph);
  const uint32_t density=clamp100(s.density);
  const uint32_t phrase=bounded_phrase(s.phrase_bars);
  const uint32_t local_bar=absolute_bar%phrase;
  // Lane A: 4-on-the-floor baseline. Morph adds 0..4 Euclidean accents
  // at the 16th-note grid, without removing the downbeats.
  const uint32_t extras=(morph*4+50)/100;
  for (uint32_t i=0;i<16;++i) {
    const bool base=(i%4)==0;
    const bool extra=extras && euclid(i,extras,16,s.rotation);
    if (base || extra) insert(out,uint16_t(i*24),s.target_a,s.pitch_a,base?108:77,18);
  }
  // Lane B: Euclidean at 1:1, three pulses per two beats at 3:2,
  // or five evenly placed pulses per bar at 5:4. 96 PPQN quantization.
  const uint32_t hits=3+(density*8+50)/100;
  if(s.ratio == 1 || s.ratio == 2) {
    // Keep the defining ratio pulses fixed at every Density setting.
    // Density adds 0..8 quieter off-grid subdivisions, which are both
    // audible and visible on the precise-tick display. At 0, only the
    // underlying 3:2 or 5:4 rhythm remains.
    if(s.ratio == 1) {
      for(uint32_t i=0;i<6;++i)
        insert(out,uint16_t(i*64),s.target_b,s.pitch_b,90,12);
    } else {
      for(uint32_t i=0;i<5;++i)
        insert(out,uint16_t((i*BAR_TICKS+2)/5),s.target_b,s.pitch_b,90,12);
    }
    const uint32_t ornaments=(density*8+50)/100;
    for(uint32_t i=0;i<16;++i)
      if(euclid(i,ornaments,16,(s.rotation+local_bar)%16))
        insert(out,uint16_t(i*24+12),s.target_b,s.pitch_b,62,8);
  } else if(morph<65) {
    for(uint32_t i=0;i<16;++i)
      if(euclid(i,hits,16,(s.rotation+local_bar)%16))
        insert(out,uint16_t(i*24),s.target_b,s.pitch_b,83,12);
  } else {
    // Triplet quarters: 3 attacks in a 2-beat span (64 ticks per attack).
    // Mix with Euclidean on the same lane; duplicate tick avoided here.
    for(uint32_t i=0;i<6;++i) insert(out,uint16_t(i*64),s.target_b,s.pitch_b,90,12);
    if(morph>=90) {
      for(uint32_t i=0;i<16;++i)
        if(euclid(i,hits/2,16,(s.rotation+local_bar)%16) && (i%8)!=0)
          insert(out,uint16_t(i*24+12),s.target_b,s.pitch_b,65,8);
    }
  }
  // Optional single retrigger between lane A grid steps (bounded and
  // deterministic). Keeps the original attack while adding a short repeat.
  if(s.ratchet) {
    const uint32_t threshold=clamp100(s.ratchet);
    for(uint32_t i=0;i<16;++i) {
      if((i%4)==0 && ((i*37u+local_bar*13u)%100u)<threshold)
        insert(out,uint16_t(i*24+12),s.target_a,s.pitch_a,65,8);
    }
  }
}
} // namespace metric
