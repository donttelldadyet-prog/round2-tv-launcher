#include "hevc_stream.h"
#include "hevc_sample.h"
#include <algorithm>

namespace {
size_t start_code(const uint8_t *data, size_t size, size_t from, size_t& prefix) {
    for(size_t i=from; i+3<=size; ++i) {
        if(data[i] || data[i+1]) continue;
        if(data[i+2]==1) {prefix=3;return i;}
        if(i+4<=size && !data[i+2] && data[i+3]==1) {prefix=4;return i;}
    }
    return size;
}
}
void HevcStream::reset() {vps_.clear();sps_.clear();pps_.clear();width_=height_=profile_=bit_depth_=0;vps_id_=sps_vps_id_=sps_id_=pps_sps_id_=-1;}
HevcFrameInfo HevcStream::inspect(const uint8_t *data, size_t size) {
    HevcFrameInfo result;
    if(!data || size<6 || size>hevc_probe::kMaxAccessUnit) return result;
    size_t prefix=0, pos=start_code(data,size,0,prefix);
    if(pos!=0) return result;
    bool vcl=false, changed=false;
    std::vector<uint8_t> vps=vps_,sps=sps_,pps=pps_;
    int width=width_,height=height_,profile=profile_,bit_depth=bit_depth_;
    int vps_id=vps_id_,sps_vps_id=sps_vps_id_,sps_id=sps_id_,pps_sps_id=pps_sps_id_;
    unsigned count=0;
    while(pos<size && ++count<=128) {
        size_t begin=pos+prefix, next_prefix=0;
        size_t end=start_code(data,size,begin,next_prefix);
        if(end-begin<2) return HevcFrameInfo{};
        const int type=(data[begin]>>1)&63;
        if((data[begin]&0x80) || !(data[begin+1]&7)) return HevcFrameInfo{};
        if(type==35) result.has_aud=true;
        if(type==32 || type==33 || type==34) {
            if(end-begin>65536) return HevcFrameInfo{};
            std::vector<uint8_t> nal(data+begin,data+end);
            if(type==33) {
                hevc_probe::Sample parsed;
                if(!hevc_probe::parse_sps(nal.data(),nal.size(),parsed) ||
                   parsed.width>3840 || parsed.height>2160 ||
                   int64_t(parsed.width)*parsed.height>3840LL*2160) return HevcFrameInfo{};
                width=parsed.width;height=parsed.height;profile=parsed.profile;bit_depth=parsed.bit_depth;
                sps_vps_id=parsed.vps_id;sps_id=parsed.sps_id;
                result.has_sps=true;
                if(sps!=nal) {sps.swap(nal);changed=true;}
            } else if(type==32) {
                if(nal.size()<3) return HevcFrameInfo{};
                vps_id=nal[2]>>4;
                result.has_vps=true;
                if(vps!=nal) {vps.swap(nal);changed=true;}
            } else {
                int pps_id;
                if(!hevc_probe::parse_pps_ids(nal.data(),nal.size(),pps_id,pps_sps_id)) return HevcFrameInfo{};
                result.has_pps=true;
                if(pps!=nal) {pps.swap(nal);changed=true;}
            }
        } else if(type<=31) {
            vcl=true;
            if(type>=16 && type<=21) result.keyframe=true;
        }
        if(end==size) break;
        pos=end;prefix=next_prefix;
    }
    if(count>128 || !vcl || vps.empty() || sps.empty() || pps.empty() || !width ||
       vps_id<0 || sps_vps_id!=vps_id || sps_id<0 || pps_sps_id!=sps_id) return HevcFrameInfo{};
    vps_.swap(vps);sps_.swap(sps);pps_.swap(pps);
    width_=width;height_=height;profile_=profile;bit_depth_=bit_depth;
    vps_id_=vps_id;sps_vps_id_=sps_vps_id;sps_id_=sps_id;pps_sps_id_=pps_sps_id;
    result.valid=true;result.configuration_changed=changed;
    result.width=width_;result.height=height_;result.profile=profile_;result.bit_depth=bit_depth_;
    return result;
}
bool HevcStream::prepend_missing_parameter_sets(std::vector<uint8_t>& access_unit,
                                                 const HevcFrameInfo& info, bool& added) const {
    added=false;
    if(!info.valid || vps_.empty() || sps_.empty() || pps_.empty()) return false;
    if(info.has_vps && info.has_sps && info.has_pps) return true;
    static const uint8_t start[]={0,0,0,1};
    size_t extra=0;
    if(!info.has_vps) extra+=sizeof(start)+vps_.size();
    if(!info.has_sps) extra+=sizeof(start)+sps_.size();
    if(!info.has_pps) extra+=sizeof(start)+pps_.size();
    if(access_unit.size()>hevc_probe::kMaxAccessUnit ||
       extra>hevc_probe::kMaxAccessUnit-access_unit.size()) return false;
    std::vector<uint8_t> prefix;
    prefix.reserve(extra);
    const std::vector<uint8_t>* sets[]={&vps_,&sps_,&pps_};
    const bool present[]={info.has_vps,info.has_sps,info.has_pps};
    for(unsigned i=0;i<3;++i) if(!present[i]) {
        prefix.insert(prefix.end(),start,start+sizeof(start));
        prefix.insert(prefix.end(),sets[i]->begin(),sets[i]->end());
    }
    access_unit.insert(access_unit.begin(),prefix.begin(),prefix.end());
    added=true;
    return true;
}
bool ensure_hevc_aud(std::vector<uint8_t>& access_unit, bool has_aud) {
    if(has_aud) return !access_unit.empty() && access_unit.size()<=hevc_probe::kMaxAccessUnit;
    static const uint8_t aud[]={0,0,0,1,0x46,0x01,0x50};
    if(access_unit.empty() || access_unit.size()>hevc_probe::kMaxAccessUnit-sizeof(aud)) return false;
    access_unit.insert(access_unit.begin(),aud,aud+sizeof(aud));
    return true;
}
