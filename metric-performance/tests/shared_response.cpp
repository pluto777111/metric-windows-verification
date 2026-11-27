#include "../control.cpp"
#include <cassert>

using namespace metric;

static unsigned reads[8];
static bool read(opxy::u32 track, opxy::control::PatternEvent* notes, opxy::u32 capacity,
                 opxy::u32& count, opxy::control::PatternTiming& timing) {
    assert(capacity>=2);
    ++reads[track];
    if(track==0) {
        timing={192,0};
        notes[0]={0,24,60,90};
        notes[1]={48,12,64,80};
    } else if(track==1) {
        timing={384,0};
        notes[0]={0,24,67,100};
        notes[1]={120,24,69,90};
    } else return false;
    count=2;
    return true;
}

int main() {
    ControlState st{}, recalled{};
    control_reset(st);
    st.output_ab[0]=4;
    st.output_ab[1]=5;
    st.performance.source_mode=1;
    st.performance.output=1;
    opxy::control::Input input{};
    input.track=12;
    input.transport.playing=true;
    input.values[12][7]=0x18000000;
    input.read_pattern=read;
    opxy::control::Overlay overlay{};
    control(st,input,overlay);
    assert(reads[0]==1 && reads[1]==1);
    assert(st.bar.count==5 && ((overlay.display[14]>>8)&255)==6);
    const unsigned tick[]={0,48,120,192,240};
    const unsigned pitch[]={67,64,69,60,64};
    for(unsigned i=0;i<5;++i) {
        const auto& note=st.bar.events[i];
        assert(note.tick==tick[i] && note.pitch==pitch[i] && note.target==5 && st.lanes[i]==1);
    }

    unsigned char data[project_header]{};
    assert(control_save(st,data,sizeof data)==sizeof data);
    control_load(recalled,data,sizeof data);
    assert(recalled.output_ab[0]==4 && recalled.output_ab[1]==5 && recalled.input_b_enabled);

    // Matching A and B taps that instrument once. The output assignments
    // stay put when the old native Track B slot is edited.
    st.next_tick=384; // Compare one new bar, without replaying an earlier callback.

    input.values[12][7]=0;
    input.transport.ticks96=384;
    control(st,input,overlay);
    assert(reads[0]==2 && reads[1]==1);
    assert(st.bar.count==4 && st.settings.target_b==5 && st.bar.events[0].pitch==60);

    // Earlier projects start with their previous destination and source A.
    data[0]=static_cast<unsigned char>(kEngineVersion);
    control_load(recalled,data,10);
    recalled.performance.source_mode=1;
    recalled.performance.output=1;
    input.values[12][7]=0x18000000;
    input.transport.ticks96=0;
    control(recalled,input,overlay);
    assert(!recalled.input_b_enabled && reads[1]==2); // Output collision read only.
    assert(recalled.output_ab[1]==1);
    input.values[12][7]=0x28000000;
    control(recalled,input,overlay);
    assert(recalled.input_b_enabled && recalled.output_ab[1]==1);
}
