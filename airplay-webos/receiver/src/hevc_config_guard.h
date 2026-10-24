#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t offset[3], length[3], annexb_size;
} ux_hevc_config_sets;

// The pinned UxPlay hvc1 mirror config has VPS/SPS/PPS records at 0x75.
// Validate every byte range before the mirror thread reads or copies it.
static inline bool ux_hevc_config_parse(const uint8_t *data, size_t size,
                                         ux_hevc_config_sets *out) {
    if(!data || !out || size<0x75 || size>2097152) return false;
    const uint8_t tags[3]={0xa0,0xa1,0xa2};
    const int types[3]={32,33,34};
    size_t pos=0x75,total=12;
    for(int i=0;i<3;++i) {
        if(pos>size || size-pos<5 || data[pos]!=tags[i] ||
           data[pos+1]!=0 || data[pos+2]!=1) return false;
        size_t length=((size_t)data[pos+3]<<8)|data[pos+4];
        pos+=5;
        if(length<2 || length>65535 || length>size-pos ||
           ((data[pos]>>1)&63)!=types[i] || (data[pos]&0x80) ||
           !(data[pos+1]&7)) return false;
        out->offset[i]=pos;out->length[i]=length;
        pos+=length;total+=length;
    }
    if(total>196608) return false;
    out->annexb_size=total;
    return true;
}
