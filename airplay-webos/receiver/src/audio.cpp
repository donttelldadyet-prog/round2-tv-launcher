#include "audio.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}
#include <cstdio>
#include <cstring>
AudioDecoder::~AudioDecoder() { reset(); }
void AudioDecoder::reset() {
    swr_free(&resampler_); avcodec_free_context(&context_); codec_ = 0;
}
bool AudioDecoder::configure(unsigned codec) {
    reset();
    AVCodecID id = codec == 2 ? AV_CODEC_ID_ALAC : AV_CODEC_ID_AAC;
    if (codec != 2 && codec != 4 && codec != 8) return false;
    const AVCodec *decoder = avcodec_find_decoder(id);
    if (!decoder || !(context_ = avcodec_alloc_context3(decoder))) return false;
    const uint8_t eld[] = {0xf8,0xe8,0x50,0x00};
    const uint8_t lc[] = {0x12,0x10};
    const uint8_t alac[] = {0,0,0,0x24,0x61,0x6c,0x61,0x63,0,0,0,0,0,0,1,0x60,0,0x10,0x28,0x0a,0x0e,2,0,0xff,0,0,0,0,0,0,0,0,0,0,0xac,0x44};
    const uint8_t *config = codec==2?alac:codec==8?eld:lc;
    size_t length = codec==2?sizeof(alac):codec==8?sizeof(eld):sizeof(lc);
    context_->extradata = static_cast<uint8_t *>(av_mallocz(length + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!context_->extradata) { reset(); return false; }
    memcpy(context_->extradata,config,length); context_->extradata_size=length;
    context_->sample_rate=44100;
    av_channel_layout_default(&context_->ch_layout,2);
    if (avcodec_open2(context_,decoder,nullptr)<0) { reset(); return false; }
    codec_=codec;
    fprintf(stderr,"[audio] decoder=%s ct=%u\n",decoder->name,codec);
    return true;
}
bool AudioDecoder::decode(unsigned codec, const uint8_t *data, size_t size, std::vector<uint8_t>& pcm) {
    pcm.clear();
    if (!data || !size || size>1048576) return false;
    if (codec==1) {
        if (size%4) return false;
        pcm.assign(data,data+size); return true;
    }
    if (codec_!=codec && !configure(codec)) return false;
    AVPacket *packet=av_packet_alloc(); AVFrame *frame=av_frame_alloc();
    if (!packet || !frame) { av_packet_free(&packet); av_frame_free(&frame); return false; }
    bool ok = av_new_packet(packet,size)>=0;
    if (ok) { memcpy(packet->data,data,size); ok=avcodec_send_packet(context_,packet)>=0; }
    while (ok) {
        int r=avcodec_receive_frame(context_,frame);
        if (r==AVERROR(EAGAIN) || r==AVERROR_EOF) break;
        if (r<0) { ok=false; break; }
        if (!resampler_) {
            AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
            if (swr_alloc_set_opts2(&resampler_,&stereo,AV_SAMPLE_FMT_S16,44100,
                &frame->ch_layout,static_cast<AVSampleFormat>(frame->format),frame->sample_rate,0,nullptr)<0 || swr_init(resampler_)<0) {
                ok=false; break;
            }
        }
        int capacity=swr_get_out_samples(resampler_,frame->nb_samples);
        if (capacity<0 || capacity>65536) { ok=false; break; }
        size_t offset=pcm.size(); pcm.resize(offset+capacity*4);
        uint8_t *output=pcm.data()+offset;
        int samples=swr_convert(resampler_,&output,capacity,const_cast<const uint8_t **>(frame->extended_data),frame->nb_samples);
        if (samples<0) { ok=false; break; }
        pcm.resize(offset+samples*4);
        av_frame_unref(frame);
    }
    av_frame_free(&frame); av_packet_free(&packet);
    return ok;
}
