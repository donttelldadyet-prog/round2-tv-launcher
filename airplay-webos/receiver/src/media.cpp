// Round2: video and audio output through NDL DirectMedia v2 (LG webOS 5 and newer).
//
// UxAir's original backend used Starfish Media Pipeline with libAcbAPI for the video plane,
// which webOS 5 removed. NDL DirectMedia v2 (libNDL_directmedia) takes H.264/H.265 and PCM in
// one Load and owns the video plane itself. Tested on a 75NANO80PTA (webOS 5) with the AirPlay
// lab: timestamps must be in milliseconds, the screen area is left at its default, and the app
// window must keep being cleared to transparent (main.cpp does that while mirroring).
//
// Modified from UxAir 1.1.0 (src/media.cpp), GPL-3.0-or-later. See LICENSE and ROUND2.md.
#include "media.h"
#include "media_rate.h"
#include "settings.h"
#include "ndl_glue.h"
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <time.h>

struct Media::Impl { std::mutex mutex; };

static Media *g_media = nullptr;          // NDL's callback carries no user pointer
static bool g_ndl_initialized = false;
static int64_t g_load_ns = 0;            // timestamps count from Load, like moonlight-tv's NDL backend
static int64_t monotonic_ns() { timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return int64_t(t.tv_sec)*1000000000LL+t.tv_nsec; }

static void resource_released(const char *type) {
    fprintf(stderr,"[ndl] resource released by the TV: %s\n",type?type:"?");
    if(g_media) g_media->failed=true;
}
static void load_event(int type, long long value, const char *message) {
    Media *m=g_media;
    if(type!=0x16&&type!=0x17&&type!=0x1a&&type!=0x1c)
        fprintf(stderr,"[ndl] event=0x%x value=%lld %s\n",type,value,message?message:"");
    if(!m) return;
    switch(type) {
    case 0x16: m->ready=true; fprintf(stderr,"[media-state] load-completed\n"); break;
    case 0x1a: m->playing=true; fprintf(stderr,"[media-state] playing\n"); break;
    case 0x1c: m->ended=true; break;
    default: break;
    }
}

Media::Media() : impl_(new Impl) {}
Media::~Media() { close(); delete impl_; }
void Media::event(int, int64_t, const char *, void *) {}

bool Media::open(const char *app_id, int width, int height, bool audio, int rate,
                 int video_source_buffer_bytes, int canvas_width, int canvas_height, MirrorCodec codec, int max_fps, bool audio_only) {
    (void)video_source_buffer_bytes; (void)canvas_width; (void)canvas_height;
    open_failure_=MediaOpenFailure::None;
    MediaRateFields frame_rate;
    if ((!audio_only && (width<=0 || height<=0)) || !media_rate_fields(max_fps,frame_rate) ||
        (!audio_only && codec==MirrorCodec::H264 && int64_t(width)*height>1920*1080) ||
        (!audio_only && codec==MirrorCodec::ExperimentalHevc && int64_t(width)*height>3840*2160)) {
        open_failure_=MediaOpenFailure::InvalidConfiguration;
        fprintf(stderr,"[media-init] failure=invalid-configuration\n");
        return false;
    }
    close();
    ready = playing = ended = failed = false;
    frames = 0;
    video_info_width=width; video_info_height=height;
    acb_window_width=acb_window_height=0;
    last_frame_event_ns = 0;
    feed_video_accepted=feed_audio_accepted=0;
    feed_video_retries=feed_audio_retries=0;
    feed_video_rejected=0;
    feed_calls=0;
    feed_time_ns=feed_max_time_ns=0;
    g_media=this;
    if(!g_ndl_initialized) {
        if(r2_ndl_init(app_id,resource_released)!=0) {
            open_failure_=MediaOpenFailure::AcbInitialize;
            fprintf(stderr,"[media-init] NDL_DirectMediaInit failed: %s\n",r2_ndl_error());
            return false;
        }
        g_ndl_initialized=true;
    }
    r2_ndl_foreground();
    fprintf(stderr,"[media] NDL Load mode=%s video=%dx%d %s audio=%s %d Hz\n",audio_only?"audio-only":"mirroring",
            width,height,codec==MirrorCodec::ExperimentalHevc?"H265":"H264",audio?"PCM":"none",rate);
    int64_t phase=monotonic_ns();
    if(r2_ndl_load(audio_only?0:width,audio_only?0:height,codec==MirrorCodec::ExperimentalHevc,audio,rate,load_event)!=0) {
        open_failure_=MediaOpenFailure::StarfishLoad;
        fprintf(stderr,"[media-init] NDL_DirectMediaLoad failed: %s\n",r2_ndl_error());
        g_media=nullptr;
        return false;
    }
    loaded_=true;
    g_load_ns=monotonic_ns();
    ready=true;   // NDL accepts buffers straight after Load; LOADCOMPLETED follows
    fprintf(stderr,"[media-init] NDL Load end ms=%.2f\n",(monotonic_ns()-phase)/1e6);
    if(audio) {   // prime the audio path, as moonlight-tv's NDL backend does
        unsigned short empty[4]={0,0,0,0};
        r2_ndl_audio(empty,sizeof(empty),0);
    }
    return true;
}

void Media::tick() {}

FeedResult Media::feed(const void *data, size_t size, int64_t pts_ns, bool video) {
    if (!loaded_ || failed || !data || !size) {
        if(video)++feed_video_rejected;
        return FeedResult::Error;
    }
    int64_t before=monotonic_ns();
    // Low latency (Round2 0.1.2): stamp each buffer with the time it is handed over, in ms since
    // Load, instead of the sender's media time. The TV then shows frames as they come rather than
    // holding a buffer to line them up; that was about a second of delay on Bella.
    (void)pts_ns;
    long long pts_ms=(before-g_load_ns)/1000000;
    int rc=video ? r2_ndl_video(data,unsigned(size),pts_ms) : r2_ndl_audio(data,unsigned(size),pts_ms);
    int64_t after=monotonic_ns();
    uint64_t elapsed=after>before?uint64_t(after-before):0;
    ++feed_calls;
    feed_time_ns.fetch_add(elapsed);
    uint64_t previous=feed_max_time_ns.load();
    while(previous<elapsed&&!feed_max_time_ns.compare_exchange_weak(previous,elapsed)) {}
    if(rc==0) {
        if(video) { ++frames; last_frame_event_ns=after; ++feed_video_accepted; }
        else ++feed_audio_accepted;
        return FeedResult::Accepted;
    }
    // NDL reports a full buffer as an error; try again shortly rather than ending the session
    (video?feed_video_retries:feed_audio_retries).fetch_add(1);
    return FeedResult::Retry;
}

void Media::eos() {}
void Media::pause(bool) {}   // NDL v2 has no pause; AirPlay pauses by not sending data

void Media::close() {
    if(loaded_) {
        r2_ndl_unload();
        fprintf(stderr,"[media] unloaded\n");
    }
    loaded_=false;
    if(g_media==this) g_media=nullptr;
}
