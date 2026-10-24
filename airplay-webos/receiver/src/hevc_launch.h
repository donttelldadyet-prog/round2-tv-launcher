#pragma once
#include <cstddef>

namespace hevc_probe {
enum class LaunchMode { Idle, Hd1080, Uhd };
enum class LaunchStatus { Valid, Missing, NotJson, Oversized, Malformed, NoMode, Unsupported, Duplicate };
struct LaunchDecision {
    LaunchMode mode = LaunchMode::Idle;
    LaunchStatus status = LaunchStatus::Missing;
    size_t argument_bytes = 0;
};
// webOS passes one JSON launch object as argv[1]. Only a top-level "mode" or
// top-level "params.mode" may authorize hardware; unknown fields are ignored.
LaunchDecision parse_launch_argument(const char *argument);
const char *launch_status_name(LaunchStatus status);
}
