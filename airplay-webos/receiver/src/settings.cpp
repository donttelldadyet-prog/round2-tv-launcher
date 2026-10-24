#include "settings.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

bool supported_mirror_resolution(int width, int height) {
    return (width == 1280 && height == 720) || (width == 1920 && height == 1080) ||
           (width == 3840 && height == 2160);
}
bool supported_mirror_fps(int fps) { return fps == 30 || fps == 60; }
bool set_mirror_resolution(Settings& settings, int width, int height) {
    if (!supported_mirror_resolution(width, height)) return false;
    if(width==3840)settings.codec=MirrorCodec::ExperimentalHevc;
    settings.width = width; settings.height = height; return true;
}
bool set_mirror_fps(Settings& settings, int fps) {
    if (!supported_mirror_fps(fps)) return false;
    settings.max_fps = fps; return true;
}
bool set_mirror_codec(Settings& settings, MirrorCodec codec) {
    if(codec!=MirrorCodec::H264 && codec!=MirrorCodec::ExperimentalHevc) return false;
    settings.codec=codec;
    if(codec==MirrorCodec::H264 && settings.width==3840) {
        // H.264 supports the existing 1080p profile; only UHD requires HEVC.
        settings.width=1920; settings.height=1080;
    }
    return true;
}
bool input_fits_mirror_profile(const Settings& settings, int width, int height) {
    if(width<=0 || height<=0) return false;
    const int long_edge=settings.width>settings.height?settings.width:settings.height;
    const int max_area=settings.width*settings.height;
    return width<=long_edge && height<=long_edge && int64_t(width)*height<=max_area;
}
MirrorLimits mirror_limits(const Settings& settings) {
    Settings safe=settings; validate_settings(safe);
    if (safe.width == 3840) return {2*1024*1024, 16*1024*1024, 2*1024*1024};
    if (safe.width == 1920) return {2*1024*1024, 8*1024*1024, 2*1024*1024};
    return {1024*1024, 4*1024*1024, 1024*1024};
}
VideoWindow fit_video_window(int source_width, int source_height, int canvas_width, int canvas_height) {
    if (source_width<=0 || source_height<=0 || canvas_width<=0 || canvas_height<=0)
        return {0,0,0,0};
    int width=canvas_width, height=canvas_height;
    if (int64_t(source_width)*canvas_height > int64_t(source_height)*canvas_width)
        height=int(int64_t(canvas_width)*source_height/source_width);
    else width=int(int64_t(canvas_height)*source_width/source_height);
    return {(canvas_width-width)/2,(canvas_height-height)/2,width,height};
}
ProfileSwitchResult switch_mirror_profile(Settings& current, int width, int height,
    const std::function<bool(const Settings&)>& activate) {
    Settings proposed=current;
    if (!set_mirror_resolution(proposed,width,height) ||
        (proposed.width==current.width && proposed.height==current.height))
        return ProfileSwitchResult::Unchanged;
    if (activate(proposed)) { current=proposed; return ProfileSwitchResult::Applied; }
    return activate(current) ? ProfileSwitchResult::Recovered : ProfileSwitchResult::Unavailable;
}
void validate_settings(Settings& settings) {
    if(settings.codec!=MirrorCodec::H264 && settings.codec!=MirrorCodec::ExperimentalHevc)
        settings.codec=MirrorCodec::H264;
    if (!supported_mirror_resolution(settings.width, settings.height) ||
        (settings.width==3840 && settings.codec!=MirrorCodec::ExperimentalHevc)) {
        settings.width = 1280; settings.height = 720;
    }
    if (!supported_mirror_fps(settings.max_fps)) settings.max_fps = 30;
    if (settings.diagnostics != DiagnosticsMode::Off && settings.diagnostics != DiagnosticsMode::Compact && settings.diagnostics != DiagnosticsMode::Detailed)
        settings.diagnostics = DiagnosticsMode::Off;
}
const char *diagnostics_mode_name(DiagnosticsMode mode) {
    switch(mode) {
    case DiagnosticsMode::Compact: return "Compact";
    case DiagnosticsMode::Detailed: return "Detailed";
    default: return "Off";
    }
}
bool load_settings(const std::string& path, Settings& settings) {
    std::ifstream in(path.c_str());
    std::string line;
    if (!std::getline(in, line) || line != "UXPLAY_SETTINGS_V1") return false;
    // Early UxAir settings files may omit codec and FPS; interpret those with
    // the historical safe profile instead of applying new factory defaults.
    Settings parsed;parsed.width=1280;parsed.height=720;parsed.codec=MirrorCodec::H264;parsed.max_fps=30;
    bool have_setting=false;
    unsigned lines = 0;
    while (std::getline(in, line) && ++lines <= 16) {
        if (line.size() > 80) return false;
        if (line == "resolution=720p") { parsed.width=1280; parsed.height=720; have_setting=true; }
        else if (line == "resolution=1080p") { parsed.width=1920; parsed.height=1080; have_setting=true; }
        else if (line == "resolution=4k") { parsed.width=3840; parsed.height=2160; have_setting=true; }
        else if (line == "codec=hevc-experimental") { parsed.codec=MirrorCodec::ExperimentalHevc; have_setting=true; }
        else if (line == "codec=h264") { parsed.codec=MirrorCodec::H264; have_setting=true; }
        else if (line == "fps=30") { parsed.max_fps=30; have_setting=true; }
        else if (line == "fps=60") { parsed.max_fps=60; have_setting=true; }
        else if (line == "diagnostics=off") { parsed.diagnostics=DiagnosticsMode::Off; have_setting=true; }
        else if (line == "diagnostics=compact") { parsed.diagnostics=DiagnosticsMode::Compact; have_setting=true; }
        else if (line == "diagnostics=detailed") { parsed.diagnostics=DiagnosticsMode::Detailed; have_setting=true; }
    }
    if (lines > 16 || !have_setting) return false;
    validate_settings(parsed);
    settings=parsed;
    return true;
}
bool save_settings(const std::string& path, const Settings& settings) {
    Settings safe=settings; validate_settings(safe);
    const std::string temp=path+".tmp";
    FILE *file=fopen(temp.c_str(), "w");
    if (!file) return false;
    chmod(temp.c_str(), 0600);
    bool ok=fprintf(file,"UXPLAY_SETTINGS_V1\nresolution=%s\ncodec=%s\nfps=%d\ndiagnostics=%s\n",
        safe.width==3840?"4k":safe.width==1920?"1080p":"720p",
        safe.codec==MirrorCodec::ExperimentalHevc?"hevc-experimental":"h264",
        safe.max_fps,
        safe.diagnostics==DiagnosticsMode::Compact?"compact":safe.diagnostics==DiagnosticsMode::Detailed?"detailed":"off")>0;
    if (fflush(file)!=0 || fsync(fileno(file))!=0) ok=false;
    if (fclose(file)!=0) ok=false;
    if (ok && rename(temp.c_str(),path.c_str())==0) return true;
    remove(temp.c_str()); return false;
}
