#pragma once

namespace metric {
namespace evolution {

// A deliberately small monophonic phrase model. Notes correspond by order.
constexpr unsigned steps_per_phrase = 16;
constexpr unsigned scale_degrees = 7;
constexpr unsigned note_count = 6;
constexpr unsigned morph_units = 100;

struct Note {
    unsigned step;
    unsigned degree;
    bool accent;
};

struct Phrase {
    Note notes[note_count];
};

struct Relation {
    unsigned step_motion;
    unsigned degree_motion;
    bool same_intervals;
    bool same_contour;
    bool same_accents;
};

inline bool valid(const Phrase& phrase) {
    for (unsigned i = 0; i < note_count; ++i) {
        const Note& note = phrase.notes[i];
        if (note.step >= steps_per_phrase || note.degree >= scale_degrees ||
            (i && note.step <= phrase.notes[i - 1].step)) return false;
    }
    return true;
}

inline int interval(const Phrase& phrase, unsigned index) {
    return static_cast<int>(phrase.notes[index].degree) -
           static_cast<int>(phrase.notes[index - 1].degree);
}

inline int direction(int value) {
    return (value > 0) - (value < 0);
}

inline unsigned distance(unsigned a, unsigned b) {
    return a < b ? b - a : a - b;
}

inline Relation compare(const Phrase& from, const Phrase& to) {
    Relation relation{0, 0, true, true, true};
    for (unsigned i = 0; i < note_count; ++i) {
        const Note& a = from.notes[i];
        const Note& b = to.notes[i];
        relation.step_motion += distance(a.step, b.step);
        relation.degree_motion += distance(a.degree, b.degree);
        relation.same_accents &= a.accent == b.accent;
        if (i) {
            const int left = interval(from, i);
            const int right = interval(to, i);
            relation.same_intervals &= left == right;
            relation.same_contour &= direction(left) == direction(right);
        }
    }
    return relation;
}

inline bool transpose(const Phrase& from, int degrees, Phrase& out) {
    if (!valid(from) || degrees <= -static_cast<int>(scale_degrees) ||
        degrees >= static_cast<int>(scale_degrees)) return false;
    Phrase next = from;
    for (Note& note : next.notes) {
        const int degree = static_cast<int>(note.degree) + degrees;
        if (degree < 0 || degree >= static_cast<int>(scale_degrees)) return false;
        note.degree = static_cast<unsigned>(degree);
    }
    out = next;
    return true;
}

// Change one event while keeping the rise, fall, or repeat at each interval.
inline bool vary(const Phrase& from, unsigned index, unsigned step, unsigned degree, Phrase& out) {
    if (!valid(from) || index >= note_count) return false;
    Phrase next = from;
    next.notes[index].step = step;
    next.notes[index].degree = degree;
    if (!valid(next) || !compare(from, next).same_contour) return false;
    out = next;
    return true;
}

inline unsigned interpolate(unsigned from, unsigned to, unsigned amount) {
    return (from * (morph_units - amount) + to * amount + morph_units / 2) / morph_units;
}

// Anchors define endpoints. Try a discrete path with strict optional contour
// preservation. Both endpoints must share accent placement by note identity.
inline bool morph(const Phrase& from, const Phrase& to, unsigned amount,
                  bool keep_contour, Phrase& out) {
    if (amount > morph_units || !valid(from) || !valid(to)) return false;
    const Relation endpoints = compare(from, to);
    if (!endpoints.same_accents || (keep_contour && !endpoints.same_contour)) return false;
    Phrase next = from;
    for (unsigned i = 0; i < note_count; ++i) {
        next.notes[i].step = interpolate(from.notes[i].step, to.notes[i].step, amount);
        next.notes[i].degree = interpolate(from.notes[i].degree, to.notes[i].degree, amount);
    }
    if (!valid(next) || (keep_contour && !compare(from, next).same_contour)) return false;
    out = next;
    return true;
}

// The host calls advance_phrase at a phrase boundary. Edits replace the next
// phrase as a whole, so a change cannot split a phrase already in playback.
class Sequence {
public:
    bool start(const Phrase& phrase) {
        if (!valid(phrase)) return false;
        home_ = current_ = phrase;
        started_ = true;
        pending_ = false;
        return true;
    }

    bool queue(const Phrase& phrase) {
        if (!started_ || !valid(phrase)) return false;
        next_ = phrase;
        pending_ = true;
        return true;
    }

    bool queue_return() { return queue(home_); }

    bool advance_phrase() {
        if (!pending_) return false;
        current_ = next_;
        pending_ = false;
        return true;
    }

    const Phrase* current() const { return started_ ? &current_ : nullptr; }

private:
    Phrase home_{};
    Phrase current_{};
    Phrase next_{};
    bool started_ = false;
    bool pending_ = false;
};

} // namespace evolution
} // namespace metric
