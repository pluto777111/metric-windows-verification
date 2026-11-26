#pragma once
#include "phrase_evolution.hpp"
#include "punch/punch_input.hpp"

namespace metric { namespace evolution {

enum class Role { departure, arrival, resolution };
enum class Mode { transform, complement };
enum class Journey { smooth, fragmented, surprising };

struct Material {
    Phrase call;
    Phrase answer;
    unsigned response_delay;
    Role role;
};

struct Intent {
    Mode mode;
    Journey journey;
    unsigned distance;
    unsigned freedom;
    unsigned tension;
};

struct Frame {
    Material material;
    int omitted;
};

constexpr unsigned anchor_capacity = 8;

class Session {
public:
    metric_punch::Input input;
    metric_core::Settings rhythm;
    metric_punch::Scheduled scheduled;

    bool start(const Material& home) {
        if (!valid(home.call) || !valid(home.answer) || home.response_delay > 8) return false;
        for (unsigned i=0;i<steps_per_phrase;++i) fast_[i]=slow_[i]=0;
        played_=false;
        random_=1;
        frame_.omitted=-1;
        anchors_[0] = {home, 0};
        count_ = 1;
        frame_.material = home;
        scheduled.live = rhythm;
        return true;
    }

    unsigned count() const { return count_; }
    unsigned parent(unsigned slot) const { return slot < count_ ? anchors_[slot].parent : count_; }
    const Frame& frame() const { return frame_; }

    bool capture(unsigned source) {
        return add_anchor(frame_.material, source);
    }

    bool add_anchor(const Material& material, unsigned source) {
        if (source >= count_ || count_ == anchor_capacity ||
            !valid(material.call) || !valid(material.answer) || material.response_delay > 8)
            return false;
        anchors_[count_++] = {material, source};
        return true;
    }

    bool replace(unsigned slot) {
        if (slot == 0 || slot >= count_) return false;
        anchors_[slot].material = frame_.material;
        return true;
    }

    bool combine(unsigned call, unsigned answer, unsigned source) {
        if (call >= count_ || answer >= count_ || source >= count_ || count_ == anchor_capacity)
            return false;
        Material result = anchors_[answer].material;
        result.call = anchors_[call].material.call;
        return add_anchor(result, source);
    }

    void key(unsigned note, unsigned velocity, unsigned tick) {
        if (note < 128 && velocity < 128 &&
            input.observe_note_on(static_cast<uint8_t>(note), static_cast<uint8_t>(velocity)))
            metric_punch::request(scheduled, input.punch, rhythm, tick);
    }

    void reset_performance(unsigned tick) {
        input.set_armed(false);
        metric_punch::request(scheduled, input.punch, rhythm, tick);
    }

    bool advance(unsigned source, unsigned destination, const Intent& intent, unsigned tick) {
        if (source >= count_ || destination >= count_) return false;
        metric_punch::advance(scheduled, tick);
        const Material& from = anchors_[source].material;
        const Material& to = anchors_[destination].material;
        const unsigned amount = clamp(intent.distance);
        const unsigned freedom = clamp(intent.freedom);
        const unsigned tension = clamp(intent.tension);
        // Learn from the previous audible response. Index by post-delay onset.
        if (played_) {
            unsigned seen = 0;
            for (unsigned i = 0; i < note_count; ++i)
                if (static_cast<int>(i) != frame_.omitted)
                    seen |= 1u << onset(frame_.material, i);
            for (unsigned step = 0; step < steps_per_phrase; ++step) {
                const bool hit = seen & (1u << step);
                fast_[step] = (fast_[step] + (hit ? 256u : 0u)) / 2;
                slow_[step] = (slow_[step] * 7 + (hit ? 256u : 0u)) / 8;
            }
        }

        Material next = from;
        if (intent.mode == Mode::transform)
            next.call = transition(from.call, to.call, amount, intent.journey, freedom);
        next.answer = transition(from.answer, to.answer, amount, intent.journey, freedom);
        next.response_delay = interpolate(from.response_delay, to.response_delay, amount);
        next.role = amount < 50 ? from.role : to.role;
        const unsigned rotation = (scheduled.live.rotation + 16 - rhythm.rotation % 16) % 16;
        const int signed_rotation = rotation < 8 ? int(rotation) : int(rotation) - 16;
        next.response_delay = (int(next.response_delay) + signed_rotation + 9) % 9;
        frame_ = {next, -1};
        unsigned best = 0;
        for (unsigned i = 1; i + 1 < note_count; ++i) {
            const unsigned step = onset(next, i);
            const unsigned score = 2 * fast_[step] + slow_[step];
            const unsigned threshold = (100 - tension) * 6;
            if (tension && score >= 128 && score > threshold && score > best) {
                best = score;
                frame_.omitted = static_cast<int>(i);
            }
        }
        played_ = true;
        return true;
    }

    static unsigned onset(const Material& material, unsigned index) {
        return (material.answer.notes[index].step + material.response_delay) % steps_per_phrase;
    }

    struct Anchor { Material material; unsigned parent; };
    Anchor anchors_[anchor_capacity];
    unsigned count_;
    Frame frame_;
    unsigned short fast_[steps_per_phrase];
    unsigned short slow_[steps_per_phrase];
    unsigned random_;
    bool played_;

private:

    static unsigned clamp(unsigned value) { return value > 100 ? 100 : value; }

    Phrase transition(const Phrase& from, const Phrase& to, unsigned amount,
                      Journey journey, unsigned freedom) {
        if (amount == 0) return from;
        if (amount == 100) return to;
        unsigned position = amount;
        if (journey == Journey::fragmented) position = amount / 25 * 25;
        Phrase result = from;
        for (unsigned i = 0; i < note_count; ++i) {
            result.notes[i] = {
                interpolate(from.notes[i].step, to.notes[i].step, position),
                interpolate(from.notes[i].degree, to.notes[i].degree, position),
                position < 50 ? from.notes[i].accent : to.notes[i].accent
            };
        }
        if (journey == Journey::surprising && freedom) {
            random_ = random_ * 1664525u + 1013904223u;
            const unsigned index = 1 + (random_ >> 16) % (note_count - 2);
            const unsigned degree = (result.notes[index].degree + 1) % scale_degrees;
            if ((random_ >> 24) % 100 < freedom) {
                Phrase variation{};
                if (vary(result, index, result.notes[index].step, degree, variation))
                    result = variation;
            }
        }
        return result;
    }
};

}} // namespace metric::evolution
