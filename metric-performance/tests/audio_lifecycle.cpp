// Native test using a minimal mock of the Open-XY audio callback types.
#include "../engine.cpp"
#include <cassert>
#include <cstdio>
int main() {
  metric::Voice v{};
  metric::Shared sh{};
  // The SDK declares Parameters as a pointer to const Q31 values.
  // Keep the backing array writable, but pass it through the real API type.
  opxy::i32 values[8] = {0, 1073741824, 0, 0, 0, 0, 0, 0};
  opxy::Parameters p = values;
  opxy::i32 out[opxy::block_frames]{};
  metric::initialize(v,sh,0);
  bool heard=false;
  opxy::i32 peak=0;
  for (int block=0;block<512;++block) {
    metric::render(v,sh,0,out,p,{});
    for (auto sample:out) {
      if(sample) heard=true;
      const opxy::i32 magnitude=sample<0 ? -sample : sample;
      if(magnitude>peak) peak=magnitude;
      assert(magnitude<=1073741824 && "audio must stay under declared peak");
    }
  }
  assert(heard && "initialization followed by render must produce nonzero audio");
  assert(peak>150000000 && "audition must be clearly above the previous faint level");
  metric::destroy(v,sh,0);
  metric::render(v,sh,0,out,p,{});
  for (auto sample:out) assert(sample==0);
  metric::start(v,sh,0);
  metric::render(v,sh,0,out,p,{});
  bool restarted=false;
  for (auto sample:out) if(sample) restarted=true;
  assert(restarted);
  std::printf("PASS: METRIC audition sustained audio, peak %d Q31, lifecycle and output bounds\n", peak);
}
