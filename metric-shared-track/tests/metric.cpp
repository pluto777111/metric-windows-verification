#include "../metric_engine.hpp"
#include <cassert>
#include <cstdio>
using namespace metric_core;
int main() {
  for(unsigned n=1;n<=32;n++) for(unsigned h=0;h<=n;h++) {
    unsigned count=0;for(unsigned i=0;i<n;i++) count+=euclid(i,h,n);
    assert(count==h);
  }
  Settings s; Bar a,b;
  for(unsigned ratio=0;ratio<3;ratio++) for(unsigned morph=0;morph<=100;morph+=25) {
    s.ratio=ratio;s.morph=morph;
    for(unsigned bar=0;bar<32;bar++) {
      generate(s,bar,a);generate(s,bar,b);
      assert(a.count==b.count&&a.count<=MAX_EVENTS);
      for(unsigned i=0;i<a.count;i++) {
        assert(a.events[i].tick<BAR_TICKS);
        assert(a.events[i].target<8);
        assert(a.events[i].tick==b.events[i].tick);
        if(i)assert(a.events[i-1].tick<=a.events[i].tick);
      }
      generate(s,bar%4,b);
      assert(a.count==b.count);
      for(unsigned i=0;i<a.count;i++)assert(a.events[i].tick==b.events[i].tick);
    }
  }
  puts("PASS: METRIC v0.3 standalone rhythm core, ratios, ordering and four-bar repeat");
}
