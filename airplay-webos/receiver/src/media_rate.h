#pragma once
#include "settings.h"

struct MediaRateFields {
    int video_fps_value;
    int video_fps_scale;
    int max_frame_rate;
};

inline bool media_rate_fields(int max_fps, MediaRateFields& fields) {
    if(!supported_mirror_fps(max_fps))return false;
    fields.video_fps_value=max_fps;
    fields.video_fps_scale=1;
    fields.max_frame_rate=max_fps;
    return true;
}
