#pragma once
#include <opxy/audio.hpp>
#include "metric_engine.hpp"
namespace metric {
// This is an AUDITION-only build. It sounds rhythms on note audition;
// the sequencer track-control code remains otherwise unchanged.
struct Voice {
  metric_core::Bar bar;
  opxy::u32 frame;
  opxy::u32 absolute_bar;
  opxy::u32 next_event;
  opxy::u32 phase;
  opxy::u32 noise;
  opxy::i32 kick_env;
  opxy::i32 hat_env;
  opxy::u32 kick_increment;
  opxy::u32 active;
};
struct Shared {};
}
