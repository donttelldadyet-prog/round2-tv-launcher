#pragma once
#include <cstdint>

enum class UiPage { Mirroring, Media, History, Diagnostics, System };
enum class UiDialogKind { None, Resolution, Codec, FrameRate, Overlay, AudioDelay, ResetHistory };

struct UiNavigation {
    UiPage page = UiPage::Mirroring;
    UiDialogKind dialog = UiDialogKind::None;
    int focus = 0;
    int choice = 0;

    void category(int index);
    void move_category(int delta);
    void move_row(int delta, int count);
    void begin(UiDialogKind kind, int initial_choice);
    void move_choice(int delta, int count);
    int confirm();
    bool back();
};

int visible_row_first(int selected, int count, int visible);
struct HistoryUiLayout {
    int panel_x,content_width,card_top,chart_y,chart_h,daily_width,chart_gap;
    int distribution_y,distribution_bar_y,heading_y,list_y,row_h,reset_x,reset_y,reset_w,reset_h;
};
HistoryUiLayout history_ui_layout(int width,int height);
bool history_reset_hit(const HistoryUiLayout&,int x,int y);
int history_session_hit(const HistoryUiLayout&,int x,int y,int count,int selected);
bool session_ui_redraw_due(bool opened, bool visible, int64_t last_render_ns, int64_t now_ns);
