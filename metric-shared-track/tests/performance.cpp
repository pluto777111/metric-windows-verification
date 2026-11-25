#include "../control.cpp"
#include <cassert>

static unsigned source_reads;
static bool read_source(opxy::u32 track, opxy::control::PatternEvent* events,
                        opxy::u32 capacity, opxy::u32& count) {
  assert(track==0 && capacity>=120);
  ++source_reads;
  events[0]={0,24,60,90};
  events[1]={96,24,65,105};
  events[2]={192,48,67,80};
  count=3;
  return true;
}

int main() {
  using namespace metric;
  ControlState state{};
  control_reset(state);
  auto master=state.settings;
  auto snapshot=master;
  snapshot.density=91;
  metric_punch::capture(state.performance.phrases.input.punch,23,snapshot,metric_punch::DENSITY);
  observe_punch_key(state.performance,23,true);
  assert(effective_settings(state.performance,master).density==91);
  snapshot.density=15;
  snapshot.morph=88;
  metric_punch::capture(state.performance.phrases.input.punch,0,snapshot,
                        metric_punch::DENSITY|metric_punch::MORPH);
  observe_punch_key(state.performance,0,true);
  assert(effective_settings(state.performance,master).density==15);
  assert(effective_settings(state.performance,master).morph==88);
  observe_punch_key(state.performance,0,false);
  assert(effective_settings(state.performance,master).density==91);
  assert(effective_settings(state.performance,master).morph==master.morph);
  observe_punch_key(state.performance,23,false);
  assert(effective_settings(state.performance,master).density==master.density);

  opxy::control::Input input{};
  opxy::control::Overlay overlay{};
  input.track=12;
  input.transport.playing=1;
  control(state,input,overlay);
  const auto first=state.bar;
  assert(first.count>0 && overlay.own_turns==0);
  input.page_key=2;
  control(state,input,overlay);
  assert(overlay.display[9]==2 && overlay.own_turns==1);
  assert(state.bar.count==first.count);
  for(unsigned i=0;i<first.count;++i) {
    assert(state.bar.events[i].pitch==first.events[i].pitch);
    assert(state.bar.events[i].tick==first.events[i].tick);
  }
  input.page_key=0;
  input.turn_count=1;
  input.turns[0].slot=0;
  input.turns[0].direction=5;
  control(state,input,overlay);
  assert(state.performance.distance==20 && state.bar.events[0].pitch==first.events[0].pitch);
  input.turns[0].slot=7;
  input.turns[0].direction=2;
  control(state,input,overlay);
  assert(state.performance.pace==3);
  input.turn_count=0;
  input.page_key=1;
  input.transport.ticks96=384;
  control(state,input,overlay);
  assert(overlay.display[9]==1 && overlay.own_turns==0 && state.performance.distance==20);

  // Compare future identical workload across two page navigation histories.
  auto other=state;
  input.page_key=2;
  input.transport.ticks96=1152;
  control(state,input,overlay);
  input.page_key=3;
  control(other,input,overlay);
  assert(state.bar.count==other.bar.count);
  for(unsigned i=0;i<state.bar.count;++i) {
    const auto& a=state.bar.events[i];
    const auto& b=other.bar.events[i];
    assert(a.tick==b.tick && a.pitch==b.pitch && a.target==b.target);
  }
  assert(output_pitch(OutputMode::drum,1,11,5)==38);
  assert(output_pitch(OutputMode::tonal,1,7,5)==56);
  assert(output_pitch(OutputMode::scale_lock,2,7,0)==59);
  assert(output_pitch(OutputMode::scale_lock,2,7,1)==58);
  assert(output_pitch(OutputMode::scale_lock,6,0,5)==59);
  assert(root_from_percent(100)==11 && scale_from_bars(32)==5);

  ControlState live{};
  control_reset(live);
  opxy::control::Input context{};
  context.track=12;
  context.values[12][7]=0x18000000; // audible companion on track B, zero-based 1
  context.transport.playing=1;
  context.read_pattern=read_source;
  overlay={};
  control(live,context,overlay);
  assert(source_reads==1 && live.bar.count==3 && overlay.display[14]==1);
  assert(live.bar.events[0].pitch==60 && live.bar.events[0].target==1);
  assert(live.bar.events[1].pitch==65 && live.bar.events[1].tick==96);
  assert(live.bar.events[2].duration==48 && live.bar.events[2].pitch==67);
  context.page_key=2;
  control(live,context,overlay);
  assert(source_reads==1 && live.bar.count==3);
  context.values[12][7]=0;
  context.transport.ticks96=384;
  control(live,context,overlay);
  assert(source_reads==1 && live.bar.count==0); // never double the stock source


}
