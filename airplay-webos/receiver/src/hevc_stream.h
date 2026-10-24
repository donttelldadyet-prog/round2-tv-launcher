#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct HevcFrameInfo {
    bool valid=false, keyframe=false, configuration_changed=false, has_aud=false;
    bool has_vps=false, has_sps=false, has_pps=false;
    int width=0, height=0, profile=0, bit_depth=0;
};

// UxPlay delivers one Annex B access unit per mirror callback. Parameter sets
// may be prepended to a random-access unit; cache them across subsequent AUs.
class HevcStream {
public:
    HevcFrameInfo inspect(const uint8_t *data, size_t size);
    bool prepend_missing_parameter_sets(std::vector<uint8_t>& access_unit,
                                        const HevcFrameInfo& info, bool& added) const;
    void reset();
private:
    std::vector<uint8_t> vps_, sps_, pps_;
    int width_=0, height_=0;
    int profile_=0, bit_depth_=0;
    int vps_id_=-1, sps_vps_id_=-1, sps_id_=-1, pps_sps_id_=-1;
};
inline bool supported_hevc_mirror_profile(const HevcFrameInfo& info) {
    // AirPlay mirroring and Starfish output are verified only for Main 8-bit SDR.
    return info.valid && info.profile==1 && info.bit_depth==8;
}
bool ensure_hevc_aud(std::vector<uint8_t>& access_unit, bool has_aud);
