#include "h264.h"
#include <limits>
namespace {
struct Bits {
    std::vector<uint8_t> data;
    size_t offset=0;
    bool ok=true;
    unsigned get(unsigned n) {
        if(n>32 || offset+n>data.size()*8) {ok=false;return 0;}
        unsigned v=0;while(n--) {v=(v<<1)|((data[offset/8]>>(7-offset%8))&1);++offset;}return v;
    }
    unsigned ue() {
        unsigned zeros=0;
        while(ok && get(1)==0) {if(++zeros>30){ok=false;return 0;}}
        return ((1u<<zeros)-1)+get(zeros);
    }
    int se() {unsigned n=ue();return n&1?int((n+1)/2):-int(n/2);}
};
bool sps(const uint8_t *p,size_t n,int& width,int& height) {
    if(n<4 || n>65536)return false;
    Bits b;
    unsigned zeros=0;
    for(size_t i=1;i<n;++i) {
        if(zeros>=2 && p[i]==3) {zeros=0;continue;}
        b.data.push_back(p[i]);zeros=p[i]==0?zeros+1:0;
    }
    unsigned profile=b.get(8);b.get(8);b.get(8);b.ue();
    unsigned chroma=1,separate=0;
    if(profile==100||profile==110||profile==122||profile==244||profile==44||profile==83||profile==86||profile==118||profile==128||profile==138||profile==139||profile==134||profile==135) {
        chroma=b.ue();if(chroma>3)return false;
        if(chroma==3)separate=b.get(1);
        b.ue();b.ue();b.get(1);
        if(b.get(1)) for(unsigned i=0;i<(chroma==3?12u:8u);++i) {
            if(b.get(1)) {
                int last=8,next=8;
                for(unsigned j=0;j<(i<6?16u:64u)&&b.ok;++j) {
                    if(next) next=int((int64_t(last)+b.se()+256)%256);
                    if(next)last=next;
                }
            }
        }
    }
    b.ue();unsigned order=b.ue();
    if(order==0)b.ue();
    else if(order==1) {b.get(1);b.se();b.se();unsigned count=b.ue();if(count>255)return false;for(unsigned i=0;i<count;++i)b.se();}
    else if(order>2)return false;
    b.ue();b.get(1);
    unsigned mbw=b.ue(),mbh=b.ue(),frame=b.get(1);
    if(!frame)b.get(1);b.get(1);
    unsigned left=0,right=0,top=0,bottom=0;
    if(b.get(1)){left=b.ue();right=b.ue();top=b.ue();bottom=b.ue();}
    if(!b.ok||mbw>119||mbh>119)return false;
    unsigned array=separate?0:chroma;
    unsigned cropx=array==1||array==2?2:1;
    unsigned cropy=(array==1?2:1)*(2-frame);
    int64_t w=int64_t(mbw+1)*16-int64_t(left+uint64_t(right))*cropx;
    int64_t h=int64_t(mbh+1)*16*(2-frame)-int64_t(top+uint64_t(bottom))*cropy;
    if(w<16||h<16||w>1920||h>1920||w*h>2073600)return false;
    width=w;height=h;return true;
}
size_t prefix(const uint8_t *p,size_t n,size_t i) {
    if(i+3<=n&&p[i]==0&&p[i+1]==0) {
        if(p[i+2]==1)return 3;
        if(i+4<=n&&p[i+2]==0&&p[i+3]==1)return 4;
    }
    return 0;
}
}
H264Info inspect_h264(const uint8_t *p,size_t n) {
    H264Info info;if(!p||n<5||n>2*1024*1024||!prefix(p,n,0))return info;
    size_t pos=0;
    while(pos<n) {
        size_t len=prefix(p,n,pos);if(!len||pos+len>=n)return H264Info{};
        size_t begin=pos+len,end=begin+1;
        while(end<n&&!prefix(p,n,end))++end;
        unsigned type=p[begin]&31;
        if(p[begin]&128)return H264Info{};
        if(type==1||type==5)info.picture=true;
        if(type==5)info.keyframe=true;
        if(type==7) {if(!sps(p+begin,end-begin,info.width,info.height))return H264Info{};}
        if(type==7||type==8) {
            if(info.configuration.size()+end-pos>65536)return H264Info{};
            info.configuration.insert(info.configuration.end(),p+pos,p+end);
        }
        pos=end;
    }
    info.valid=true;return info;
}
