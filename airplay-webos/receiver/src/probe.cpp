#include "media.h"
#include "shell.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <unistd.h>
static bool read_frame(FILE *f, std::vector<unsigned char>& bytes) {
    uint32_t n = 0;
    if (fread(&n,4,1,f) != 1 || n > 2*1024*1024) return false;
    bytes.resize(n); return fread(bytes.data(),1,n,f) == n;
}
int main() {
    start_log("probe.log");
    const char *id = getenv("APPID"); if (!id) id = "com.github.depius.uxair.probe";
    Shell shell;
    if (!shell.open()) return 1;
    shell.transparent();
    FILE *v = fopen((app_directory()+"/test.avc").c_str(),"rb");
    FILE *a = fopen((app_directory()+"/test.pcm").c_str(),"rb");
    if (!v || !a) { fprintf(stderr,"[probe] Missing test media\n"); return 1; }
    Media media;
    if (!media.open(id,1280,720,true,48000)) return 1;
    std::vector<unsigned char> video, audio(480*4);
    bool hv = read_frame(v,video);
    size_t na = fread(audio.data(),1,audio.size(),a);
    unsigned nv=0, nc=0; int64_t origin=0, deadline=now_ns()+25000000000LL;
    bool ok = true;
    while ((hv || na) && shell.pump() && now_ns()<deadline) {
        media.tick();
        bool is_video = hv && (!na || int64_t(nv)*1000000000/30 <= int64_t(nc)*10000000);
        int64_t pts = is_video ? int64_t(nv)*1000000000/30 : int64_t(nc)*10000000;
        if (origin && now_ns() < origin+pts) { SDL_Delay(2); continue; }
        auto r = media.feed(is_video ? video.data():audio.data(),is_video?video.size():na,pts,is_video);
        if (r == FeedResult::Retry) { SDL_Delay(5); continue; }
        if (r == FeedResult::Error) { ok=false; break; }
        if (!origin) origin=now_ns()-pts;
        if (is_video) { ++nv; hv=read_frame(v,video); }
        else { ++nc; na=fread(audio.data(),1,audio.size(),a); }
    }
    media.eos();
    deadline=now_ns()+5000000000LL;
    while(!media.ended && !media.failed && now_ns()<deadline && shell.pump()) { media.tick(); SDL_Delay(10); }
    fprintf(stderr,"[probe] video=%u audio=%u rendered=%u playing=%d ended=%d failed=%d\n",nv,nc,media.frames.load(),int(media.playing),int(media.ended),int(media.failed));
    ok = ok && nv==450 && nc==1500 && media.playing && !media.failed;
    media.close(); fclose(v); fclose(a);
    fprintf(stderr,"[probe] result=%s\n",ok?"PASS":"FAIL");
    return ok?0:1;
}
