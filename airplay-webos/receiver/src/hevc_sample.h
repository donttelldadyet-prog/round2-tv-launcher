#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hevc_probe {
static const size_t kMaxFile = 12 * 1024 * 1024;
static const size_t kMaxAccessUnit = 2 * 1024 * 1024;
static const size_t kMaxFrames = 60;
struct AccessUnit { size_t offset; size_t size; };
struct Sample {
    int width = 0, height = 0, bit_depth = 0, profile = 0;
    int vps_id = -1, sps_id = -1;
    std::vector<AccessUnit> units;
};
bool parse_sps(const uint8_t *bytes, size_t size, Sample& result);
bool parse_pps_ids(const uint8_t *bytes, size_t size, int& pps_id, int& sps_id);
// Accept only a short Annex B Main 8-bit 4:2:0 stream with AUD-delimited AUs.
bool inspect(const uint8_t *bytes, size_t size, int expected_width,
             int expected_height, Sample& result, std::string& error);
}
