#pragma once
#include <SDL.h>
#include "input.h"
#include "ui_navigation.h"
#include "usage_stats.h"
#include <cstdint>
#include <string>
#include <vector>
#include <SDL_ttf.h>   // Round2: the font type is named differently across SDL_ttf versions
struct UiRow {
    std::string label, value, note;
    bool enabled;
    UiRow(const std::string& l,const std::string& v,const std::string& n,bool e=true)
        : label(l),value(v),note(n),enabled(e) {}
};
using UiHistoryBar = UsageBar;
struct UiHistoryData {
    std::string daily_title,monthly_title;
    std::string summary,total_time,average,mirror_time,music_time,storage;
    std::vector<UiHistoryBar> daily_bars,monthly_bars;
    unsigned mirror_percent=0,audio_percent=0;
    size_t session_count=0; bool empty=true;
};
struct UiDialog {
    std::string title, description;
    std::vector<std::string> choices;
    int selected = 0;
    int current = -1;
};
enum class UiHitKind { None, Category, Row, DialogChoice, Cancel };
struct UiHit {
    UiHitKind kind; int index;
    UiHit(UiHitKind k=UiHitKind::None,int i=-1):kind(k),index(i) {}
};
class Shell {
public:
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    bool open();
    bool pump();
    Action action();
    bool pointer_select(int& x, int& y) const;
    bool take_pointer_motion();
    UiHit hit_test(int x, int y, int row_count, const UiDialog *dialog = nullptr, int selected = 0, UiPage page = UiPage::Mirroring) const;
    // Round2 look (round2_ui.cpp): fonts from the app's fonts/ folder, the room's colour as accent
    bool load_fonts(const std::string& dir);
    void close_fonts();
    void set_accent(SDL_Color c) { accent_=c; }
    void screen(const std::string& status, const std::string& pin, int audio_delay_ms=0);
    void page(UiPage page, const std::string& subtitle, const std::vector<UiRow>& rows, int selected,
              const std::string& footer, const UiDialog *dialog = nullptr,
              const std::string& pin = std::string(), bool donation_open = false, const UiHistoryData *history = nullptr);
    void overlay(const std::vector<std::string>& lines, const std::string& pin = std::string());
    void transparent();
    ~Shell();
private:
    InputMapper input_{SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN};
    bool pointer_select_ = false;
    int pointer_x_ = 0, pointer_y_ = 0;
    bool pointer_valid_=false,pointer_moved_=false;
    SDL_Color accent_{122,91,224,255};
    TTF_Font *pix_=nullptr, *pix_small_=nullptr, *title_=nullptr, *body_=nullptr, *body_bold_=nullptr, *small_=nullptr, *pin_=nullptr;
    int font_scale_h_=0;
    void ensure_fonts();
    std::string font_dir_;
    void background(int w,int h);
    void text(TTF_Font*,const std::string&,int x,int y,SDL_Color,int max_w=0,int align=0);
    int text_w(TTF_Font*,const std::string&);
    void draw_rows(const std::vector<UiRow>&,int selected,SDL_Rect area,int row_h,bool arrows_on_first_delay);
    void draw_dialog(const UiDialog&,int w,int h);
    void draw_pairing(const std::string& pin,int w,int h);
    void footer(const std::string&,int w,int h);
};
int64_t now_ns();
std::string app_directory();
std::string writable_directory();
const std::string& airplay_name();            // Round2: the name iPhones see (per room)
void set_airplay_name(const std::string& name);   // Round2: where state and logs can be written
void start_log(const char *name);
void rotate_log();
