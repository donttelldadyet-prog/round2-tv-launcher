#pragma once
#include "media_rate.h"
#include <sstream>
#include <string>

inline std::string starfish_load_payload(const char *app_id, const char *video_codec,
                                         int width, int height, bool audio, int sample_rate,
                                         int video_source_buffer_bytes,
                                         const MediaRateFields& frame_rate, bool audio_only) {
    std::ostringstream p;
    p << "{\"args\":[{\"mediaTransportType\":\"BUFFERSTREAM\",\"option\":{\"appId\":\"" << app_id
      << "\",\"queryPosition\":false,\"externalStreamingInfo\":{\"contents\":{\"codec\":";
    if(audio_only) p << "{";
    else p << "{\"video\":\"" << video_codec << "\"";
    if(audio) p << (audio_only?"":",") << "\"audio\":\"PCM\"";
    p << "},\"esInfo\":{\"pauseAtDecodeTime\":true,\"ptsToDecode\":0,\"seperatedPTS\":true";
    if(!audio_only) p << ",\"videoFpsValue\":" << frame_rate.video_fps_value << ",\"videoFpsScale\":" << frame_rate.video_fps_scale;
    p << "},\"format\":\"RAW\",\"provider\":\"Chrome\"";
    if(audio) p << ",\"pcmInfo\":{\"channelMode\":\"stereo\",\"format\":\"S16LE\",\"sampleRate\":"
                << sample_rate / 1000.0 << ",\"bitsPerSample\":16,\"layout\":\"interleaved\"}";
    p << "},\"restartStreaming\":false,\"streamQualityInfo\":true,\"streamQualityInfoNonFlushable\":true,\"totalStreamSize\":256,"
         "\"bufferingCtrInfo\":{\"preBufferByte\":0,\"bufferMinLevel\":0,\"bufferMaxLevel\":0,\"qBufferLevelAudio\":0,\"qBufferLevelVideo\":0,"
         "\"srcBufferLevelAudio\":{\"minimum\":1024,\"maximum\":131072},\"srcBufferLevelVideo\":{\"minimum\":1024,\"maximum\":"
      << (audio_only?1024:video_source_buffer_bytes) << "}}}";
    if(!audio_only) p << ",\"adaptiveStreaming\":{\"audioOnly\":false,\"maxWidth\":" << width
                      << ",\"maxHeight\":" << height << ",\"maxFrameRate\":" << frame_rate.max_frame_rate << "}";
    // externalStreamingInfo and adaptiveStreaming (when present) are closed above.
    // Close the option and args item before the array and root object.
    p << "}}]}";
    return p.str();
}
