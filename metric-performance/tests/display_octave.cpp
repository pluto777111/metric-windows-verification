#include "../display.cpp"
#include <cassert>
#include <cstring>

static opxy::i32 display_words[16];

extern "C" opxy::i32 sdk_control_display(opxy::u32 index) {
  return display_words[index];
}

int main() {
  metric::DisplayState state{};
  opxy::draw::Input input{};
  input.reduced_motion=1;
  input.shown[1]=5; // Stock can retain its second-row selection on entry.
  metric::Frame frame{};
  display_words[9]=2;
  display_words[10]=12 | 24<<8 | 36<<16 | 48<<24;
  display_words[11]=2 | 1<<8 | 1<<16 | 4<<24;
  display_words[13]=3<<8; // Output octave +1.

  metric::display(state,input,frame);
  assert(state.header.page==0);
  assert(std::strcmp(frame.texts[1].chars,"pitch")==0);
  assert(std::strcmp(frame.texts[9].chars,"octave")==0);
  assert(frame.texts[9].y>frame.texts[1].y);
  assert(std::strcmp(frame.header[1],"24")==0);

  display_words[13]|=2; // Pitch encoder click selects the lower row.
  metric::display(state,input,frame);
  assert(state.header.page==1);
  assert(std::strcmp(frame.texts[1].chars,"octave")==0);
  assert(std::strcmp(frame.texts[9].chars,"pitch")==0);
  assert(frame.texts[1].y>frame.texts[9].y);
  assert(std::strcmp(frame.header[1],"+1")==0);
  assert(std::strcmp(frame.header[0],"12")==0);

  display_words[13]|=1; // Shift shows the temporary controls on the top row.
  metric::display(state,input,frame);
  assert(state.header.page==0);
  assert(std::strcmp(frame.texts[0].chars,"interplay")==0);
  assert(std::strcmp(frame.header[0],"ANSWER")==0);

  display_words[13]&=~1;
  metric::display(state,input,frame);
  assert(state.header.page==1);
  assert(std::strcmp(frame.header[1],"+1")==0);

  display_words[9]=3;
  display_words[13]=1 | (2 | 3<<4 | 1<<8 | 4<<12)<<16;
  display_words[14]=4 | 5<<8 | 6<<16 | 7<<24;
  metric::display(state,input,frame);
  assert(state.header.page==0);
  assert(std::strcmp(frame.texts[0].chars,"track a")==0);
  assert(std::strcmp(frame.header[2],"7")==0);
  display_words[13]=(display_words[13]&~1)|4;
  metric::display(state,input,frame);
  assert(state.header.page==1);
  assert(std::strcmp(frame.texts[3].chars,"oct d")==0);
  assert(std::strcmp(frame.header[1],"+1")==0);
  assert(std::strcmp(frame.header[2],"-1")==0);
  display_words[9]=4;
  display_words[13]|=8;
  display_words[15]=2<<2 | 1<<8 | 1<<16 | 80<<24;
  metric::display(state,input,frame);
  assert(state.header.page==1);
  assert(std::strcmp(frame.texts[3].chars,"bar scale")==0);
  assert(std::strcmp(frame.header[3],"3")==0);
  assert(std::strcmp(frame.header[0],"1")==0);
  display_words[3]=1;
  display_words[0]=4*metric_core::BAR_TICKS;
  display_words[15]|=9<<12;
  metric::display(state,input,frame);
  assert(std::strcmp(frame.texts[17].chars,"BAR 5/12")==0);
  assert(std::strcmp(frame.texts[22].chars,"10")==0);
  assert(frame.nodes[8].shade==10 && frame.nodes[9].shade==14);

  display_words[9]=2;
  display_words[0]=36;
  display_words[1]=(3 | 7<<5)<<8;
  display_words[8]=2;
  metric::display(state,input,frame);
  assert(frame.nodes[8].x<frame.nodes[9].x && frame.nodes[8].y>frame.nodes[9].y);
  assert(frame.nodes[9].shade==10);

  display_words[9]=1;
  display_words[3]=1;
  display_words[5]=565;
  display_words[6]=888;
  metric::display(state,input,frame);
  assert(frame.texts[23].chars[0]=='\0');
  assert(std::strcmp(frame.texts[19].chars,"ROUTE A/B")==0);
}
