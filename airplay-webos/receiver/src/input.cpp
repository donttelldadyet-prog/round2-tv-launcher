#include "input.h"

Action InputMapper::select(SelectSource source, Uint32 now_ms) {
    // webOS may deliver both a key and a pointer button for one wheel press.
    // Suppress only the opposite event source in a short interval; repeated
    // clicks from the same source remain usable.
    if (last_source_ != SelectSource::None && last_source_ != source &&
        Uint32(now_ms - last_select_ms_) <= 120) return Action::None;
    last_source_ = source;
    last_select_ms_ = now_ms;
    return Action::Select;
}

Action InputMapper::map(const SDL_Event& event, Uint32 now_ms) {
    if (event.type == SDL_QUIT) return Action::Exit;
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        return select(SelectSource::Mouse, now_ms);
    if (event.type != SDL_KEYDOWN) return Action::None;
    const SDL_Scancode scan = event.key.keysym.scancode;
    switch (scan) {
    case SDL_SCANCODE_LEFT: return Action::Left;
    case SDL_SCANCODE_RIGHT: return Action::Right;
    case SDL_SCANCODE_UP: return Action::Up;
    case SDL_SCANCODE_DOWN: return Action::Down;
    default: break;
    }
    if (exit_ != SDL_SCANCODE_UNKNOWN && scan == exit_) return Action::Exit;
    if ((back_ != SDL_SCANCODE_UNKNOWN && scan == back_) ||
        scan == SDL_SCANCODE_ESCAPE || scan == SDL_SCANCODE_AC_BACK)
        return Action::Back;
    if (scan == SDL_SCANCODE_RETURN || scan == SDL_SCANCODE_KP_ENTER ||
        scan == SDL_SCANCODE_SELECT || event.key.keysym.sym == SDLK_RETURN ||
        event.key.keysym.sym == SDLK_KP_ENTER || event.key.keysym.sym == SDLK_SELECT) {
        if (event.key.repeat) return Action::None;
        return select(SelectSource::Keyboard, now_ms);
    }
    return Action::None;
}
