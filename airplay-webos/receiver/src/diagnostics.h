#pragma once
#include <algorithm>
#include <cstdint>

struct VideoTimingSample {
    uint64_t frames = 0;
    uint64_t intervals = 0;
    uint64_t interval_total_ns = 0;
    uint64_t interval_min_ns = 0;
    uint64_t interval_max_ns = 0;
    uint64_t pts_gaps_over_100ms = 0;
    uint64_t pts_discontinuities = 0;
    uint64_t arrival_intervals = 0;
    uint64_t arrival_interval_total_ns = 0;
    uint64_t arrival_interval_max_ns = 0;
    uint64_t arrival_gaps_over_100ms = 0;
    uint64_t gaps_in_both = 0;
    uint64_t arrival_only_gaps = 0;
    uint64_t pts_only_gaps = 0;
};

struct DurationSample {
    uint64_t count = 0;
    uint64_t total_ns = 0;
    uint64_t max_ns = 0;
    uint64_t over_50ms = 0;
    double average_ms() const { return count ? total_ns / double(count) / 1e6 : 0; }
    double maximum_ms() const { return max_ns / 1e6; }
};

class DurationWindow {
public:
    void record(uint64_t ns) {
        ++window_.count;
        window_.total_ns += ns;
        window_.max_ns = std::max(window_.max_ns, ns);
        if (ns > 50000000ULL) ++window_.over_50ms;
    }
    DurationSample take_window() {
        DurationSample sample = window_;
        window_ = DurationSample();
        return sample;
    }
    void reset() { window_ = DurationSample(); }
private:
    DurationSample window_;
};

// Tracks incoming access-unit timestamps separately from throttled decoder events.
class VideoTiming {
public:
    void record(uint64_t pts_ns, uint64_t arrival_ns = 0) {
        ++window_.frames;
        bool pts_gap = false;
        if (have_previous_ && pts_ns > previous_pts_) {
            const uint64_t interval = pts_ns - previous_pts_;
            // Clock jumps are handled by the session loop, not frame cadence.
            if (interval <= 2000000000ULL) {
                if (!window_.intervals) window_.interval_min_ns = interval;
                window_.interval_min_ns = std::min(window_.interval_min_ns, interval);
                window_.interval_max_ns = std::max(window_.interval_max_ns, interval);
                window_.interval_total_ns += interval;
                ++window_.intervals;
                if (interval > 100000000ULL) { ++window_.pts_gaps_over_100ms; pts_gap = true; }
            } else ++window_.pts_discontinuities;
        } else if (have_previous_) ++window_.pts_discontinuities;
        bool arrival_gap = false;
        if (arrival_ns && previous_arrival_ns_ && arrival_ns > previous_arrival_ns_) {
            const uint64_t interval = arrival_ns - previous_arrival_ns_;
            ++window_.arrival_intervals;
            window_.arrival_interval_total_ns += interval;
            window_.arrival_interval_max_ns = std::max(window_.arrival_interval_max_ns, interval);
            if (interval > 100000000ULL) { ++window_.arrival_gaps_over_100ms; arrival_gap = true; }
        }
        if (pts_gap && arrival_gap) ++window_.gaps_in_both;
        else if (pts_gap) ++window_.pts_only_gaps;
        else if (arrival_gap) ++window_.arrival_only_gaps;
        previous_pts_ = pts_ns;
        previous_arrival_ns_ = arrival_ns;
        have_previous_ = true;
    }
    VideoTimingSample take_window() {
        VideoTimingSample sample = window_;
        window_ = VideoTimingSample();
        return sample;
    }
    void reset() {
        window_ = VideoTimingSample();
        previous_pts_ = 0;
        previous_arrival_ns_ = 0;
        have_previous_ = false;
    }
private:
    VideoTimingSample window_;
    uint64_t previous_pts_ = 0;
    uint64_t previous_arrival_ns_ = 0;
    bool have_previous_ = false;
};
