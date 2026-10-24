#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
struct AVCodecContext;
struct SwrContext;
class AudioDecoder {
public:
    ~AudioDecoder();
    bool decode(unsigned codec, const uint8_t *data, size_t size, std::vector<uint8_t>& pcm);
    void reset();
private:
    bool configure(unsigned codec);
    unsigned codec_ = 0;
    AVCodecContext *context_ = nullptr;
    SwrContext *resampler_ = nullptr;
};
