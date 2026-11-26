#include "../metric_visual.hpp"
#include <cassert>
#include <cstdio>
int main(){
  metric_core::Settings s;metric_core::Bar bar;
  s.density=0; // Ratio anchor positions only; additions tested below.
  for(unsigned ratio=0;ratio<3;++ratio){
    s.ratio=static_cast<uint8_t>(ratio);
    for(unsigned b=0;b<32;++b){
      metric_core::generate(s,b,bar);
      const auto p=metric_visual::exact(bar,s);
      assert(p.a.count>=4 && p.b.count>=3);
      assert(p.a.ticks[0]==0);
      for(unsigned j=1;j<p.a.count;++j)assert(p.a.ticks[j]>=p.a.ticks[j-1]);
      for(unsigned j=1;j<p.b.count;++j)assert(p.b.ticks[j]>=p.b.ticks[j-1]);
      if(ratio==1){
        assert(p.b.count==6);
        for(unsigned j=0;j<6;++j)assert(p.b.ticks[j]==j*64);
      }
      if(ratio==2){
        assert(p.b.count==5);
        for(unsigned j=0;j<5;++j)assert(p.b.ticks[j]==(j*384+2)/5);
      }
      assert(metric_visual::next_in_bar(p.b,0)==p.b.ticks[0]);
      assert(metric_visual::next_in_bar(p.b,383)<385);
    }
  }
  // Density increases visible B events monotonically in both ratio modes.
  for(unsigned ratio=1;ratio<=2;++ratio){
    s.ratio=static_cast<uint8_t>(ratio);
    unsigned previous=0;
    for(unsigned density=0;density<=100;density+=10){
      s.density=static_cast<uint8_t>(density);
      metric_core::generate(s,0,bar);
      const auto p=metric_visual::exact(bar,s);
      const unsigned expected=(ratio==1?6:5)+(density*8+50)/100;
      assert(p.b.count==expected);
      assert(p.b.count>=previous);
      previous=p.b.count;
      for(unsigned i=0;i<p.b.count;++i)
        assert(p.b.ticks[i]<metric_core::BAR_TICKS);
    }
  }
  s.ratchet=100;metric_core::generate(s,0,bar);
  const auto r=metric_visual::exact(bar,s);
  bool offgrid=false;
  for(unsigned i=0;i<r.a.count;++i)if(r.a.ticks[i]%24)offgrid=true;
  assert(offgrid);
  assert(metric_visual::xpos(0)==44);
  assert(metric_visual::xpos(384)==444);
  puts("PASS: v0.7.1 exact ratio anchors, density ornaments, ratchets and phase timing");
}
