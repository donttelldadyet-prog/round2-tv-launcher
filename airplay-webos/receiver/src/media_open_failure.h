#pragma once

enum class MediaOpenFailure {
    None,
    InvalidConfiguration,
    AcbCreate,
    AcbInitialize,
    MediaIdRegistration,
    StarfishLoad
};

inline const char *media_open_failure_message(MediaOpenFailure failure) {
    switch (failure) {
    case MediaOpenFailure::InvalidConfiguration: return "Video playback could not start (invalid media configuration).";
    case MediaOpenFailure::AcbCreate: return "Video playback could not start (ACB creation failed).";
    case MediaOpenFailure::AcbInitialize: return "Video playback could not start (the TV's media system did not start).";
    case MediaOpenFailure::MediaIdRegistration: return "Video playback could not start (ACB media registration failed).";
    case MediaOpenFailure::StarfishLoad: return "Video playback could not start (the TV refused the video stream).";
    case MediaOpenFailure::None: break;
    }
    return "Video playback could not start.";
}
