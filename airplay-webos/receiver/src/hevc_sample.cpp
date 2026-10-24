#include "hevc_sample.h"
#include <algorithm>

namespace hevc_probe {
namespace {
struct Bits {
    std::vector<uint8_t> data;
    size_t bit = 0;
    bool get(unsigned count, uint32_t& out) {
        if (count > 32 || bit > data.size() * 8 || count > data.size() * 8 - bit) return false;
        out = 0;
        for (unsigned i = 0; i < count; ++i, ++bit)
            out = (out << 1) | ((data[bit / 8] >> (7 - bit % 8)) & 1);
        return true;
    }
    bool skip(size_t count) {
        if (bit > data.size() * 8 || count > data.size() * 8 - bit) return false;
        bit += count; return true;
    }
    bool ue(uint32_t& out) {
        unsigned zeros = 0; uint32_t value;
        do { if (!get(1, value)) return false; if (!value && ++zeros > 24) return false; } while (!value);
        if (!get(zeros, value)) return false;
        out = ((uint32_t(1) << zeros) - 1) + value;
        return true;
    }
};
bool sps(const uint8_t *data, size_t size, Sample& result) {
    if (size < 4 || size > 65536) return false;
    Bits b;
    b.data.reserve(size);
    for (size_t i = 2; i < size; ++i) {
        if (i >= 4 && data[i] == 3 && data[i-1] == 0 && data[i-2] == 0) continue;
        b.data.push_back(data[i]);
    }
    uint32_t v, vps_id, sps_id, sublayers, profile, chroma, width, height, depth_y, depth_c;
    if (!b.get(4,vps_id) || !b.get(3, sublayers) || sublayers > 6 || !b.skip(1) ||
        !b.skip(3) || !b.get(5, profile) || !b.skip(32 + 48 + 8)) return false;
    bool sub_profile[7] = {}, sub_level[7] = {};
    for(uint32_t i=0;i<sublayers;++i) {
        if(!b.get(1,v)) return false;sub_profile[i]=v!=0;
        if(!b.get(1,v)) return false;sub_level[i]=v!=0;
    }
    if(sublayers && !b.skip(2*(8-sublayers))) return false;
    for(uint32_t i=0;i<sublayers;++i) {
        if((sub_profile[i] && !b.skip(88)) || (sub_level[i] && !b.skip(8))) return false;
    }
    if (!b.ue(sps_id) || sps_id>15 || !b.ue(chroma) || chroma != 1 ||
        !b.ue(width) || !b.ue(height) ||
        !b.get(1, v)) return false;
    if (v) { uint32_t left, right, top, bottom;
        if (!b.ue(left) || !b.ue(right) || !b.ue(top) || !b.ue(bottom) ||
            left + right >= width/2 || top + bottom >= height/2) return false;
        width -= 2 * (left + right); height -= 2 * (top + bottom);
    }
    if (!b.ue(depth_y) || !b.ue(depth_c) ||
        !((profile==1 && depth_y==0 && depth_c==0) ||
          (profile==2 && depth_y==depth_c && (depth_y==0 || depth_y==2))) ||
        width == 0 || height == 0 || width > 4096 || height > 2160) return false;
    result.width = int(width); result.height = int(height);
    result.bit_depth = int(depth_y+8); result.profile = int(profile);
    result.vps_id=int(vps_id);result.sps_id=int(sps_id);
    return true;
}
struct Nal { size_t start, payload, end; int type; };
bool next_nal(const uint8_t *data, size_t size, size_t& pos, Nal& n) {
    size_t i = pos;
    while (i + 3 < size && !(data[i] == 0 && data[i+1] == 0 &&
           (data[i+2] == 1 || (data[i+2] == 0 && data[i+3] == 1)))) ++i;
    if (i + 3 >= size) return false;
    size_t prefix = data[i+2] == 1 ? 3 : 4;
    n.start = i; n.payload = i + prefix;
    size_t j = n.payload;
    while (j + 3 < size && !(data[j] == 0 && data[j+1] == 0 &&
           (data[j+2] == 1 || (data[j+2] == 0 && data[j+3] == 1)))) ++j;
    n.end = j + 3 < size ? j : size;
    pos = n.end;
    if (n.end - n.payload < 2) return false;
    n.type = (data[n.payload] >> 1) & 63;
    return (data[n.payload] & 0x80) == 0 && (data[n.payload+1] & 7) != 0;
}
}
bool parse_sps(const uint8_t *bytes, size_t size, Sample& result) {
    return sps(bytes,size,result);
}
bool parse_pps_ids(const uint8_t *bytes, size_t size, int& pps_id, int& sps_id) {
    if(!bytes || size<3 || size>65536) return false;
    Bits b;
    b.data.reserve(size);
    for(size_t i=2;i<size;++i) {
        if(i>=4 && bytes[i]==3 && !bytes[i-1] && !bytes[i-2])continue;
        b.data.push_back(bytes[i]);
    }
    uint32_t pps,sps;
    if(!b.ue(pps) || !b.ue(sps) || pps>63 || sps>15)return false;
    pps_id=int(pps);sps_id=int(sps);
    return true;
}
bool inspect(const uint8_t *bytes, size_t size, int expected_width,
             int expected_height, Sample& result, std::string& error) {
    result = Sample{};
    if (!bytes || size < 16 || size > kMaxFile ||
        !((expected_width == 1920 && expected_height == 1080) ||
          (expected_width == 3840 && expected_height == 2160))) {
        error = "size-or-profile-bound"; return false;
    }
    size_t pos = 0; Nal n{}; bool vps = false, pps = false, seen_sps = false, vcl = false;
    if (!next_nal(bytes, size, pos, n) || n.start != 0 || n.type != 35) {
        error = "missing-initial-aud"; return false;
    }
    size_t au_start = 0;
    while (true) {
        if (n.type == 35 && n.start != au_start) {
            if (!vcl || n.start - au_start > kMaxAccessUnit || result.units.size() >= kMaxFrames) {
                error = "access-unit-bound-or-empty"; return false;
            }
            result.units.push_back({au_start, n.start - au_start}); au_start = n.start; vcl = false;
        } else if (n.type == 32) vps = true;
        else if (n.type == 33) {
            if (seen_sps || !sps(bytes + n.payload, n.end - n.payload, result)) {
                error = "invalid-sps"; return false;
            }
            seen_sps = true;
        } else if (n.type == 34) pps = true;
        else if (n.type <= 31) {
            if (!vps || !seen_sps || !pps) { error = "missing-parameter-sets"; return false; }
            vcl = true;
        }
        if (pos == size) break;
        if (!next_nal(bytes, size, pos, n)) { error = "malformed-nal"; return false; }
    }
    if (!vcl || size - au_start > kMaxAccessUnit || result.units.size() >= kMaxFrames) {
        error = "final-access-unit-bound"; return false;
    }
    result.units.push_back({au_start, size - au_start});
    if (result.width != expected_width || result.height != expected_height || result.units.empty() ||
        result.profile!=1 || result.bit_depth!=8) {
        error = "dimension-mismatch"; return false;
    }
    return true;
}
}
