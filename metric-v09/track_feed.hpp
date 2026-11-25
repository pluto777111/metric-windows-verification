#pragma once

namespace metric {
using Tick = unsigned;

struct NoteOn {
    unsigned track;
    unsigned pitch;
    unsigned velocity;
    Tick length;
};

struct PhraseEvent {
    Tick offset;
    NoteOn note;
};

struct PlayingPattern {
    unsigned project;
    unsigned pattern;
    unsigned track;
    Tick length;
};

// Attach at stock sequencer dispatch when that event is available. Keep using
// the original stock dispatch independently of whether an onset fits here.
class TrackFeed {
public:
    static constexpr unsigned capacity = 128;
    struct Phrase {
        PhraseEvent notes[capacity]{};
        unsigned count = 0;
        bool ready = false;
        bool truncated = false;
    };

    bool complete() const { return previous_.ready && !previous_.truncated; }
    const Phrase& previous() const { return previous_; }

    void stop() {
        tracking_ = false;
        whole_loop_ = false;
        pending_.count = 0;
        pending_.truncated = false;
        previous_.ready = false;
    }

    // position must be relative to the same stock pattern throughout a loop.
    // The host supplies the loop identity independently of note density.
    void clock(const PlayingPattern& pattern, Tick loop, Tick position) {
        if (!pattern.length || position >= pattern.length) {
            stop();
            return;
        }
        const bool same = tracking_ && pattern.project == pattern_.project &&
            pattern.pattern == pattern_.pattern && pattern.track == pattern_.track &&
            pattern.length == pattern_.length;
        if (!same || loop != loop_ || position < position_) {
            const bool next_loop = same && loop == loop_ + 1;
            if (next_loop && whole_loop_) {
                previous_ = pending_;
                previous_.ready = true;
            } else {
                previous_.ready = false;
            }
            pending_.count = 0;
            pending_.truncated = false;
            whole_loop_ = next_loop || position == 0;
        }
        tracking_ = true;
        pattern_ = pattern;
        loop_ = loop;
        position_ = position;
    }

    void observe_sequenced_onset(const NoteOn& note) {
        if (!tracking_ || note.track != pattern_.track || note.pitch >= 128 ||
            !note.velocity || note.velocity >= 128 || !note.length) return;
        if (pending_.count == capacity) {
            pending_.truncated = true;
            return;
        }
        pending_.notes[pending_.count++] = {position_, note};
    }

private:
    PlayingPattern pattern_{};
    Tick loop_ = 0, position_ = 0;
    bool tracking_ = false, whole_loop_ = false;
    Phrase pending_{}, previous_{};
};

} // namespace metric
