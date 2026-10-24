#pragma once
#include <SDL.h>

enum class Action { None, Back, Exit, Left, Right, Up, Down, Select };

// Keep webOS-specific scancodes supplied by the TV build so this mapper can
// also be exercised with the native SDL2 headers in host tests.
class InputMapper {
public:
    InputMapper(SDL_Scancode back, SDL_Scancode exit) : back_(back), exit_(exit) {}
    Action map(const SDL_Event& event, Uint32 now_ms);
private:
    enum class SelectSource { None, Keyboard, Mouse };
    SDL_Scancode back_, exit_;
    SelectSource last_source_ = SelectSource::None;
    Uint32 last_select_ms_ = 0;
    Action select(SelectSource source, Uint32 now_ms);
};
