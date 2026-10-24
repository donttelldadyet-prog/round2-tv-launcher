#include "ui_navigation.h"
#include <algorithm>

bool session_ui_redraw_due(bool opened,bool visible,int64_t last_render_ns,int64_t now_ns) {
    return opened&&visible&&(last_render_ns==0||now_ns-last_render_ns>=1000000000LL);
}
int visible_row_first(int selected,int count,int visible) {
    if(count<=visible||visible<=0)return 0;
    return std::max(0,std::min(selected-visible/2,count-visible));
}
HistoryUiLayout history_ui_layout(int width,int height) {
    const int panel=width/4+32,content=width-panel-48,top=height/5;
    const int chart_y=top+176,chart_h=230,gap=20;
    const int daily=(content-gap)*65/100;
    const int distribution_y=chart_y+chart_h+48;
    const int distribution_bar_y=distribution_y+27;
    const int heading_y=distribution_bar_y+22+25;
    const int row_h=std::max(28,std::min(36,height/30));
    const int list_y=heading_y+66;
    return {panel,content,top,chart_y,chart_h,daily,gap,distribution_y,
            distribution_bar_y,heading_y,list_y,row_h,panel-5,height-74,180,42};
}
static bool in_rect(int x,int y,int left,int top,int width,int height) {
    return x>=left&&x<left+width&&y>=top&&y<top+height;
}
bool history_reset_hit(const HistoryUiLayout& l,int x,int y) {
    return in_rect(x,y,l.reset_x,l.reset_y-4,l.reset_w,l.reset_h+4);
}
int history_session_hit(const HistoryUiLayout& l,int x,int y,int count,int selected) {
    if(x<l.panel_x-5||x>=l.panel_x+l.content_width||count<=0||y<l.list_y)return -1;
    const int visible=std::min(5,count),first=visible_row_first(std::max(0,selected),count,visible);
    const int row=(y-l.list_y)/l.row_h;
    return row<visible&&first+row<count?first+row:-1;
}
void UiNavigation::category(int index) {
    if(dialog!=UiDialogKind::None||index<0||index>=5)return;
    page=static_cast<UiPage>(index);focus=0;
}
void UiNavigation::move_category(int delta) {
    if(dialog!=UiDialogKind::None)return;
    int current=static_cast<int>(page);
    category((current+delta+5)%5);
}
void UiNavigation::move_row(int delta,int count) {
    if(dialog!=UiDialogKind::None||count<=0)return;
    focus=(focus+delta+count)%count;
}
void UiNavigation::begin(UiDialogKind kind,int initial_choice) {
    if(dialog!=UiDialogKind::None||kind==UiDialogKind::None)return;
    dialog=kind;choice=initial_choice;
}
void UiNavigation::move_choice(int delta,int count) {
    if(dialog==UiDialogKind::None||count<=0)return;
    choice=(choice+delta+count)%count;
}
int UiNavigation::confirm() {
    if(dialog==UiDialogKind::None)return -1;
    int selected=choice;dialog=UiDialogKind::None;choice=0;return selected;
}
bool UiNavigation::back() {
    if(dialog!=UiDialogKind::None){dialog=UiDialogKind::None;choice=0;return true;}
    if(page!=UiPage::Mirroring){page=UiPage::Mirroring;focus=0;return true;}
    return false;
}
