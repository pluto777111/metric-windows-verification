#include "../control.cpp"
#include <cassert>

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
  input.page_key=2;
  input.transport.playing=1;
  control(state,input,overlay);
  assert(overlay.display[9]==2);
  assert(state.bar.count==6);
  for (unsigned i=0;i<state.bar.count;++i) assert(state.bar.events[i].pitch>=48);
  input.page_key=3;
  input.transport.ticks96=384;
  control(state,input,overlay);
  assert(overlay.display[9]==3);
  assert(state.bar.count>6);
  input.page_key=1;
  input.transport.ticks96=768;
  control(state,input,overlay);
  assert(overlay.display[9]==1);
  for (unsigned i=0;i<state.bar.count;++i) assert(state.bar.events[i].pitch<48);
}
