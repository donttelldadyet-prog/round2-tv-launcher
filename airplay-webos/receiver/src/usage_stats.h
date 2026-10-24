#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
enum class SessionKind { Mirror = 1, Audio = 2, Direct = 3 };
struct SessionSummary {
    uint64_t started_utc=0,duration_ms=0;
    SessionKind kind=SessionKind::Mirror;
    unsigned width=0,height=0,codec=0;
    uint64_t incoming_frames=0,feed_frames=0,packet_drops=0,video_frame_drops=0,decoder_errors=0,session_errors=0;
};
struct DailyUsage {
    uint32_t utc_day=0;
    uint64_t mirror_ms=0,audio_ms=0;
    uint32_t mirror_sessions=0,audio_sessions=0;
};
struct UsageStats {
    static const size_t MaxRecentSessions=100,MaxDailyRecords=365,MaxSerializedBytes=65536;
    uint64_t playback_ms=0,successful_sessions=0,mirror_sessions=0,audio_sessions=0,direct_sessions=0;
    uint64_t incoming_frames=0,feed_frames=0,packet_drops=0,video_frame_drops=0,decoder_errors=0,session_errors=0;
    std::deque<SessionSummary> recent;
    std::vector<DailyUsage> daily;
    uint32_t daily_from_day=0;
    bool storage_corrupt=false;
    void record(const SessionSummary& session);
    void reset();
};
enum class UsagePeriod { Days7=0,Days30=1,Months12=2,ThisMonth=3,ThisYear=4 };
struct UsageBar { std::string label; uint64_t mirror_ms=0,audio_ms=0; bool available=false,partial=false,future=false,current=false; };
struct UsageSummary {
    std::string title;
    uint64_t mirror_ms=0,audio_ms=0,mirror_sessions=0,audio_sessions=0;
    std::vector<UsageBar> bars;
};
bool load_usage_stats(const std::string& path,UsageStats& stats);
bool save_usage_stats(const std::string& path,const UsageStats& stats);
size_t usage_storage_bytes(const std::string& path);
UsageSummary summarize_usage(const UsageStats& stats,UsagePeriod period,uint32_t today_utc_day);
uint32_t usage_utc_day(uint64_t utc_seconds);
const char *usage_period_name(UsagePeriod period);
std::string usage_axis_label(uint64_t milliseconds,uint64_t unit_ms=0);
uint64_t usage_chart_axis(uint64_t maximum);
struct UsageBarHeights { int mirror=0,audio=0; };
UsageBarHeights usage_bar_heights(uint64_t mirror,uint64_t audio,uint64_t axis,int plot_height);
unsigned usage_mirror_percent(uint64_t mirror_ms,uint64_t audio_ms);
const char *session_kind_name(SessionKind kind);
