// Round2: the receiver's screens in the Round2 game-menu look (Chakra Petch + Press Start 2P,
// near-black background, the room's colour as accent). Replaces UxAir's page/overlay/screen
// drawing; navigation and settings logic stay in main.cpp.
//
// Home (UiPage::Mirroring): how to connect on the left, quick settings on the right.
// More settings (UiPage::System): the rest, one list. Everything scales with the screen height.
#include "shell.h"
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
const SDL_Color kInk{246,243,252,255}, kMute{176,168,196,255}, kDim{120,112,140,255};
const SDL_Color kVoid{6,5,10,255}, kPanel{20,16,32,235}, kRowOn{36,28,58,255};
struct Layout {
    int w,h; double s;
    SDL_Rect card;      // quick settings / settings list
    int row_h, rows_top, visible;
};
Layout layout_for(int w,int h,UiPage page,int row_count) {
    Layout l; l.w=w; l.h=h; l.s=h/1080.0;
    const double s=l.s;
    l.row_h=int(84*s);
    if(page==UiPage::System) l.card={int(w*0.08),int(h*0.30),int(w*0.84),0};
    else l.card={int(w*0.58),int(h*0.26),int(w*0.35),0};
    l.rows_top=l.card.y+int(70*s);
    const int max_bottom=int(h*0.86);
    l.visible=std::max(1,std::min(row_count,(max_bottom-l.rows_top)/l.row_h));
    l.card.h=(l.rows_top-l.card.y)+l.visible*l.row_h+int(24*s);
    return l;
}
int first_visible(int selected,int count,int visible) {
    if(count<=visible) return 0;
    return std::max(0,std::min(selected-visible/2,count-visible));
}
void fill(SDL_Renderer *r,SDL_Rect rc,SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r,c.a<255?SDL_BLENDMODE_BLEND:SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a); SDL_RenderFillRect(r,&rc);
}
// the menu's slanted tag shape: a parallelogram
void slant(SDL_Renderer *r,SDL_Rect rc,SDL_Color c,int cut) {
    SDL_SetRenderDrawBlendMode(r,c.a<255?SDL_BLENDMODE_BLEND:SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);
    for(int y=0;y<rc.h;++y) {
        int off=cut-(cut*y)/std::max(1,rc.h);
        SDL_RenderDrawLine(r,rc.x+off,rc.y+y,rc.x+rc.w-1-(cut-off),rc.y+y);
    }
}
}

bool Shell::load_fonts(const std::string& dir) { font_dir_=dir; font_scale_h_=0; ensure_fonts(); return pix_&&title_&&body_&&small_&&pin_; }
void Shell::close_fonts() {
    for(TTF_Font **f:{&pix_,&pix_small_,&title_,&body_,&body_bold_,&small_,&pin_}) if(*f){TTF_CloseFont(*f);*f=nullptr;}
}
void Shell::ensure_fonts() {
    int w=1920,h=1080; if(renderer) SDL_GetRendererOutputSize(renderer,&w,&h);
    if(h==font_scale_h_ && pix_) return;
    close_fonts(); font_scale_h_=h;
    const double s=h/1080.0;
    auto open=[&](const char *file,double px){ return TTF_OpenFont((font_dir_+"/"+file).c_str(),std::max(8,int(px*s))); };
    pix_=open("PressStart2P-Regular.ttf",17);
    pix_small_=open("PressStart2P-Regular.ttf",12);
    title_=open("ChakraPetch-Bold.ttf",92);
    body_=open("ChakraPetch-Regular.ttf",33);
    body_bold_=open("ChakraPetch-SemiBold.ttf",33);
    small_=open("ChakraPetch-Regular.ttf",25);
    pin_=open("PressStart2P-Regular.ttf",92);
}
int Shell::text_w(TTF_Font *f,const std::string& s) { int w=0; if(f&&!s.empty()) TTF_SizeUTF8(f,s.c_str(),&w,nullptr); return w; }
// align: 0 left, 1 centre, 2 right (x is then the right edge)
void Shell::text(TTF_Font *f,const std::string& str,int x,int y,SDL_Color c,int max_w,int align) {
    if(!f||str.empty()) return;
    std::string s=str;
    if(max_w>0) while(s.size()>1 && text_w(f,s)>max_w) { s.pop_back(); while(!s.empty() && (s.back()&0xC0)==0x80) s.pop_back(); if(text_w(f,s+"…")<=max_w){s+="…";break;} }
    SDL_Surface *surf=TTF_RenderUTF8_Blended(f,s.c_str(),c);
    if(!surf) return;
    SDL_Texture *t=SDL_CreateTextureFromSurface(renderer,surf);
    SDL_Rect dst{align==1?x-surf->w/2:align==2?x-surf->w:x,y,surf->w,surf->h};
    SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer,t,nullptr,&dst);
    SDL_DestroyTexture(t); SDL_FreeSurface(surf);
}
// split into lines that fit max_w (word wrap), at most max_lines; the last line is shortened
static std::vector<std::string> wrap(TTF_Font *f,const std::string& str,int max_w,int max_lines) {
    std::vector<std::string> out; std::string line,word; size_t i=0;
    auto width=[&](const std::string& t){int w=0; if(f&&!t.empty()) TTF_SizeUTF8(f,t.c_str(),&w,nullptr); return w;};
    while(i<=str.size()) {
        if(i==str.size()||str[i]==' ') {
            std::string cand=line.empty()?word:line+" "+word;
            if(!line.empty() && width(cand)>max_w) { out.push_back(line); line=word; }
            else line=cand;
            word.clear();
        } else word+=str[i];
        ++i;
    }
    if(!line.empty()) out.push_back(line);
    if(int(out.size())>max_lines) { out.resize(max_lines); }
    return out;
}
void Shell::background(int w,int h) {
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
    // the menu's dark radial glow, approximated with horizontal bands
    for(int y=0;y<h;y+=2) {
        const double d=std::fabs(y-h*0.45)/(h*0.9);
        const double k=std::max(0.0,1.0-d*1.6);
        SDL_SetRenderDrawColor(renderer,Uint8(6+15*k),Uint8(5+11*k),Uint8(10+21*k),255);
        SDL_Rect band{0,y,w,2}; SDL_RenderFillRect(renderer,&band);
    }
    // faint scanlines
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer,0,0,0,40);
    for(int y=0;y<h;y+=4) SDL_RenderDrawLine(renderer,0,y,w,y);
}
void Shell::footer(const std::string& keys,int w,int h) {
    const double s=h/1080.0;
    fill(renderer,{0,int(h-74*s),w,int(74*s)},{6,5,10,200});
    SDL_SetRenderDrawColor(renderer,accent_.r,accent_.g,accent_.b,120);
    SDL_RenderDrawLine(renderer,0,int(h-74*s),w,int(h-74*s));
    text(pix_small_,keys,int(w*0.08),int(h-46*s),kMute,int(w*0.84));
}
void Shell::draw_rows(const std::vector<UiRow>& rows,int selected,SDL_Rect area,int row_h,bool delay_arrows) {
    const double s=row_h/84.0;
    const int count=int(rows.size()),visible=std::max(1,area.h/row_h),first=first_visible(selected,count,visible);
    for(int i=0;i<visible && first+i<count;++i) {
        const UiRow& r=rows[first+i];
        const bool on=first+i==selected;
        SDL_Rect rc{area.x,area.y+i*row_h,area.w,row_h-int(8*s)};
        if(on) { fill(renderer,rc,kRowOn); fill(renderer,{rc.x,rc.y,int(6*s),rc.h},accent_); }
        const int ty=rc.y+(rc.h-TTF_FontHeight(body_))/2;
        const SDL_Color lc=!r.enabled&&!on?kMute:kInk;
        text(on?body_bold_:body_,r.label,rc.x+int(28*s),ty,lc,rc.w*55/100);
        std::string v=r.value;
        if(delay_arrows && first+i==2 && on) v="<  "+v+"  >";
        if(!v.empty()) text(body_bold_,v,rc.x+rc.w-int(26*s),ty,on?accent_:kMute,rc.w*42/100,2);
    }
    if(first>0) text(pix_small_,"^",area.x+area.w-int(18*s),area.y-int(22*s),kDim);
    if(first+visible<count) text(pix_small_,"v",area.x+area.w-int(18*s),area.y+visible*row_h,kDim);
}
void Shell::draw_dialog(const UiDialog& d,int w,int h) {
    const double s=h/1080.0;
    fill(renderer,{0,0,w,h},{3,2,6,200});
    const int row_h=int(70*s),count=int(d.choices.size()),visible=std::min(count,7);
    SDL_Rect card{w/2-int(380*s),0,int(760*s),int(170*s)+visible*row_h+int(70*s)};
    card.y=(h-card.h)/2;
    fill(renderer,card,{18,14,30,250});
    fill(renderer,{card.x,card.y,card.w,int(6*s)},accent_);
    text(pix_,d.title,card.x+int(36*s),card.y+int(40*s),accent_,card.w-int(72*s));
    text(small_,d.description,card.x+int(36*s),card.y+int(84*s),kMute,card.w-int(72*s));
    const int first=first_visible(d.selected,count,visible),top=card.y+int(140*s);
    for(int i=0;i<visible;++i) {
        const int idx=first+i; const bool on=idx==d.selected;
        SDL_Rect rc{card.x+int(24*s),top+i*row_h,card.w-int(48*s),row_h-int(6*s)};
        if(on) { fill(renderer,rc,kRowOn); fill(renderer,{rc.x,rc.y,int(6*s),rc.h},accent_); }
        text(on?body_bold_:body_,d.choices[idx],rc.x+int(28*s),rc.y+(rc.h-TTF_FontHeight(body_))/2,on?kInk:kMute);
        if(idx==d.current) text(pix_small_,"NOW",rc.x+rc.w-int(24*s),rc.y+(rc.h-TTF_FontHeight(pix_small_))/2,accent_,0,2);
    }
    text(pix_small_,"[OK] CHOOSE   [BACK] CANCEL",card.x+int(36*s),card.y+card.h-int(44*s),kDim);
}
void Shell::draw_pairing(const std::string& pin,int w,int h) {
    const double s=h/1080.0;
    fill(renderer,{0,0,w,h},{3,2,6,215});
    SDL_Rect card{w/2-int(520*s),h/2-int(240*s),int(1040*s),int(480*s)};
    fill(renderer,card,{18,14,30,250});
    fill(renderer,{card.x,card.y,card.w,int(6*s)},accent_);
    text(pix_,"PAIR YOUR DEVICE",w/2,card.y+int(56*s),accent_,0,1);
    text(body_,"Type this code on your iPhone, iPad or Mac",w/2,card.y+int(118*s),kInk,0,1);
    std::string spaced; for(size_t i=0;i<pin.size();++i){ if(i) spaced+=' '; spaced+=pin[i]; }
    text(pin_,spaced,w/2,card.y+int(220*s),kInk,0,1);
    text(pix_small_,"FIRST TIME ONLY   [BACK] CANCEL",w/2,card.y+card.h-int(64*s),kDim,0,1);
}

void Shell::screen(const std::string& status,const std::string& pin,int) {
    ensure_fonts();
    int w,h; SDL_GetRendererOutputSize(renderer,&w,&h); const double s=h/1080.0;
    background(w,h);
    text(pix_,"ROUND2 · AIRPLAY",int(w*0.08),int(h*0.14),accent_);
    text(title_,airplay_name(),int(w*0.08),int(h*0.19),kInk,int(w*0.84));
    text(body_,status,int(w*0.08),int(h*0.19+120*s),kMute,int(w*0.84));
    if(!pin.empty()) text(small_,pin,int(w*0.08),int(h*0.19+180*s),kDim,int(w*0.84));
    SDL_RenderPresent(renderer);
}

void Shell::page(UiPage page_id,const std::string& subtitle,const std::vector<UiRow>& rows,int selected,const std::string& footer_keys,
                 const UiDialog *dialog,const std::string& pin,bool,const UiHistoryData *) {
    ensure_fonts();
    int w,h; SDL_GetRendererOutputSize(renderer,&w,&h);
    const Layout l=layout_for(w,h,page_id,int(rows.size())); const double s=l.s;
    background(w,h);
    const int x=int(w*0.08);
    if(page_id==UiPage::System) {
        text(pix_,"ROUND2 · AIRPLAY",x,int(h*0.12),accent_);
        text(title_,"More settings",x,int(h*0.16),kInk);
        fill(renderer,l.card,kPanel);
        text(pix_small_,"MOST GUESTS WILL NEVER NEED THESE",l.card.x+int(28*s),l.card.y+int(30*s),kDim);
        draw_rows(rows,selected,{l.card.x+int(16*s),l.rows_top,l.card.w-int(32*s),l.visible*l.row_h},l.row_h,false);
        if(selected>=0 && selected<int(rows.size())) { int ny=l.card.y+l.card.h+int(22*s);
            for(const std::string& ln:wrap(small_,rows[selected].note,int(w*0.84),2)) { text(small_,ln,x,ny,kMute,int(w*0.84)); ny+=TTF_FontLineSkip(small_); } }
    } else {
        // how to connect
        text(pix_,"ROUND2 · AIRPLAY",x,int(h*0.12),accent_);
        text(title_,airplay_name(),x,int(h*0.16),kInk,int(w*0.47));
        text(body_,subtitle,x,int(h*0.16+122*s),kMute,int(w*0.47));
        const char *steps[3]={"Open Control Centre on your iPhone or iPad","Tap Screen Mirroring",nullptr};
        const std::string choose="Choose "+airplay_name();
        for(int i=0;i<3;++i) {
            const int y=int(h*0.42)+i*int(112*s);
            SDL_Rect tag{x,y,int(64*s),int(64*s)};
            slant(renderer,tag,accent_,int(10*s));
            text(pix_,std::to_string(i+1),tag.x+tag.w/2,tag.y+(tag.h-TTF_FontHeight(pix_))/2,kVoid,0,1);
            text(body_,steps[i]?steps[i]:choose,x+int(92*s),y+(tag.h-TTF_FontHeight(body_))/2,kInk,int(w*0.40));
        }
        text(small_,"Same Wi-Fi as this TV.  Netflix, Disney+ and Apple TV+ won't mirror.",x,int(h*0.42)+3*int(112*s)+int(8*s),kDim,int(w*0.47));
        // quick settings
        fill(renderer,l.card,kPanel);
        text(pix_small_,"QUICK SETTINGS",l.card.x+int(28*s),l.card.y+int(30*s),accent_);
        draw_rows(rows,selected,{l.card.x+int(16*s),l.rows_top,l.card.w-int(32*s),l.visible*l.row_h},l.row_h,true);
        if(selected>=0 && selected<int(rows.size())) { int ny=l.card.y+l.card.h+int(22*s);
            for(const std::string& ln:wrap(small_,rows[selected].note,l.card.w,3)) { text(small_,ln,l.card.x,ny,kMute,l.card.w); ny+=TTF_FontLineSkip(small_); } }
    }
    footer(footer_keys,w,h);
    if(dialog) draw_dialog(*dialog,w,h);
    if(!pin.empty()) draw_pairing(pin,w,h);
    SDL_RenderPresent(renderer);
}

void Shell::overlay(const std::vector<std::string>& lines,const std::string& pin) {
    ensure_fonts();
    int w,h; SDL_GetRendererOutputSize(renderer,&w,&h); const double s=h/1080.0;
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer,0,0,0,0); SDL_RenderClear(renderer);   // the picture shows through
    if(!lines.empty()) {
        int bw=0; for(const std::string& l:lines) bw=std::max(bw,text_w(body_,l));
        const int lh=int(46*s);
        SDL_Rect box{int(48*s),int(h-80*s)-int(lines.size())*lh-int(36*s),bw+int(76*s),int(lines.size())*lh+int(36*s)};
        fill(renderer,box,{6,5,10,205});
        fill(renderer,{box.x,box.y,int(6*s),box.h},accent_);
        for(size_t i=0;i<lines.size();++i) text(i?small_:body_bold_,lines[i],box.x+int(34*s),box.y+int(18*s)+int(i)*lh,i?kMute:kInk);
    }
    if(!pin.empty()) draw_pairing(pin,w,h);
    SDL_RenderPresent(renderer);
}

UiHit Shell::hit_test(int x,int y,int row_count,const UiDialog *dialog,int selected,UiPage page_id) const {
    int w=0,h=0; SDL_GetRendererOutputSize(renderer,&w,&h);
    const double s=h/1080.0;
    if(dialog) {
        const int row_h=int(70*s),count=int(dialog->choices.size()),visible=std::min(count,7);
        const int card_w=int(760*s),card_h=int(170*s)+visible*row_h+int(70*s);
        const int cx=w/2-int(380*s),cy=(h-card_h)/2,top=cy+int(140*s);
        if(x<cx||x>=cx+card_w||y<top||y>=top+visible*row_h) return {UiHitKind::Cancel,-1};
        return {UiHitKind::DialogChoice,first_visible(dialog->selected,count,visible)+(y-top)/row_h};
    }
    const Layout l=layout_for(w,h,page_id,row_count);
    if(x<l.card.x||x>=l.card.x+l.card.w||y<l.rows_top||y>=l.rows_top+l.visible*l.row_h) return {};
    const int idx=first_visible(selected,row_count,l.visible)+(y-l.rows_top)/l.row_h;
    return idx<row_count?UiHit(UiHitKind::Row,idx):UiHit();
}
