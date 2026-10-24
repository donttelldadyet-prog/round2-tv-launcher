#pragma once
#include <cstddef>
#include <string>

namespace hevc_probe {
enum class FeedReply { Accepted, Backpressure, Rejected };
inline FeedReply classify_feed_reply(const std::string& reply) {
    if (reply.find("Ok") != std::string::npos) return FeedReply::Accepted;
    if (reply.find("BufferFull") != std::string::npos ||
        reply.find("Pending") != std::string::npos) return FeedReply::Backpressure;
    return FeedReply::Rejected;
}
inline bool send_eos_after_feed(size_t accepted, size_t total, bool failed, bool stopped) {
    return total != 0 && accepted == total && !failed && !stopped;
}
}
