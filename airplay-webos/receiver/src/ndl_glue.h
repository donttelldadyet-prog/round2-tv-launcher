#pragma once
/* Round2: C wrappers for NDL DirectMedia v2 (see ndl_glue.c). */
#ifdef __cplusplus
extern "C" {
#endif
int r2_ndl_init(const char *app_id, void (*released)(const char *));
void r2_ndl_foreground(void);
const char *r2_ndl_error(void);
int r2_ndl_load(int width, int height, int hevc, int with_audio, int sample_rate,
                void (*event)(int, long long, const char *));
int r2_ndl_video(const void *data, unsigned size, long long pts_ms);
int r2_ndl_audio(const void *data, unsigned size, long long pts_ms);
int r2_ndl_unload(void);
#ifdef __cplusplus
}
#endif
