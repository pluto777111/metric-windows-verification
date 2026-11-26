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
  auto header_input=input;
  char values[8][header_capacity];
  const char* names[8];
  const unsigned choices[8]={0,0,16,7,3,0,8,8};
  static const char* const phrases[7]={"1 bar","2 bars","4 bars","8 bars","16 bars","32 bars","64 bars"};
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
    static const char* const modes[]={"DRUM","TONAL","LOCK"};
    static const char* const scales[]={"MAJOR","MINOR","DORIAN","MIXOLYD","PHRYG","HARM MIN",
      "LYDIAN","LOCRIAN","MEL MIN","PHRYG DOM","MAJ PENT","MIN PENT","BLUES","WHOLE","DIM H-W","DIM W-H"};
    static const char* const roots[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    static const char* const sources[]={"GENERATE","TRACK","BOTH"};
    static const char* const interplay[]={"INDEP","SPACE","ANSWER"};
    static const char* const journeys[]={"SMOOTH","FRAGMENT","SURPRISE"};
    static const char* const qualities[]={"SETTLE","HOLD","BUILD"};
    static const char* const octaves[]={"-2","-1","0","+1","+2"};
    const unsigned modifiers=opxy::control::from_control(13);
    const bool shifted=(modifiers&1u)!=0;
    for(unsigned i=0;i<4;++i) header_input.shown[i]=i;
    if(page==2 && !shifted && (modifiers&2u)) header_input.shown[1]=5;
    const unsigned word=page==4 ? 15 : page==3 ? (shifted ? 14 : 12) : shifted ? 11 : 10;
    const unsigned bank=opxy::control::from_control(word);
    const unsigned knob[4]={bank&255,(bank>>8)&255,(bank>>16)&255,bank>>24};
    for(unsigned i=0;i<8;++i) { names[i]=""; values[i][0]=0; }
    if(page==4) {
      const unsigned steps=opxy::control::from_control(7), held=opxy::control::from_control(8);
      const unsigned span=1+(knob[0]>>2);
      static const char* const pins[]={"OFF","PITCH","RHYTHM","BOTH"};
      names[0]=steps && held ? "step" : "bar";
      names[1]="pin"; names[2]="quality"; names[3]="evolve";
      names[7]=steps ? "CV steps" : "bar scale";
      number(values[0],steps && held ? held : 1+(knob[0]&3)*span);
      copy(values[1],header_capacity,steps && held ? pins[knob[1]&3] : (knob[1]&1) ? "ON" : "OFF");
      copy(values[2],header_capacity,qualities[knob[2]%3]);
      number(values[3],knob[3]);
      number(values[7],steps ? steps : span);
      if((modifiers&8u) && !(steps && held)) header_input.shown[3]=7;
    } else if(page==3 && shifted) {
      static const char* const routes[]={"track A","track B","track C","track D"};
      for(unsigned i=0;i<4;++i) {
        names[i]=routes[i];
        number(values[i],knob[i]+1);
      }
    } else if(page==3) {
      static const char* const octave_labels[]={"oct A","oct B","oct C","oct D"};
      names[0]="output"; names[1]="root"; names[2]="scale"; names[3]="source";
      copy(values[0],header_capacity,modes[knob[0]%3]);
      copy(values[1],header_capacity,roots[knob[1]%12]);
      copy(values[2],header_capacity,scales[knob[2]%16]);
      copy(values[3],header_capacity,sources[knob[3]%3]);
      for(unsigned i=0;i<4;++i) {
        names[i+4]=octave_labels[i];
        const unsigned octave=(modifiers>>(16+i*4))&15;
        copy(values[i+4],header_capacity,octaves[octave<5 ? octave : 2]);
        if(modifiers&4u) header_input.shown[i]=i+4;
      }
    } else if(shifted) {
      names[0]="interplay"; names[1]="dest"; names[2]="journey"; names[3]="pace";
      copy(values[0],header_capacity,interplay[knob[0]%3]);
      copy(values[1],header_capacity,knob[1] ? "ANCHOR" : "HOME");
      copy(values[2],header_capacity,journeys[knob[2]%3]);
      number(values[3],knob[3]);
    } else {
      names[0]="rhythm"; names[1]="pitch"; names[2]="scatter"; names[3]="tension";
      for(unsigned i=0;i<4;++i) number(values[i],knob[i]);
      const unsigned octave=(modifiers>>8)&255;
      names[5]="octave";
      copy(values[5],header_capacity,octaves[octave<5 ? octave : 2]);
    }
  }
  page_header(state.header,header_input,frame,names,values,0);
  if(page==2 && header_input.shown[1]==5)
    for(unsigned i=0;i<4;++i) if(i!=1) header(frame,i,values[i]);
  if(page==4 && header_input.shown[3]==7)
    for(unsigned i=0;i<3;++i) header(frame,i,values[i]);
  metric_core::Settings s;
  s.morph=static_cast<uint8_t>(knobs[0]);
  s.density=static_cast<uint8_t>(knobs[1]);
  s.rotation=static_cast<uint8_t>(knobs[2]);
  static const uint8_t phrase[7]={1,2,4,8,16,32,64};
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

  text(frame,16,12*unit,55*unit,unit,
       page==4 ? "METRIC / ANCHORS" : page==3 ? "METRIC / OUTPUT" :
       audition ? "METRIC / EVOLUTION" : "METRIC   /   TWO CLOCKS");
  if(audition) {
    for(unsigned i=8;i<42;++i)hide(frame,i);
    for(unsigned i=17;i<26;++i)text(frame,i,12*unit,201*unit,unit,"");
    if(page==2) {
      metric_visual::PhraseView view{};
      for(unsigned i=0;i<7;++i)
        view.packed[i]=static_cast<u32>(opxy::control::from_control(metric_visual::preview_words[i]));
      static const char* const lanes[]={"A","B","C","D"};
      constexpr unsigned bin_ticks=metric_core::BAR_TICKS/metric_visual::preview_time_bins;
      for(unsigned lane=0;lane<metric_visual::preview_lanes;++lane) {
        const unsigned count=(view.packed[6]>>(8*lane))&255;
        const int row=85+lane*32;
        if(count)text(frame,17+lane,12*unit,row*unit,unit,lanes[lane]);
        for(unsigned i=0;i<count && i<metric_visual::preview_per_lane;++i) {
          const unsigned cell=lane*metric_visual::preview_per_lane+i;
          const unsigned dot=(view.packed[cell/4]>>(8*(cell%4)))&255;
          const unsigned onset=(dot&31)*bin_ticks;
          const int height=static_cast<int>(dot>>5);
          const bool pulse=bar_tick>=onset && bar_tick-onset<bin_ticks;
          const int scale=pulse && !input.reduced_motion ? 420+(bin_ticks-(bar_tick-onset))*18 : 420;
          place(frame,8+cell,metric_visual::xpos(onset)*unit,(row+8-height*2)*unit,
                scale,pulse ? 10u : 8u,fill_and_stroke);
        }
      }
      if(playing)place(frame,40,metric_visual::xpos(bar_tick)*unit,200*unit,unit,8,fill_and_stroke);
      else text(frame,17,12*unit,85*unit,unit,"PLAY TO SEE NOTES");
    } else if(page==4 && opxy::control::from_control(7)>0) {
      const unsigned cycle=opxy::control::from_control(4), phase=opxy::control::from_control(5);
      const unsigned markers=opxy::control::from_control(6), steps=opxy::control::from_control(7);
      const unsigned held=opxy::control::from_control(8);
      if(cycle) {
        char playing_step[12];
        number(playing_step,1+phase/(cycle/steps));
        text(frame,17,12*unit,82*unit,unit,"STEP");
        text(frame,18,68*unit,82*unit,unit,playing_step);
        for(unsigned column=0;column<32;++column)
          if(markers&(1u<<column))
            place(frame,8+column,(44+400*column/32)*unit,122*unit,600,9,fill_and_stroke);
        if(playing)place(frame,40,(44+phase*400/cycle)*unit,104*unit,unit,10,fill_and_stroke);
        if(held) {
          place(frame,41,(44+(held-1)*400/steps)*unit,138*unit,unit,10,fill_and_stroke);
          text(frame,19,12*unit,170*unit,unit,"STEP + TURN TO LOCK");
        }
        else if(!markers)text(frame,19,12*unit,170*unit,unit,"HOLD A STEP TO EDIT");
      }
    } else if(page==4) {
      const unsigned anchors=static_cast<u32>(opxy::control::from_control(15));
      const unsigned span=1+((anchors>>2)&15), length=4*span;
      const unsigned current=bar_index%length+1;
      char bar_text[15]="BAR ";
      number(bar_text+4,current);
      unsigned end=current<10 ? 5 : 6;
      bar_text[end++]='/';
      number(bar_text+end,length);
      text(frame,17,12*unit,82*unit,unit,bar_text);
      for(unsigned i=0;i<4;++i) {
        const unsigned x=44+100*i;
        const bool pinned=anchors&(1u<<(12+i));
        const bool selected=i==(anchors&3u);
        place(frame,8+i,x*unit,121*unit,selected ? unit : 700,
              pinned ? 10u : 14u,fill_and_stroke);
        char label[12];
        number(label,1+i*span);
        text(frame,19+i,(x-5)*unit,151*unit,unit,label);
      }
      if(playing) {
        const unsigned cycle=length*metric_core::BAR_TICKS;
        const unsigned x=44+(tick%cycle)*400/cycle;
        place(frame,40,x*unit,102*unit,unit,9,fill_and_stroke);
      }
    }
    return;
  }
  metric_core::Bar bar;
  metric_core::generate(s,bar_index,bar);
  const auto pattern=metric_visual::exact(bar,s);
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
  text(frame,23,12*unit,201*unit,unit,"");
  char next_b[12];
  number(next_b,metric_visual::next_in_bar(pattern.b,bar_tick));
  text(frame,24,292*unit,201*unit,unit,"B NEXT (TICKS)");
  text(frame,25,414*unit,201*unit,unit,next_b);
  // Exact next-hit timing for Lane A is available from Pattern::a and is
  // tested separately; only B gets the numeric readout due to panel width.
}
}
