#include "hevc_sample.h"
#include "hevc_launch.h"
#include "hevc_feed.h"
#include "hevc_gate.h"
#include <StarfishMediaAPIs.h>
#include <AcbAPI.h>
#include <SDL.h>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <time.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

// An isolated experiment: no receiver, discovery, sockets, pairing or persistent settings.
namespace {
const char kAppId[] = "com.github.depius.uxair.hevcprobe";
volatile sig_atomic_t stop_requested = 0;
volatile sig_atomic_t stop_signal_number = 0;
volatile sig_atomic_t stop_sender_pid = 0;
volatile sig_atomic_t stop_code = 0;
volatile sig_atomic_t stop_phase = 0;
volatile sig_atomic_t phase_number = 0; // 0=start, 1=1080, 2=UHD, 3=cleanup, 4=done
volatile sig_atomic_t internal_shutdown_requested = 0;
volatile sig_atomic_t stop_at_sec = 0;
volatile sig_atomic_t stop_at_msec = 0;
void stop_signal(int number, siginfo_t *info, void *) {
    timespec t{};
    if (clock_gettime(CLOCK_MONOTONIC,&t)==0) {
        stop_at_sec=sig_atomic_t(t.tv_sec);
        stop_at_msec=sig_atomic_t(t.tv_nsec/1000000);
    }
    stop_sender_pid=info?info->si_pid:0;
    stop_code=info?info->si_code:0;
    stop_phase=phase_number;
    stop_signal_number=number;
    stop_requested=1;
}
void hard_deadline(int) {
    const char message[] = "[hevc-probe] hard-deadline: synchronous firmware call stalled; process exits\n";
    write(STDERR_FILENO, message, sizeof(message)-1);
    _exit(124); // Last resort; normal paths explicitly unload and finalize.
}
int64_t now_ms() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return int64_t(t.tv_sec)*1000+t.tv_nsec/1000000; }
int rss_kib() {
    FILE *f=fopen("/proc/self/status","r"); if (!f) return -1;
    char line[160]; int rss=-1;
    while (fgets(line,sizeof(line),f)) if (sscanf(line,"VmRSS: %d kB",&rss)==1) break;
    fclose(f); return rss;
}
std::string self_dir() {
    char path[4096]; ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);
    if (n<1) return "";
    path[n]=0;
    std::string s(path);
    return s.substr(0,s.rfind('/'));
}
int dimension(const char *s,const char *key) {
    if (!s) return 0;
    const char *p=strstr(s,key);
    if (!p) return 0;
    p += strlen(key); char *end=nullptr; long n=strtol(p,&end,10);
    return end!=p && n>0 && n<=8192 ? int(n) : 0;
}
struct Events {
    std::atomic<bool> loaded{false}, playing{false}, failed{false};
    std::atomic<int> failure_source{0}; // 1=Starfish error event
    std::atomic<unsigned> frame_ready{0}, errors{0}, event_logs{0};
    std::atomic<int> video_width{0}, video_height{0};
    std::atomic<int64_t> load_event_ms{0}, playing_event_ms{0}, first_frame_event_ms{0};
    std::atomic<unsigned> need_data{0}, enough_data{0}, buffer_low{0};
    std::mutex info_mutex;
    std::string video_info;
    std::atomic<bool> info_pending{false};
};
void event(int type,int64_t value,const char *message,void *user) {
    Events *e=static_cast<Events *>(user);
    if (type==PF_EVENT_TYPE_FRAMEREADY) {
        if (++e->frame_ready==1) e->first_frame_event_ms=now_ms();
        return;
    }
    if (type==PF_EVENT_TYPE_STR_STATE_UPDATE__LOADCOMPLETED) {
        e->loaded=true; e->load_event_ms=now_ms();
    }
    if (type==PF_EVENT_TYPE_STR_STATE_UPDATE__PLAYING) {
        e->playing=true; e->playing_event_ms=now_ms();
    }
    if (type==PF_EVENT_TYPE_INT_NEED_DATA) ++e->need_data;
    if (type==PF_EVENT_TYPE_INT_ENOUGH_DATA) ++e->enough_data;
    if (type==PF_EVENT_TYPE_INT_BUFFERLOW) ++e->buffer_low;
    if (type==PF_EVENT_TYPE_INT_ERROR || type==PF_EVENT_TYPE_STR_ERROR) {
        e->failure_source=1; e->failed=true; ++e->errors;
    }
    if (type==PF_EVENT_TYPE_STR_VIDEO_INFO) {
        e->video_width=dimension(message,"\"width\":");
        e->video_height=dimension(message,"\"height\":");
        if (message && strnlen(message,2048)<2048) {
            std::lock_guard<std::mutex> lock(e->info_mutex);
            e->video_info=message; e->info_pending=true;
        }
    }
    if (e->event_logs.fetch_add(1)<48)
        fprintf(stderr,"[hevc-probe] event type=0x%x value=%lld video_info=%dx%d errors=%u\n",
                type,(long long)value,e->video_width.load(),e->video_height.load(),e->errors.load());
}
void acb_event(long,long,long type,long app,long play,const char *) {
    static std::atomic<unsigned> logged{0};
    if (logged.fetch_add(1)<96)
        fprintf(stderr,"[hevc-probe] acb event=%ld app=%ld play=%ld\n",type,app,play);
}
// The NDK prototype omits the taskId that this firmware's wrapper expects.
extern "C" int acb_video_info(long,const char *,long *) __asm__("AcbAPI_setMediaVideoData");
struct Player {
    long acb=0, task=0; StarfishMediaAPIs *api=nullptr;
    bool acb_initialized=false, loaded=false, sink=false, acb_playing=false;
    int acb_sink_raw=-1, acb_loaded_state_raw=-1;
    Events events;
    ~Player() { close(); }
    bool open(int width,int height,int window_width,int window_height) {
        int64_t t=now_ms(); acb=AcbAPI_create();
        fprintf(stderr,"[hevc-probe] acb-create result=%d ms=%lld\n",acb!=0,(long long)(now_ms()-t));
        if (!acb) return false;
        t=now_ms(); acb_initialized=AcbAPI_initialize(acb,PLAYER_TYPE_MSE,kAppId,acb_event);
        fprintf(stderr,"[hevc-probe] acb-init result=%d ms=%lld\n",acb_initialized,(long long)(now_ms()-t));
        if (!acb_initialized) return false;
        t=now_ms(); api=new StarfishMediaAPIs(nullptr);
        fprintf(stderr,"[hevc-probe] starfish-construct result=%d ms=%lld\n",api!=nullptr,(long long)(now_ms()-t));
        if (!api) return false;
        const bool media_id=AcbAPI_setMediaId(acb,api->getMediaID());
        fprintf(stderr,"[hevc-probe] acb-media-id accepted=%d\n",media_id);
        if (!media_id) return false;
        char payload[1400];
        int n=snprintf(payload,sizeof(payload),
            "{\"args\":[{\"mediaTransportType\":\"BUFFERSTREAM\",\"option\":{\"appId\":\"%s\","
            "\"queryPosition\":false,\"externalStreamingInfo\":{\"contents\":{\"codec\":{\"video\":\"H265\"},"
            "\"esInfo\":{\"pauseAtDecodeTime\":true,\"ptsToDecode\":0,\"seperatedPTS\":true,"
            "\"videoFpsValue\":30,\"videoFpsScale\":1},\"format\":\"RAW\",\"provider\":\"Chrome\"},"
            "\"restartStreaming\":false,\"streamQualityInfo\":true,\"streamQualityInfoNonFlushable\":true,"
            "\"totalStreamSize\":256,\"bufferingCtrInfo\":{\"preBufferByte\":0,\"bufferMinLevel\":0,"
            "\"bufferMaxLevel\":0,\"qBufferLevelAudio\":0,\"qBufferLevelVideo\":0,"
            "\"srcBufferLevelAudio\":{\"minimum\":1024,\"maximum\":131072},"
            "\"srcBufferLevelVideo\":{\"minimum\":1024,\"maximum\":2097152}}},"
            "\"adaptiveStreaming\":{\"audioOnly\":false,\"maxWidth\":%d,\"maxHeight\":%d,\"maxFrameRate\":30}}}]}",
            kAppId,width,height);
        if (n<0 || size_t(n)>=sizeof(payload)) {
            fprintf(stderr,"[hevc-probe] load-config-size-failed\n"); return false;
        }
        fprintf(stderr,"[hevc-probe] load-begin codec=H265 transport=BUFFERSTREAM format=RAW bounds=%dx%d\n",width,height);
        t=now_ms(); loaded=api->Load(payload,event,&events);
        fprintf(stderr,"[hevc-probe] load-return accepted=%d ms=%lld rss_kib=%d\n",loaded,(long long)(now_ms()-t),rss_kib());
        if (!loaded) return false;
        t=now_ms(); int window_result=AcbAPI_setDisplayWindow(acb,0,0,window_width,window_height,true,&task);
        fprintf(stderr,"[hevc-probe] acb-window requested=%dx%d raw_result=%d ms=%lld (not output-plane proof)\n",
                window_width,window_height,window_result,(long long)(now_ms()-t));
        return true; // ACB return-code convention is not established on this firmware.
    }
    void tick() {
        if (!loaded || events.failed) return;
        if (events.loaded && !sink) {
            acb_sink_raw=AcbAPI_setSinkType(acb,SINK_TYPE_MAIN);
            acb_loaded_state_raw=AcbAPI_setState(acb,APPSTATE_FOREGROUND,PLAYSTATE_LOADED,&task);
            fprintf(stderr,"[hevc-probe] acb-sink-raw=%d loaded-state-raw=%d "
                    "(return convention unverified)\n",acb_sink_raw,acb_loaded_state_raw);
            // The B7 returned 0 here while ACB LOADED and Starfish PLAYING
            // transitions succeeded. The working H.264 path does not gate
            // feeding on this wrapper's undocumented return convention.
            sink=true;
        }
        if (events.info_pending.exchange(false)) {
            std::string info;
            { std::lock_guard<std::mutex> lock(events.info_mutex); info=events.video_info; }
            int result=acb_video_info(acb,info.c_str(),&task);
            fprintf(stderr,"[hevc-probe] acb-video-info result=%d width=%d height=%d\n",
                result,events.video_width.load(),events.video_height.load());
        }
        if (events.playing && sink && !acb_playing) {
            int b=AcbAPI_setState(acb,APPSTATE_FOREGROUND,PLAYSTATE_PLAYING,&task);
            fprintf(stderr,"[hevc-probe] acb-playing-state-raw=%d\n",b);
            acb_playing=true;
        }
    }
    bool close() {
        if (!acb && !api) return true;
        phase_number=3;
        fprintf(stderr,"[hevc-probe] cleanup-begin pid=%ld mono_ms=%lld internal_shutdown=%d\n",
                long(getpid()),(long long)now_ms(),int(internal_shutdown_requested));
        bool clean=true;
        if (acb && loaded) {
            int result=AcbAPI_setState(acb,APPSTATE_FOREGROUND,PLAYSTATE_UNLOADED,&task);
            fprintf(stderr,"[hevc-probe] cleanup acb-unloaded-raw=%d\n",result);
        }
        if (api) {
            if (loaded) {
                bool ok=api->Unload();
                fprintf(stderr,"[hevc-probe] cleanup unload=%d\n",ok);
                clean=clean && ok;
            }
            delete api; api=nullptr;
        }
        if (acb) {
            if (acb_initialized) {
                int result=AcbAPI_finalize(acb);
                fprintf(stderr,"[hevc-probe] cleanup acb-finalize-raw=%d\n",result);
                clean=clean && result!=0;
            }
            AcbAPI_destroy(acb); fprintf(stderr,"[hevc-probe] cleanup acb-destroy-called\n"); acb=0;
        }
        loaded=false;
        fprintf(stderr,"[hevc-probe] cleanup-end ok=%d mono_ms=%lld\n",clean,(long long)now_ms());
        return clean;
    }
};
}

int main(int argc,char **argv) {
    // Record a safe startup category even when Application Manager adds fields
    // to the JSON launch object or starts the app without an explicit mode.
    const std::string dir=self_dir();
    std::string log=dir.empty()?"":dir+"/hevc-probe.log";
    FILE *log_file=log.empty()?nullptr:fopen(log.c_str(),"a");
    const int app_log_error=log_file?0:errno;
    if (!log_file) {
        log="/tmp/uxplay-hevc-probe.log";
        log_file=fopen(log.c_str(),"a");
    }
    if (!log_file) {
        fprintf(stderr,"[hevc-probe] startup log-unavailable app_errno=%d fallback_errno=%d\n",app_log_error,errno);
        return 2;
    }
    struct stat log_stat{};
    if (fstat(fileno(log_file),&log_stat)==0 && log_stat.st_size>1024*1024)
        ftruncate(fileno(log_file),0);
    const hevc_probe::LaunchDecision launch=argc==2
        ? hevc_probe::parse_launch_argument(argv[1]) : hevc_probe::LaunchDecision{};
    const bool hd=launch.mode==hevc_probe::LaunchMode::Hd1080;
    const bool uhd=launch.mode==hevc_probe::LaunchMode::Uhd;
    fprintf(log_file,"[hevc-probe] startup argc=%d arg1_bytes=%zu launch=%s mode=%s log=%s app_log_errno=%d\n",
            argc,launch.argument_bytes,hevc_probe::launch_status_name(launch.status),
            hd?"1080p":uhd?"uhd":"idle",log=="/tmp/uxplay-hevc-probe.log"?"tmp":"app",app_log_error);
    fflush(log_file);
    if (!hd && !uhd) {
        fprintf(log_file,"[hevc-probe] idle: explicit mode required\n");
        fclose(log_file);
        fprintf(stderr,"HEVC probe idle: launch with explicit mode; see docs/MILESTONE5C.md\n");
        return 0;
    }
    std::string state_dir=dir;
    if (log=="/tmp/uxplay-hevc-probe.log") {
        state_dir="/tmp/uxplay-hevc-probe-state";
        if (mkdir(state_dir.c_str(),0700)!=0 && errno!=EEXIST) {
            fprintf(log_file,"[hevc-probe] startup-state-directory-failed errno=%d\n",errno);
            fclose(log_file); return 2;
        }
        struct stat state_info{};
        if (lstat(state_dir.c_str(),&state_info)!=0 || !S_ISDIR(state_info.st_mode) ||
            state_info.st_uid!=geteuid() || (state_info.st_mode&077)!=0) {
            fprintf(log_file,"[hevc-probe] startup-state-directory-unsafe\n");
            fclose(log_file); return 2;
        }
    }
    if (dir.empty() || dup2(fileno(log_file),STDERR_FILENO)<0) {
        fprintf(log_file,"[hevc-probe] startup executable-directory-or-redirect-failed\n");
        fclose(log_file); return 2;
    }
    fclose(log_file);
    setvbuf(stderr,nullptr,_IOLBF,0);
    int null_fd=open("/dev/null",O_WRONLY);
    if (null_fd>=0) { dup2(null_fd,STDOUT_FILENO); close(null_fd); }
    const int lock_fd=open((state_dir+"/hevc-probe.lock").c_str(),O_CREAT|O_RDWR|O_NOFOLLOW,0600);
    if (lock_fd<0 || flock(lock_fd,LOCK_EX|LOCK_NB)!=0) {
        fprintf(stderr,"[hevc-probe] startup-exclusive-lock-failed errno=%d\n",errno);
        if (lock_fd>=0) close(lock_fd);
        return 2;
    }
    fprintf(stderr,"[hevc-probe] lifecycle pid=%ld mono_ms=%lld mode=%s lock=held\n",
            long(getpid()),(long long)now_ms(),uhd?"uhd":"1080p");
    struct sigaction action{};
    action.sa_sigaction=stop_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags=SA_SIGINFO;
    if (sigaction(SIGTERM,&action,nullptr)!=0 || sigaction(SIGINT,&action,nullptr)!=0) {
        fprintf(stderr,"[hevc-probe] signal-handler-install-failed errno=%d\n",errno);
        close(lock_fd); return 2;
    }
    signal(SIGALRM,hard_deadline);
    alarm(45);
    hevc_probe::GateIdentity gate_identity;
    std::string gate_reason;
    const bool gate_identity_ok=hevc_probe::collect_gate_identity(dir,gate_identity,gate_reason);
    fprintf(stderr,"[hevc-probe] gate-identity ready=%d reason=%s\n",gate_identity_ok,gate_reason.c_str());
    if (hd && !hevc_probe::clear_uhd_gate(state_dir,gate_reason)) {
        fprintf(stderr,"[hevc-probe] uhd-gate=blocked reason=%s\n",gate_reason.c_str());
        fprintf(stderr,"[hevc-probe] termination reason=gate-clear-failed exit=2\n");
        alarm(0); close(lock_fd); return 2;
    }
    if (uhd) {
        int source_pid=0;
        if (!gate_identity_ok || !hevc_probe::consume_uhd_gate(state_dir,gate_identity,now_ms(),source_pid,gate_reason)) {
            fprintf(stderr,"[hevc-probe] uhd-gate=blocked reason=%s\n",gate_reason.c_str());
            fprintf(stderr,"[hevc-probe] termination reason=gate-blocked exit=2\n");
            alarm(0); close(lock_fd); return 2;
        }
        fprintf(stderr,"[hevc-probe] uhd-gate=passed source_pid=%d one_use=1\n",source_pid);
    }
    auto run_case=[&](int width,int height,const char *phase)->int {
    phase_number=width==3840?2:1;
    fprintf(stderr,"[hevc-probe] phase-begin name=%s pid=%ld mono_ms=%lld bounds=%dx%d rss_kib=%d\n",
            phase,long(getpid()),(long long)now_ms(),width,height,rss_kib());
    const std::string file=dir+(width==3840?"/sample-uhd.hevc":"/sample-1080p.hevc");
    std::ifstream in(file.c_str(),std::ios::binary|std::ios::ate);
    if (!in || in.tellg()<16 || in.tellg()>std::streamoff(hevc_probe::kMaxFile)) {
        fprintf(stderr,"[hevc-probe] sample-size-or-open-failed\n"); return 2;
    }
    size_t size=size_t(in.tellg()); std::vector<uint8_t> bytes(size); in.seekg(0);
    if (!in.read(reinterpret_cast<char *>(bytes.data()),size)) {
        fprintf(stderr,"[hevc-probe] sample-read-failed\n"); return 2;
    }
    hevc_probe::Sample sample; std::string error;
    if (!hevc_probe::inspect(bytes.data(),bytes.size(),width,height,sample,error)) {
        fprintf(stderr,"[hevc-probe] sample-rejected reason=%s\n",error.c_str()); return 2;
    }
    fprintf(stderr,"[hevc-probe] sample-valid codec=HEVC profile=Main bitdepth=%d sps=%dx%d frames=%zu bytes=%zu rss_kib=%d\n",
            sample.bit_depth,sample.width,sample.height,sample.units.size(),bytes.size(),rss_kib());
    setenv("EGL_PLATFORM","wayland",0); setenv("XDG_RUNTIME_DIR","/tmp/xdg",0);
    if (SDL_Init(SDL_INIT_VIDEO)!=0) { fprintf(stderr,"[hevc-probe] sdl-init-failed\n"); return 2; }
    SDL_DisplayMode mode{}; SDL_Window *window=nullptr; SDL_Renderer *renderer=nullptr;
    int rc=1;
    do {
        if (SDL_GetCurrentDisplayMode(0,&mode)!=0) {
            fprintf(stderr,"[hevc-probe] sdl-display-mode-failed\n"); break;
        }
        window=SDL_CreateWindow("HEVC Hardware Probe",0,0,mode.w,mode.h,SDL_WINDOW_FULLSCREEN);
        if (!window) { fprintf(stderr,"[hevc-probe] sdl-window-failed\n"); break; }
        renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
        if (!renderer) { fprintf(stderr,"[hevc-probe] sdl-renderer-failed\n"); break; }
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer,0,0,0,0); SDL_RenderClear(renderer); SDL_RenderPresent(renderer);
        fprintf(stderr,"[hevc-probe] sdl-canvas=%dx%d (not output-plane resolution)\n",mode.w,mode.h);
        const int64_t probe_start_ms=now_ms();
        Player player;
        if (!player.open(width,height,mode.w,mode.h)) {
            fprintf(stderr,"[hevc-probe] media-open-failed\n"); break;
        }
        const int64_t load_deadline=now_ms()+8000;
        int64_t play_deadline=0,next_play_ms=0;
        unsigned play_attempts=0;
        bool play_request=false;
        auto progress=[&]() {
            SDL_PumpEvents(); player.tick();
            if (player.events.loaded && !play_deadline) play_deadline=now_ms()+5000;
            if (player.events.loaded && !player.events.failed && !play_request &&
                play_attempts<10 && now_ms()>=next_play_ms) {
                ++play_attempts;
                const int64_t t=now_ms(); play_request=player.api->Play();
                fprintf(stderr,"[hevc-probe] play-return attempt=%u accepted=%d ms=%lld\n",
                        play_attempts,play_request,(long long)(now_ms()-t));
                next_play_ms=now_ms()+100;
            }
        };
        const unsigned initial_frames=player.events.frame_ready.load();
        unsigned accepted=0,retries=0; int64_t start=now_ms();
        const char *feed_stop="complete";
        size_t next_frame=0;
        for (size_t i=0;i<sample.units.size();++i) {
            next_frame=i;
            if (stop_requested) { feed_stop="signal"; break; }
            if (player.events.failed) { feed_stop="state-failed"; break; }
            while (!stop_requested && !player.events.failed &&
                   now_ms()<start+int64_t(i)*1000/30) { progress(); SDL_Delay(2); }
            progress();
            if (stop_requested) { feed_stop="signal"; break; }
            if (player.events.failed) { feed_stop="state-failed"; break; }
            if (!player.events.loaded && now_ms()>=load_deadline) {
                feed_stop="load-timeout"; break;
            }
            const hevc_probe::AccessUnit& au=sample.units[i];
            char payload[192]; snprintf(payload,sizeof(payload),
                "{\"bufferAddr\":\"%p\",\"bufferSize\":%zu,\"pts\":%lld,\"esData\":1}",
                bytes.data()+au.offset,au.size,(long long)(int64_t(i)*1000000000/30));
            fprintf(stderr,"[hevc-probe] feed-begin frame=%zu elapsed_ms=%lld load=%d play=%d sink=%d acb_play=%d bytes=%zu\n",
                    i,(long long)(now_ms()-probe_start_ms),player.events.loaded.load(),
                    player.events.playing.load(),player.sink,player.acb_playing,au.size);
            if (width==3840 && i==0)
                fprintf(stderr,"[hevc-probe] uhd-first-feed mono_ms=%lld bytes=%zu\n",(long long)now_ms(),au.size);
            bool fed=false;
            bool backpressure=false;
            for (unsigned attempt=0;attempt<10 && !stop_requested && !player.events.failed;++attempt) {
                int64_t t=now_ms(); std::string reply=player.api->Feed(payload);
                int64_t elapsed=now_ms()-t;
                const hevc_probe::FeedReply kind=hevc_probe::classify_feed_reply(reply);
                if (kind==hevc_probe::FeedReply::Accepted) { ++accepted; fed=true;
                    fprintf(stderr,"[hevc-probe] feed frame=%zu accepted=1 bytes=%zu ms=%lld\n",i,au.size,(long long)elapsed); break; }
                if (kind==hevc_probe::FeedReply::Backpressure) {
                    backpressure=true; ++retries;
                    fprintf(stderr,"[hevc-probe] feed-retry frame=%zu attempt=%u ms=%lld reply_bytes=%zu\n",
                            i,attempt+1,(long long)elapsed,reply.size());
                    progress(); SDL_Delay(20); continue;
                }
                fprintf(stderr,"[hevc-probe] feed frame=%zu rejected=1 ms=%lld reply_bytes=%zu\n",
                        i,(long long)elapsed,reply.size());
                feed_stop="feed-rejected"; break;
            }
            if (!fed) {
                if (stop_requested) feed_stop="signal";
                else if (player.events.failed) feed_stop="state-failed";
                else if (backpressure && strcmp(feed_stop,"feed-rejected")!=0) feed_stop="retry-exhausted";
                fprintf(stderr,"[hevc-probe] feed-failed frame=%zu reason=%s retries=%u\n",i,feed_stop,retries);
                break;
            }
        }
        if (accepted==sample.units.size()) next_frame=sample.units.size();
        if (stop_requested && strcmp(feed_stop,"complete")==0) feed_stop="signal";
        if (player.events.failed && strcmp(feed_stop,"complete")==0) feed_stop="state-failed";
        if (accepted==sample.units.size() && !stop_requested && !player.events.failed) {
            while (!player.events.playing && !player.events.failed && !stop_requested) {
                progress();
                if ((!player.events.loaded && now_ms()>=load_deadline) ||
                    (player.events.loaded && now_ms()>=play_deadline)) {
                    fprintf(stderr,"[hevc-probe] wait-timeout stage=%s elapsed_ms=%lld play_attempts=%u\n",
                            player.events.loaded?"play":"load",(long long)(now_ms()-probe_start_ms),play_attempts);
                    break;
                }
                SDL_Delay(10);
            }
        }
        fprintf(stderr,"[hevc-probe] feed-stop reason=%s next_frame=%zu accepted=%u/%zu elapsed_ms=%lld "
                "signal=%d failed=%d failure_source=%d load=%d play=%d acb_sink_attempted=%d "
                "acb_sink_raw=%d acb_loaded_raw=%d "
                "load_event_ms=%lld play_event_ms=%lld frame_event_ms=%lld play_attempts=%u\n",
                feed_stop,next_frame,accepted,sample.units.size(),(long long)(now_ms()-probe_start_ms),
                int(stop_signal_number),player.events.failed.load(),player.events.failure_source.load(),
                player.events.loaded.load(),player.events.playing.load(),player.sink,
                player.acb_sink_raw,player.acb_loaded_state_raw,
                player.events.load_event_ms.load()? (long long)(player.events.load_event_ms.load()-probe_start_ms):-1LL,
                player.events.playing_event_ms.load()? (long long)(player.events.playing_event_ms.load()-probe_start_ms):-1LL,
                player.events.first_frame_event_ms.load()? (long long)(player.events.first_frame_event_ms.load()-probe_start_ms):-1LL,
                play_attempts);
        bool eos_accepted=false;
        if (hevc_probe::send_eos_after_feed(accepted,sample.units.size(),player.events.failed,stop_requested)) {
            eos_accepted=player.api->pushEOS();
            fprintf(stderr,"[hevc-probe] eos-accepted=%d\n",eos_accepted);
        } else fprintf(stderr,"[hevc-probe] eos-skipped reason=incomplete-or-failed-feed\n");
        const int64_t drain_start=now_ms();
        int64_t deadline=drain_start+(accepted==sample.units.size()?5000:500);
        fprintf(stderr,"[hevc-probe] drain-begin limit_ms=%d complete_feed=%d\n",
                accepted==sample.units.size()?5000:500,accepted==sample.units.size());
        while (!stop_requested && !player.events.failed && now_ms()<deadline) {
            progress(); SDL_Delay(20);
        }
        fprintf(stderr,"[hevc-probe] drain-end reason=%s elapsed_ms=%lld\n",
                stop_requested?"signal":player.events.failed?"state-failed":"deadline",
                (long long)(now_ms()-drain_start));
        fprintf(stderr,"[hevc-probe] result input_accepted=%u/%zu retries=%u frame_ready_events=%u "
            "video_info=%dx%d acb_window=%dx%d errors=%u need_data=%u enough_data=%u buffer_low=%u rss_kib=%d\n",accepted,sample.units.size(),retries,
            player.events.frame_ready.load()-initial_frames,player.events.video_width.load(),player.events.video_height.load(),
            mode.w,mode.h,player.events.errors.load(),player.events.need_data.load(),
            player.events.enough_data.load(),player.events.buffer_low.load(),rss_kib());
        fprintf(stderr,"[hevc-probe] evidence: Feed acceptance is input only; FRAMEREADY is a firmware callback; "
            "ACB window is requested geometry; native output-plane resolution remains unmeasured\n");
        const bool control_path=!stop_requested && !player.events.failed && player.events.loaded &&
            player.events.playing && player.acb_playing && eos_accepted &&
            accepted==sample.units.size() &&
            player.events.video_width==width && player.events.video_height==height;
        internal_shutdown_requested=1;
        const bool cleanup_ok=player.close();
        rc=control_path && cleanup_ok?0:1;
        fprintf(stderr,"[hevc-probe] control-path=%s cleanup_ok=%d frame_ready_observed=%d "
                "decoder-path=UNPROVEN (neither callback nor video-info proves display)\n",
                control_path?"COMPLETE":"INCOMPLETE",cleanup_ok,
                player.events.frame_ready.load()>initial_frames);
        if (width==1920 && rc==0 && gate_identity_ok && !stop_requested) {
            const bool written=hevc_probe::write_uhd_gate(state_dir,gate_identity,now_ms(),int(getpid()),gate_reason);
            fprintf(stderr,"[hevc-probe] uhd-gate-token written=%d reason=%s\n",written,gate_reason.c_str());
            if (!written) rc=1;
        }
    } while(false);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    phase_number=4;
    fprintf(stderr,"[hevc-probe] phase-end name=%s control_path=%s rc=%d rss_kib=%d\n",
            phase,rc==0?"complete":"incomplete",rc,rss_kib());
    return rc;
    };
    int rc=run_case(uhd?3840:1920,uhd?2160:1080,uhd?"experimental-uhd":"1080p");
    if (stop_requested) {
        if (hd) {
            std::string revoke_reason;
            const bool revoked=hevc_probe::clear_uhd_gate(state_dir,revoke_reason);
            fprintf(stderr,"[hevc-probe] uhd-gate-token revoked=%d reason=%s\n",revoked,revoke_reason.c_str());
        }
        const int sender=int(stop_sender_pid);
        fprintf(stderr,"[hevc-probe] termination reason=signal number=%d sender_pid=%d sender=%s "
                "si_code=%d phase=%d signal_mono_sec=%d signal_mono_msec=%d internal_shutdown=%d\n",
                int(stop_signal_number),sender,sender==int(getpid())?"self":sender>0?"other-process":"unknown",
                int(stop_code),int(stop_phase),int(stop_at_sec),int(stop_at_msec),int(internal_shutdown_requested));
        rc=1;
    } else fprintf(stderr,"[hevc-probe] termination reason=%s phase=%d mono_ms=%lld\n",
                   rc==0?"control-path-complete":"control-path-incomplete",int(phase_number),(long long)now_ms());
    fprintf(stderr,"[hevc-probe] exit=%d\n",rc);
    alarm(0); close(lock_fd); return rc;
}
