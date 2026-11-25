#include "../control.cpp"
#include <cassert>
#include <cstdio>
using namespace metric;
static opxy::i32 q(unsigned n,unsigned choices){
  return static_cast<opxy::i32>(((2u*n+1u)*32768u/(2u*choices))<<16);
}
static void tick(ControlState& st,opxy::control::Input& in,opxy::control::Overlay& o,unsigned now){
  o.note_count=0; in.transport.ticks96=now; control(st,in,o);
}
int main(){
  ControlState st{}; control_reset(st);
  opxy::control::Input in{}; opxy::control::Overlay out{};
  in.track=12; in.transport.playing=1; in.transport.bpm_x10=1200;
  in.values[12][0]=static_cast<opxy::i32>(0.3*2147483647.0);
  in.values[12][1]=static_cast<opxy::i32>(0.5*2147483647.0);
  in.values[12][3]=q(2,6); // four-bar phrase
  in.values[12][4]=q(0,3);
  in.values[12][6]=q(2,8); // target A: track 3 (zero based 2)
  in.values[12][7]=q(6,8); // target B: track 7 (zero based 6)
  unsigned total=0, kick=0, hat=0;
  for(unsigned t=0;t<384;++t){
    tick(st,in,out,t);
    for(unsigned j=0;j<out.note_count;++j){
      const auto& n=out.notes[j];
      assert(n.track==2 || n.track==6);
      assert(n.pitch==(n.track==2?36u:42u));
      assert(n.length_us>0 && n.length_us<=opxy::control::longest_note_us);
      ++total; if(n.track==2) ++kick; else ++hat;
    }
  }
  assert(total>10 && kick>=4 && hat>=3);
  // Mute moves transport forward while suppressing all outbound notes.
  in.muted=1;
  for(unsigned t=384;t<768;++t){tick(st,in,out,t); assert(!out.note_count);}
  // Explicit ratio and new lane route apply when transport continues.
  in.muted=0; in.values[12][4]=q(1,3); in.values[12][7]=q(4,8);
  unsigned new_target=0;
  for(unsigned t=768;t<1152;++t){
    tick(st,in,out,t);
    for(unsigned j=0;j<out.note_count;++j){
      assert(out.notes[j].track==2 || out.notes[j].track==4);
      if(out.notes[j].track==4) ++new_target;
    }
  }
  assert(new_target==10); // 3:2 skeleton (6) + density 50 ornaments (4)
  assert(st.sent_a>0 && st.sent_b>0);
  assert(static_cast<unsigned>(out.display[5])==st.sent_a);
  assert(static_cast<unsigned>(out.display[6])==st.sent_b);
  // Stop: no note routing whatsoever.
  in.transport.playing=0;
  for(unsigned t=1152;t<1190;++t){tick(st,in,out,t);assert(!out.note_count);}
  // Restart from time zero must play downbeat again.
  in.transport.playing=1; tick(st,in,out,0);
  assert(out.note_count>=1);
  assert(st.sent_a>=1 && st.sent_b>=1);
  const unsigned downbeat_count=out.note_count;
  tick(st,in,out,0); // repeated host tick must not retrigger
  assert(out.note_count==0);
  // Tempo changes duration in microseconds, not tick-based note positions.
  in.transport.playing=0;tick(st,in,out,0);
  in.transport.playing=1;in.transport.bpm_x10=600;
  tick(st,in,out,0);
  assert(out.note_count==downbeat_count);
  const unsigned slow_duration=out.notes[0].length_us;
  in.transport.playing=0;tick(st,in,out,0);
  in.transport.playing=1;in.transport.bpm_x10=1200;
  tick(st,in,out,0);
  assert(out.note_count==downbeat_count);
  assert(slow_duration>=out.notes[0].length_us*2-100);
  // Cache: same bar and same parameters must not regenerate each tick.
  assert(st.generated);
  auto prior=st.bar.events[0];
  tick(st,in,out,1);
  assert(st.bar.events[0].tick==prior.tick);
  puts("PASS: transport/routing, density ornament counts, counters, mute, tempo, stop/restart and cache");
}
