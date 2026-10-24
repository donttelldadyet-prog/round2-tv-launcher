#pragma once
#include <cstddef>
#include <cstdint>

// Load() acceptance permits a bounded input preroll. It does not imply that
// Starfish has emitted LOADCOMPLETED or that decoded video is being displayed.
enum class HevcFeedStage { NoLoad, Failed, Preroll, WaitingForLoad, Loaded };
class HevcStartupGate {
public:
    static const unsigned kMaxPrerollFrames = 60;
    static const size_t kMaxPrerollBytes = 8u * 1024u * 1024u;
    static const int64_t kStartupTimeoutNs = 8000000000LL;

    void reset() {
        load_accepted_=false; load_started_ns_=0;
        preroll_frames_=accepted_frames_=0; preroll_bytes_=0;
    }
    void accept_load(int64_t now_ns) { reset(); load_accepted_=true; load_started_ns_=now_ns; }
    int64_t age_ns(int64_t now_ns) const {
        return load_accepted_ && now_ns>load_started_ns_ ? now_ns-load_started_ns_ : 0;
    }
    bool startup_timed_out(int64_t now_ns, bool load_completed, bool playing, bool paused) const {
        return load_accepted_ && !paused && age_ns(now_ns)>kStartupTimeoutNs &&
               (!load_completed || !playing);
    }
    HevcFeedStage video_stage(bool load_completed, bool failed, size_t bytes) const {
        if(failed) return HevcFeedStage::Failed;
        if(!load_accepted_) return HevcFeedStage::NoLoad;
        if(load_completed) return HevcFeedStage::Loaded;
        if(!bytes || preroll_bytes_>=kMaxPrerollBytes ||
           bytes>kMaxPrerollBytes-preroll_bytes_ || preroll_frames_>=kMaxPrerollFrames)
            return HevcFeedStage::WaitingForLoad;
        return HevcFeedStage::Preroll;
    }
    void accepted_video(size_t bytes, HevcFeedStage stage) {
        ++accepted_frames_;
        if(stage==HevcFeedStage::Preroll) { ++preroll_frames_; preroll_bytes_+=bytes; }
    }
    unsigned accepted_frames() const { return accepted_frames_; }
    unsigned preroll_frames() const { return preroll_frames_; }
    size_t preroll_bytes() const { return preroll_bytes_; }
private:
    bool load_accepted_=false;
    int64_t load_started_ns_=0;
    unsigned preroll_frames_=0, accepted_frames_=0;
    size_t preroll_bytes_=0;
};
