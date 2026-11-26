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
}
