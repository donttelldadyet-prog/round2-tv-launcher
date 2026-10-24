/* Round2: the NDL DirectMedia v2 calls, in C. The SDK's NDL header does not compile as C++
 * (its deprecation shims redeclare enums), so media.cpp calls these small wrappers instead. */
#define NDL_DIRECTMEDIA_API_VERSION 2
#include <NDL_directmedia.h>
#include "ndl_glue.h"

int r2_ndl_init(const char *app_id, void (*released)(const char *)) { return NDL_DirectMediaInit(app_id, released); }
void r2_ndl_foreground(void) { NDL_DirectMediaSetAppState(NDL_DIRECTMEDIA_APP_STATE_FOREGROUND); }
const char *r2_ndl_error(void) { return NDL_DirectMediaGetError(); }

int r2_ndl_load(int width, int height, int hevc, int with_audio, int sample_rate,
                void (*event)(int, long long, const char *)) {
    NDL_DIRECTMEDIA_DATA_INFO_T info = {0};
    if (width > 0 && height > 0) {
        info.video.width = width;
        info.video.height = height;
        info.video.type = hevc ? NDL_VIDEO_TYPE_H265 : NDL_VIDEO_TYPE_H264;
    }
    if (with_audio) {
        info.audio.pcm.type = NDL_AUDIO_TYPE_PCM;
        info.audio.pcm.format = NDL_DIRECTMEDIA_AUDIO_PCM_FORMAT_S16LE;
        info.audio.pcm.layout = "interleaved";
        info.audio.pcm.channelMode = NDL_DIRECTMEDIA_AUDIO_PCM_MODE_STEREO;
        info.audio.pcm.sampleRate = NDL_DIRECTMEDIA_AUDIO_PCM_SAMPLE_RATE_OF(sample_rate);
    }
    return NDL_DirectMediaLoad(&info, event);
}
int r2_ndl_video(const void *data, unsigned size, long long pts_ms) { return NDL_DirectVideoPlay((void *) data, size, pts_ms); }
int r2_ndl_audio(const void *data, unsigned size, long long pts_ms) { return NDL_DirectAudioPlay((void *) data, size, pts_ms); }
int r2_ndl_unload(void) { return NDL_DirectMediaUnload(); }
