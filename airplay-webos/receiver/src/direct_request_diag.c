#include "direct_request_diag.h"
#include <stdio.h>
#include <string.h>

#define PROBE_MAX_BYTES 16777216
#define PROBE_MAX_RECORDS 192

static const char *method_label(const char *value) {
    if (!value) return "unknown";
    if (!strcmp(value,"GET")) return "GET";
    if (!strcmp(value,"POST")) return "POST";
    if (!strcmp(value,"PUT")) return "PUT";
    if (!strcmp(value,"HEAD")) return "HEAD";
    if (!strcmp(value,"OPTIONS")) return "OPTIONS";
    if (!strcmp(value,"DELETE")) return "DELETE";
    return "other";
}
static int path_is(const char *path,const char *known) {
    size_t n=strlen(known);
    return path && !strncmp(path,known,n) && (path[n]=='\0' || path[n]=='?');
}
static const char *path_label(const char *path) {
    if (path_is(path,"/photo")) return "photo";
    if (path_is(path,"/slideshows")) return "slideshows";
    if (path_is(path,"/play")) return "play";
    if (path_is(path,"/server-info")) return "server-info";
    if (path_is(path,"/reverse")) return "reverse";
    if (path_is(path,"/playback-info")) return "playback-info";
    if (path_is(path,"/stop")) return "stop";
    if (path_is(path,"/action")) return "action";
    if (path_is(path,"/scrub")) return "scrub";
    if (path_is(path,"/rate")) return "rate";
    if (path_is(path,"/getProperty")) return "get-property";
    if (path_is(path,"/setProperty")) return "set-property";
    if (path_is(path,"/event")) return "event";
    if (path_is(path,"/info")) return "info";
    if (path_is(path,"/fp-setup2")) return "fairplay-setup";
    return "other";
}
static const char *type_label(const char *value) {
    if (!value) return "none";
    const struct {const char *raw,*label;} known[]={
        {"image/jpeg","image-jpeg"},{"image/heic","image-heic"},
        {"image/heif","image-heif"},{"image/png","image-png"},
        {"video/mp4","video-mp4"},{"application/x-apple-binary-plist","binary-plist"},
        {"text/x-apple-plist+xml","xml-plist"},{"application/vnd.apple.mpegurl","hls-playlist"},
        {"application/x-mpegurl","hls-playlist"},{"application/octet-stream","binary"},
        {"text/plain","text"}
    };
    size_t i;
    for(i=0;i<sizeof(known)/sizeof(known[0]);++i) {
        size_t n=strlen(known[i].raw);
        if(!strncmp(value,known[i].raw,n) && (value[n]=='\0' || value[n]==';'))
            return known[i].label;
    }
    return "other";
}
static const char *protocol_label(const char *value) {
    if(value && !strcmp(value,"HTTP/1.1")) return "HTTP/1.1";
    if(value && !strcmp(value,"RTSP/1.0")) return "RTSP/1.0";
    return "other";
}
static const char *category(const char *path, const char *content_type) {
    const char *p=path_label(path),*t=type_label(content_type);
    if(!strcmp(p,"photo") || !strcmp(p,"slideshows") || !strncmp(t,"image-",6))return "photo";
    if(!strcmp(p,"play") || !strcmp(t,"video-mp4") || !strcmp(t,"hls-playlist"))return "video";
    if(!strcmp(p,"other"))return "unknown";
    return "control";
}
static const char *info_query_label(const char *path) {
    if(!path_is(path,"/info"))return "na";
    if(!strcmp(path,"/info"))return "none";
    if(!strcmp(path,"/info?txtAirPlay"))return "txt-airplay";
    if(!strcmp(path,"/info?txtRAOP"))return "txt-raop";
    return "other";
}
int direct_info_eligible(const char *method, const char *path,
                         const char *protocol, const char *content_type,
                         int body_length, int session_header_present) {
    return method && !strcmp(method,"GET") && path && !strcmp(path,"/info") &&
           protocol && !strcmp(protocol,"HTTP/1.1") && !content_type &&
           body_length==0 && !session_header_present;
}
static const char *result_label(int result) {
    if(result==200)return "200";
    if(result==500)return "500";
    return "0";
}
static void format_result(char *output, size_t capacity,
                          const char *method, const char *path,
                          const char *content_type, const char *protocol,
                          int body_length, int session_header_present, int result) {
    if(!output || !capacity)return;
    int bounded=body_length<0?-1:body_length>PROBE_MAX_BYTES?PROBE_MAX_BYTES:body_length;
    snprintf(output,capacity,
             "[direct-probe] method=%s path=%s content_type=%s body_bytes=%d body_capped=%d protocol=%s session_header=%d category=%s result=%s info_query=%s",
             method_label(method),path_label(path),type_label(content_type),bounded,
             body_length>PROBE_MAX_BYTES,protocol_label(protocol),session_header_present?1:0,
             category(path,content_type),result_label(result),info_query_label(path));
}
void direct_request_diag_format(char *output, size_t capacity,
                                const char *method, const char *path,
                                const char *content_type, const char *protocol,
                                int body_length, int session_header_present) {
    format_result(output,capacity,method,path,content_type,protocol,
                  body_length,session_header_present,0);
}
static volatile unsigned record_count=0;
static int reserve_record(void) {
    unsigned index;
    do {
        index=record_count;
        if(index>PROBE_MAX_RECORDS)return 0;
    } while(!__sync_bool_compare_and_swap(&record_count,index,index+1));
    if(index==PROBE_MAX_RECORDS) {
        fprintf(stderr,"[direct-probe] limit=%d further_requests=omitted\n",PROBE_MAX_RECORDS);
        return 0;
    }
    return 1;
}
void direct_request_diag_log(const char *method, const char *path,
                             const char *content_type, const char *protocol,
                             int body_length, int session_header_present) {
    direct_request_diag_log_result(method,path,content_type,protocol,
                                   body_length,session_header_present,0);
}
void direct_request_diag_log_result(const char *method, const char *path,
                                    const char *content_type, const char *protocol,
                                    int body_length, int session_header_present,
                                    int result) {
    if(!reserve_record())return;
    char line[256];
    format_result(line,sizeof(line),method,path,content_type,protocol,
                  body_length,session_header_present,result);
    fprintf(stderr,"%s\n",line);
}
void direct_request_diag_body_limit(void) {
    if(!reserve_record())return;
    fprintf(stderr,"[direct-probe] method=unknown path=other content_type=unknown body_bytes=33554432 body_capped=1 protocol=other session_header=0 category=unknown result=body_limit\n");
}
