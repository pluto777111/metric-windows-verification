#pragma once
#include "punch_engine.hpp"
// Non-owning keyboard observer. The host decides whether a note is played,
// routed to its Punch-In FX performance context, or used elsewhere.
// METRIC PUNCH never consumes an instrument note or intercepts a system key.
namespace metric_punch {
struct Input {
  Punch punch;
  uint8_t first_note;  // Configurable mapping, not verified OP-XY key numbers.
  bool armed;
  bool observed_down[KEY_COUNT];

  // Host should call when entering/leaving a METRIC punch performance context.
  // Exiting resets all live gestures; any subsequent note-off is harmless.
  bool set_armed(bool on) {
    if(armed==on)return false;
    armed=on;
    if(!on)panic();
    return true;
  }
  // Returns whether PUNCH state changed, NOT whether the event was consumed.
  bool observe_note_on(uint8_t note,uint8_t velocity){
    if(velocity==0)return observe_note_off(note);
    if(!armed || note<first_note || uint16_t(note)-first_note>=KEY_COUNT)return false;
    uint8_t index=uint8_t(note-first_note);
    if(observed_down[index])return false; // prevent repeated note-on from retoggling latch
    observed_down[index]=true;
    press(punch,index);
    return true;
  }
  bool observe_note_off(uint8_t note){
    if(note<first_note || uint16_t(note)-first_note>=KEY_COUNT)return false;
    uint8_t index=uint8_t(note-first_note);
    if(!observed_down[index])return false;
    observed_down[index]=false;
    release(punch,index);
    return true;
  }
  void panic(){
    clear(punch);
    for(uint8_t i=0;i<KEY_COUNT;++i)observed_down[i]=false;
  }
};
} // namespace metric_punch
