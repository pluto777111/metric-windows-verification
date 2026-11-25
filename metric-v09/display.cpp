#include <opxy/track_control.hpp>
#include "display.h"
#include "output_pitch.hpp"
#include "metric_visual.hpp"
namespace metric {
using namespace opxy::draw;
using opxy::u32;
static const char* const titles[8]={"morph","density","rotate","phrase","ratio","ratchet","track A","track B"};
inline void number(char* out,unsigned value) {
  char r[12]; unsigned n=0,i=0;
  do{r[n++]=static_cast<char>('0'+value%10);value/=10;}while(value&&n<11);
  while (n) {
    out[i++] = r[--n];
  }
  out[i] = 0;
}
inline unsigned decode(opxy::i32 raw,unsigned choices){
  const unsigned q=raw<0?0u:static_cast<unsigned>(raw)>>16;
  if(choices){unsigned v=q*choices>>15;return v<choices?v:choices-1;}
  return (q*100u+16384u)>>15;
}
inline void display_reset(DisplayState& state){state.header.reset();}
inline void display(DisplayState& state,const opxy::draw::Input& input,Frame& frame){
  const auto page=opxy::control::from_control(9);
  const bool audition=page>=2;
  char values[8][header_capacity];
  const char* names[8];
  const unsigned choices[8]={0,0,16,6,3,0,8,8};
  static const char* const phrases[6]={"1 bar","2 bars","4 bars","8 bars","16 bars","32 bars"};
  static const char* const ratios[3]={"1:1","3:2","5:4"};
  unsigned knobs[8];
  for(unsigned i=0;i<8;++i){
    names[i]=titles[i];
    const unsigned v=decode(input.slots[i],choices[i]);
    knobs[i]=v;
    if(i==3)copy(values[i],header_capacity,phrases[v]);
    else if(i==4)copy(values[i],header_capacity,ratios[v]);
    else number(values[i],v+(i>=6?1:0));
  }
  if(audition) {
    static const char* const modes[3]={"DRUM","TONAL","LOCK"};
    static const char* const scales[6]={"MAJOR","MINOR","DORIAN","MIXOLYD","PHRYG","HARM MIN"};
    static const char* const roots[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    names[0]="distance"; names[1]="tension"; names[2]="space";
    names[3]="scale"; names[4]="output"; names[5]="root";
    copy(values[3],header_capacity,knobs[4]==2 ? scales[knobs[3]] : "-");
    copy(values[4],header_capacity,modes[knobs[4]]);
    copy(values[5],header_capacity,knobs[4]>0 ? roots[root_from_percent(knobs[5])] : "-");
  }
  page_header(state.header,input,frame,names,values,0);
  metric_core::Settings s;
  s.morph=static_cast<uint8_t>(knobs[0]);
  s.density=static_cast<uint8_t>(knobs[1]);
  s.rotation=static_cast<uint8_t>(knobs[2]);
  static const uint8_t phrase[6]={1,2,4,8,16,32};
  s.phrase_bars=phrase[knobs[3]];
  s.ratio=static_cast<uint8_t>(knobs[4]);
  s.ratchet=static_cast<uint8_t>(knobs[5]);
  s.target_a=static_cast<uint8_t>(knobs[6]);
  s.target_b=static_cast<uint8_t>(knobs[7]);
  s.pitch_a=36; s.pitch_b=42;

  // The track-control callback publishes the current transport position;
  // when it is not running the display still shows the initial bar.
  const u32 tick=static_cast<u32>(opxy::control::from_control(0));
  const bool playing=opxy::control::from_control(3)!=0;
  const u32 bar_index=playing?tick/metric_core::BAR_TICKS:0u;
  const unsigned bar_tick=playing?tick%metric_core::BAR_TICKS:0u;
  metric_core::Bar bar;
  metric_core::generate(s,bar_index,bar);
  const auto pattern=metric_visual::exact(bar,s);

  text(frame,16,12*unit,55*unit,unit,
       page==3 ? "METRIC / INTERPLAY AUDITION" :
       audition ? "METRIC / PHRASE AUDITION" : "METRIC   /   TWO CLOCKS");
  if(audition) {
    text(frame,17,12*unit,79*unit,unit,"MORPH: DISTANCE");
    text(frame,18,12*unit,137*unit,unit,"DENSITY: TENSION");
    for(unsigned i=8;i<42;++i)hide(frame,i);
    for(unsigned i=19;i<26;++i)text(frame,i,12*unit,201*unit,unit,"");
    if(page==3)text(frame,19,12*unit,201*unit,unit,"ROTATION: SPACE");
    return;
  }
  text(frame,17,12*unit,79*unit,unit,"A    KICK / REFERENCE");
  text(frame,18,12*unit,137*unit,unit,"B    RATIO / PULSE");
  for(unsigned i=0;i<16;++i){
    if(i<pattern.a.count){
      const unsigned x=metric_visual::xpos(pattern.a.ticks[i]);
      const bool current=playing && pattern.a.ticks[i]<=bar_tick &&
          bar_tick-pattern.a.ticks[i]<6;
      place(frame,8+i,static_cast<opxy::i32>(x*unit),104*unit,unit,
            current?10u:9u,fill_and_stroke);
    }else hide(frame,8+i);
    if(i<pattern.b.count){
      const unsigned x=metric_visual::xpos(pattern.b.ticks[i]);
      const bool current=playing && pattern.b.ticks[i]<=bar_tick &&
          bar_tick-pattern.b.ticks[i]<6;
      place(frame,24+i,static_cast<opxy::i32>(x*unit),161*unit,unit,
            current?10u:8u,fill_and_stroke);
    }else hide(frame,24+i);
  }
  // Two dedicated pointers, positioned against the exact shared transport.
  // They share time, but indicate the phase against each lane's pulses.
  if(playing){
    const opxy::i32 x=static_cast<opxy::i32>(metric_visual::xpos(bar_tick)*unit);
    place(frame,40,x,86*unit,unit,10,fill_and_stroke);
    place(frame,41,x,180*unit,unit,10,fill_and_stroke);
  }else{hide(frame,40);hide(frame,41);}
  char a_label[16],b_label[16];
  number(a_label,knobs[6]+1);number(b_label,knobs[7]+1);
  text(frame,19,12*unit,201*unit,unit,"ROUTE A/B");
  text(frame,20,113*unit,201*unit,unit,a_label);
  text(frame,21,137*unit,201*unit,unit,b_label);
  text(frame,22,191*unit,201*unit,unit,ratios[knobs[4]]);
  // Counts show notes submitted to the host router, not necessarily audible
  // notes. This helps distinguish control dispatch from instrument routing.
  char status[48];unsigned k=0;
  const char* prefix=playing?"PLAY A":"STOP A";
  for(unsigned i=0;prefix[i] && k<sizeof(status)-1;++i)status[k++]=prefix[i];
  char nbuf[12];number(nbuf,static_cast<u32>(opxy::control::from_control(5)));
  for(unsigned i=0;nbuf[i] && k<sizeof(status)-1;++i)status[k++]=nbuf[i];
  const char* middle=" B";
  for(unsigned i=0;middle[i] && k<sizeof(status)-1;++i)status[k++]=middle[i];
  number(nbuf,static_cast<u32>(opxy::control::from_control(6)));
  for(unsigned i=0;nbuf[i] && k<sizeof(status)-1;++i)status[k++]=nbuf[i];
  status[k]=0;
  text(frame,23,12*unit,225*unit,unit,status);
  char next_b[12];
  number(next_b,metric_visual::next_in_bar(pattern.b,bar_tick));
  text(frame,24,292*unit,201*unit,unit,"B NEXT (TICKS)");
  text(frame,25,414*unit,201*unit,unit,next_b);
  // Exact next-hit timing for Lane A is available from Pattern::a and is
  // tested separately; only B gets the numeric readout due to panel width.
}
}
