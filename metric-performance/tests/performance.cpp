#include "../control.cpp"
#include <cassert>

static unsigned source_reads;
static opxy::control::PatternTiming source_timing{384,0};
static bool read_source(opxy::u32 track, opxy::control::PatternEvent* events,
                        opxy::u32 capacity, opxy::u32& count,
                        opxy::control::PatternTiming& timing) {
  assert(capacity>=120);
  if(track!=0) return false;
  ++source_reads;
  events[0]={0,24,60,90};
  events[1]={96,24,65,105};
  events[2]={192,48,67,80};
  count=3;
  timing=source_timing;
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
  input.values[12][7]=0x18000000;
  input.transport.playing=1;
  control(state,input,overlay);
  const auto first=state.bar;
  unsigned routed=0;
  for(unsigned i=0;i<first.count;++i) routed|=1u<<state.lanes[i];
  assert(routed==15);
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
  input.shift=1;
  input.turns[0].slot=3;
  input.turns[0].direction=2;
  control(state,input,overlay);
  assert(state.performance.pace==3);
  input.shift=0;
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

  for(unsigned scale=0;scale<scale_count;++scale) {
    for(unsigned root=0;root<12;++root) {
      for(unsigned pitch=0;pitch<128;++pitch) {
        const unsigned locked=locked_pitch(pitch,root,scale);
        assert(locked<128 && in_scale(locked,root,scale));
      }
    }
  }

  Performance evolution{};
  evolution.anchor_mask=9;
  assert(evolved_pitch(evolution,72,2,60)==72);
  evolution.evolve=100;
  assert(evolved_pitch(evolution,72,0,60)==72);
  assert(evolved_pitch(evolution,72,2,60)==60);
  assert(evolved_pitch(evolution,72,3,60)==72);
  evolution.anchor_span=2;
  assert(evolved_pitch(evolution,72,1,60)==72);
  assert(evolved_pitch(evolution,72,2,60)==60);
  assert(shifted_octave(60,1)==72 && shifted_octave(12,-2)==0);
  assert(shifted_octave(121,2)==121);

  ControlState controls{};
  control_reset(controls);
  opxy::control::Input edits{};
  opxy::control::Overlay feedback{};
  edits.track=11;
  edits.page_key=2;
  control(controls,edits,feedback);
  edits.page_key=0;
  edits.turn_count=2;
  edits.turns[0]={1,0,0};
  edits.turns[1]={1,0,1};
  control(controls,edits,feedback);
  assert(controls.performance.octave==1 && controls.performance.pitch_motion==0);
  edits.shift=1;
  edits.turn_count=1;
  edits.turns[0]={0,0,1};
  control(controls,edits,feedback);
  assert(controls.performance.interplay==1 && controls.performance.distance==0);
  edits.shift=0;
  edits.turn_count=0;
  edits.page_key=4;
  control(controls,edits,feedback);
  control(controls,edits,feedback);
  edits.page_key=0;
  edits.turn_count=1;
  edits.turns[0]={3,0,2};
  control(controls,edits,feedback);
  assert(controls.performance.anchor_span==3 && controls.performance.evolve==0);

  edits.page_key=3;
  edits.turn_count=0;
  control(controls,edits,feedback);
  edits.page_key=0;
  edits.shift=1;
  edits.turn_count=4;
  for(unsigned i=0;i<4;++i) edits.turns[i]={static_cast<opxy::u8>(i),0,1};
  control(controls,edits,feedback);
  assert(controls.settings.target_a==0 && controls.performance.target_c==2);
  edits.shift=0;
  edits.turn_count=0;
  control(controls,edits,feedback);
  assert(controls.values[6]==0 && controls.values[7]==0);
  assert(controls.settings.target_a==1 && controls.settings.target_b==1);
  assert(controls.performance.target_c==3 && controls.performance.target_d==4);
  edits.shift=0;
  edits.turn_count=3;
  edits.turns[0]={0,0,0};
  edits.turns[1]={0,0,1};
  edits.turns[2]={2,0,-1};
  control(controls,edits,feedback);
  assert(controls.performance.lane_octave[0]==1 && controls.performance.lane_octave[2]==-1);
  assert(controls.performance.lane_octave[1]==0 && controls.performance.lane_octave[3]==0);
  assert(controls.settings.target_a==1 && controls.performance.target_c==3);
  edits.turn_count=0;
  edits.page_key=2;
  control(controls,edits,feedback);
  assert(controls.performance.editing_octave && controls.performance.editing_anchor_scale);
  edits.page_key=3;
  control(controls,edits,feedback);
  assert(controls.performance.editing_lane_octaves);
  opxy::u8 saved[project_header];
  assert(control_save(controls,saved,sizeof(saved))==sizeof(saved));
  ControlState restored{};
  control_load(restored,saved,sizeof(saved));
  assert(restored.performance.target_c==3 && restored.performance.target_d==4);
  assert(restored.performance.lane_octave[0]==1 && restored.performance.lane_octave[2]==-1);
  assert(restored.performance.off_mask==0);
  edits.page_key=0;
  edits.shift=1;
  edits.turn_count=4;
  for(unsigned i=0;i<4;++i)edits.turns[i]={static_cast<opxy::u8>(i),0,-8};
  control(controls,edits,feedback);
  assert(feedback.display[14]==0);
  edits.shift=0;
  edits.turn_count=0;
  control(controls,edits,feedback);
  assert(controls.performance.off_mask==15);
  assert(controls.settings.target_a==1 && controls.performance.target_c==3);
  metric_core::Bar silent{};
  uint8_t silent_lanes[metric_core::MAX_EVENTS]{};
  render_generated_lanes(controls.performance,controls.settings,0,silent,silent_lanes);
  assert(silent.count==0);
  assert(control_save(controls,saved,sizeof(saved))==sizeof(saved));
  control_load(restored,saved,sizeof(saved));
  assert(restored.performance.off_mask==15 && restored.performance.target_c==3);
  control_load(restored,saved,4);
  assert(restored.performance.target_c==2 && restored.performance.lane_octave[0]==0);

  edits.page_key=4;
  edits.turn_count=0;
  control(controls,edits,feedback);
  edits.page_key=0;
  edits.host_pattern={768,192,3,16,4};
  edits.turn_count=3;
  edits.turns[0]={1,0,3};
  edits.turns[1]={2,0,2};
  edits.turns[2]={3,0,8};
  control(controls,edits,feedback);
  const unsigned locked=controls.step_cues[3][4];
  assert((locked&3)==(pin_pitch|pin_rhythm) && cue_quality(locked)==2 && cue_evolve(locked)==32);
  assert(controls.step_cues[2][4]==0 && controls.step_cues[3][3]==0);
  assert(static_cast<unsigned>(feedback.display[6])==(1u<<8));
  opxy::u8 project[4108];
  const unsigned bytes=control_save(controls,project,sizeof(project));
  control_load(restored,project,bytes);
  assert(restored.step_cues[3][4]==locked);
  edits.turn_count=1;
  edits.turns[0]={0,0,-1};
  control(controls,edits,feedback);
  assert(controls.step_cues[3][4]==0 && feedback.display[6]==0);

  metric_core::Bar collisions{};
  uint8_t lanes[metric_core::MAX_EVENTS]{};
  append_performance_event(collisions,lanes,{0,1,60,90,48},0,false);
  append_performance_event(collisions,lanes,{0,1,60,95,48},1,true);
  append_performance_event(collisions,lanes,{24,1,60,95,48},1,false);
  append_performance_event(collisions,lanes,{24,1,62,95,48},1,false);
  append_performance_event(collisions,lanes,{24,1,60,95,48},1,true);
  assert(collisions.count==3);
  assert(collisions.events[0].velocity==90 && lanes[0]==0);

  ControlState live{};
  control_reset(live);
  live.performance.source_mode=1;
  opxy::control::Input context{};
  context.track=12;
  context.values[12][7]=0x18000000; // audible companion on track B, zero-based 1
  context.transport.playing=1;
  context.read_pattern=read_source;
  live.performance.off_mask=1; // Suppress A's output even as A supplies the source for B.
  overlay={};
  control(live,context,overlay);
  assert(source_reads==1 && live.bar.count==3);
  assert(live.bar.events[0].pitch==60 && live.bar.events[0].target==1);
  assert(live.bar.events[1].pitch==65 && live.bar.events[1].tick==96);
  assert(live.bar.events[2].duration==48 && live.bar.events[2].pitch==67);
  context.page_key=2;
  control(live,context,overlay);
  assert(source_reads==1 && live.bar.count==3);
  live.output_ab[1]=0;
  context.transport.ticks96=384;
  control(live,context,overlay);
  assert(source_reads==2 && live.bar.count==0); // never double the stock source

  // A programmed note takes priority over an overlapping generated note.
  live.bar.count=0;
  append_performance_event(live.bar,live.lanes,{0,0,60,80,24},0,false);
  append_performance_event(live.bar,live.lanes,{8,0,60,80,8},0,true);
  append_performance_event(live.bar,live.lanes,{0,0,61,80,24},0,false);
  protect_programmed_notes(live,1,context);
  assert(live.bar.count==1 && live.bar.events[0].pitch==61);

  // An uneven, longer cycle follows the source playhead through bar windows.
  Performance tracked{};
  metric_core::Settings source_settings{0,50,0,1,0,1,36,42,0,1};
  const opxy::control::PatternEvent long_notes[]={{0,48,60,90},{432,48,64,90}};
  metric_core::Bar window{};
  assert(pattern_phase_at_bar({504,391},391,1)==384);
  render_programmed_phrase(tracked,source_settings,1,long_notes,2,504,384,window,lanes);
  assert(window.count==2 && window.events[0].tick==48 && window.events[1].tick==120);

  // A short scaled loop repeats within a single METRIC bar.
  const opxy::control::PatternEvent short_note{16,8,60,90};
  window.count=0;
  render_programmed_phrase(tracked,source_settings,0,&short_note,1,96,24,window,lanes);
  assert(window.count==4 && window.events[0].tick==88 && window.events[3].tick==376);

  uint16_t step_cues[16]{}, resolved[16]{};
  step_cues[4]=make_cue(pin_pitch|pin_rhythm,2,100);
  step_cues[6]=make_cue(0,1,0);
  active_cues(step_cues,resolved,16);
  const CueWindow cues{resolved,384,0,24};
  assert(cues.at(95)==step_cues[6] && cues.at(96)==step_cues[4]);
  assert(cues.at(-1)==step_cues[6] && cues.at(480)==step_cues[4]);
  tracked.output=static_cast<unsigned>(OutputMode::tonal);
  const opxy::control::PatternEvent anchor_notes[]={{0,8,60,90},{96,8,64,90},{192,8,67,90}};
  source_settings.rotation=1;
  window.count=0;
  render_programmed_phrase(tracked,source_settings,0,anchor_notes,3,384,0,window,lanes,cues);
  assert(window.count==3 && window.events[0].tick==24 && window.events[1].tick==96 && window.events[1].pitch==64);
  assert(window.events[2].tick==216);

  // Same-pitch collisions use the destination pattern's own phase and wrap.
  source_timing={504,500};
  live.bar.count=0;
  append_performance_event(live.bar,live.lanes,{0,0,60,80,8},0,false);
  append_performance_event(live.bar,live.lanes,{0,0,61,80,8},0,false);
  protect_programmed_notes(live,1,context);
  assert(live.bar.count==1 && live.bar.events[0].pitch==61);
}
