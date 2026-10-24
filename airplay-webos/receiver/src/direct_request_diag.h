#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All output values come from fixed vocabularies except the clamped byte count. */
void direct_request_diag_format(char *output, size_t capacity,
                                const char *method, const char *path,
                                const char *content_type, const char *protocol,
                                int body_length, int session_header_present);

/* Only an empty, unqualified HTTP GET /info is eligible for the development
 * handshake. In particular, RTSP mirroring and URLs with queries are excluded. */
int direct_info_eligible(const char *method, const char *path,
                         const char *protocol, const char *content_type,
                         int body_length, int session_header_present);

/* At most 192 request records and one suppression marker per process. */
void direct_request_diag_log(const char *method, const char *path,
                             const char *content_type, const char *protocol,
                             int body_length, int session_header_present);
void direct_request_diag_log_result(const char *method, const char *path,
                                    const char *content_type, const char *protocol,
                                    int body_length, int session_header_present,
                                    int result);
void direct_request_diag_body_limit(void);

#ifdef __cplusplus
}
#endif
