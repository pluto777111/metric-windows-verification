// Generate FaustOtt.hpp from faust_reference.dsp using the pinned Faust libraries.
#include <faust/dsp/dsp.h>
#include <faust/gui/MapUI.h>
#include <faust/gui/meta.h>
#include "FaustOtt.hpp"
#include "ott.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr unsigned rate = 44100;
constexpr double q31 = 2147483648.0;
constexpr double peak_ceiling = 0.891250938;

opxy::i32 encode(double position) {
    return static_cast<opxy::i32>(position * (q31 - 1));
}

bool compare(double hz, double amplitude, double depth, double time_position,
             double low, double high, double up, double down, bool bursts) {
    FaustOtt reference;
    reference.init(rate);
    MapUI map;
    reference.buildUserInterface(&map);
    map.setParamValue("Depth", depth);
    map.setParamValue("Time", time_position <= .5 ? time_position * 2 : 1 + 18 * (time_position - .5));
    map.setParamValue("Low", low);
    map.setParamValue("High", high);
    map.setParamValue("Upward", up);
    map.setParamValue("Downward", down);
    ott::Controls controls;
    controls.character = ott::Drum;
    controls.level_match = 0;
    controls.depth = encode(depth);
    controls.time = encode(time_position);
    controls.low_trim = encode((low + 12) / 24);
    controls.high_trim = encode((high + 12) / 24);
    controls.upward = encode(up / 2);
    controls.downward = encode(down / 2);
    ott::Processor fixed;
    std::array<opxy::i32, 2 * opxy::block_frames> track{};
    FAUSTFLOAT in[2][opxy::block_frames], out[2][opxy::block_frames];
    FAUSTFLOAT* in_ptrs[] = {in[0], in[1]};
    FAUSTFLOAT* out_ptrs[] = {out[0], out[1]};
    double power = 0, error = 0, fixed_power = 0, limiter = 1;
    for (unsigned block = 0; block < 800; ++block) {
        for (unsigned frame = 0; frame < opxy::block_frames; ++frame) {
            const double t = (block * opxy::block_frames + frame) / static_cast<double>(rate);
            const double active = !bursts || block % 100 < 30 ? 1 : .035;
            const double sample = amplitude * active * std::sin(2 * pi * hz * t);
            in[0][frame] = sample;
            in[1][frame] = .51 * sample + (bursts ? .008 * std::sin(2 * pi * 7049 * t) : 0);
            for (unsigned ch = 0; ch < 2; ++ch)
                track[2 * frame + ch] = static_cast<opxy::i32>(q31 * in[ch][frame]);
        }
        reference.compute(opxy::block_frames, in_ptrs, out_ptrs);
        fixed.process(track.data(), controls);
        double peak = 0;
        for (const auto& channel : out)
            for (auto x : channel) peak = std::max(peak, std::abs(static_cast<double>(x)));
        const double limit = peak > peak_ceiling ? peak_ceiling / peak : 1;
        limiter = limiter < limit ? limiter + (limit - limiter) / 4 : limit;
        if (block < (bursts ? 100u : 400u)) continue;
        for (unsigned frame = 0; frame < opxy::block_frames; ++frame) {
            for (unsigned ch = 0; ch < 2; ++ch) {
                const double expected = limiter * out[ch][frame];
                const double actual = track[2 * frame + ch] / q31;
                if (!std::isfinite(actual) || std::abs(actual) > peak_ceiling) return false;
                power += expected * expected;
                fixed_power += actual * actual;
                error += (expected - actual) * (expected - actual);
            }
        }
    }
    const double db = 10 * std::log10(fixed_power / power);
    const double relative = 100 * std::sqrt(error / power);
    std::cout << hz << " Hz, A=" << amplitude << ", depth=" << depth
              << (bursts ? ", bursts" : "") << ": " << db << " dB, " << relative << "% RMS error\n";
    return std::abs(db) < .20 && relative < (bursts ? 5 : 3);
}
}

int main() {
    bool pass = true;
    for (double hz : {40., 88.3, 530., 2500., 9200.}) {
        for (double amplitude : {.0001, .006, .20})
            pass = compare(hz, amplitude, 1, .5, 0, 0, 1, 1, false) && pass;
        pass = compare(hz, .01, .25, .35, -10, 7, .7, 1.3, false) && pass;
        pass = compare(hz, .15, 1, .75, 5, -7, 1.7, .6, true) && pass;
    }
    return pass ? 0 : 1;
}
