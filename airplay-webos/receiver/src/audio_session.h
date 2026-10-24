#pragma once

struct AirPlayAudioIntent {
    unsigned codec;
    unsigned samples_per_frame;
    bool using_screen;
    bool is_media;
    bool configured;

    AirPlayAudioIntent(unsigned codec_value = 0, unsigned samples = 0,
                       bool screen = false, bool media = false, bool is_configured = false)
        : codec(codec_value), samples_per_frame(samples), using_screen(screen),
          is_media(media), configured(is_configured) {}
};

inline bool standalone_audio_candidate(const AirPlayAudioIntent& intent, bool mirror_active) {
    const bool decodable = intent.codec == 1 || intent.codec == 2 || intent.codec == 4 || intent.codec == 8;
    return intent.configured && decodable && !intent.using_screen && !mirror_active;
}

inline bool standalone_audio_yield_to_video(bool audio_only_session, bool video_packet) {
    return audio_only_session && video_packet;
}
