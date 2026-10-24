#pragma once
#include "settings.h"
#include <cstdint>

struct AdvertisedCapabilities {
    int width, height, max_fps;
    bool direct_video, hls, hevc, legacy_pairing;
};
enum class SenderVideoCodec { Unsupported, H264, Hevc };
enum class CodecNegotiationResult { Reject, Accept, AcceptH264Fallback };

// AirPlay bit 42 advertises multi-codec support; it does not select HEVC.
// Keep H.264 as a safe sender-selected fallback when HEVC support is enabled.
inline CodecNegotiationResult codec_negotiation_result(const Settings& settings,
                                                        SenderVideoCodec sender_codec) {
    Settings safe=settings;validate_settings(safe);
    if(sender_codec==SenderVideoCodec::H264)
        return safe.codec==MirrorCodec::ExperimentalHevc ?
            CodecNegotiationResult::AcceptH264Fallback : CodecNegotiationResult::Accept;
    if(sender_codec==SenderVideoCodec::Hevc && safe.codec==MirrorCodec::ExperimentalHevc)
        return CodecNegotiationResult::Accept;
    return CodecNegotiationResult::Reject;
}
inline AdvertisedCapabilities advertised_capabilities(const Settings& settings) {
    Settings safe=settings;validate_settings(safe);
    AdvertisedCapabilities result={safe.width,safe.height,safe.max_fps,false,false,
        safe.codec==MirrorCodec::ExperimentalHevc,true};
    return result;
}
// Pinned UxPlay 1.74 defaults from lib/dnssdint.h; these four overrides leave
// the effective DNS-SD mask unchanged. The mask alone does not prove playback.
static const uint64_t UXPLAY_PINNED_FEATURES = 0x000000005A7FFEE6ULL;
