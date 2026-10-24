#pragma once

// The development package has its own launcher identity, AirPlay name, and
// app-local state directory. Keep the ACB fallback ID in step with appinfo.json.
#ifdef UXAIR_DEV_BUILD
#define UXAIR_APP_ID "com.github.depius.uxair.dev"
#define UXAIR_DISPLAY_NAME "UxAir Dev"
#else
#define UXAIR_APP_ID "sg.round2.airplay"
#define UXAIR_DISPLAY_NAME "Round2 TV"
#endif
