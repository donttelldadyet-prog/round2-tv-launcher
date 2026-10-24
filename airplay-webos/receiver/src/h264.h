#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
struct H264Info {
    bool valid=false, keyframe=false, picture=false;
    int width=0,height=0;
    std::vector<uint8_t> configuration;
};
H264Info inspect_h264(const uint8_t *data,size_t size);
