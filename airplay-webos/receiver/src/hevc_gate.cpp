#include "hevc_gate.h"
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace hevc_probe {
namespace {
const int64_t kMaxAgeMs = 5 * 60 * 1000;
const char kName[] = "/hevc-uhd-gate.bin";
struct Record {
    char magic[16];
    char boot_id[37];
    char commit[41];
    uint64_t device, inode, size;
    int64_t mtime, completed_ms;
    int32_t source_pid;
};
bool valid_identity(const GateIdentity& identity) {
    if (identity.boot_id.size() != 36 || identity.commit.size() != 40) return false;
    for (size_t i=0;i<36;++i) {
        const char c=identity.boot_id[i];
        if (i==8 || i==13 || i==18 || i==23) { if (c!='-') return false; }
        else if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    for (char c: identity.commit)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return identity.inode!=0 && identity.size!=0;
}
bool fill(Record& record, const GateIdentity& identity) {
    if (!valid_identity(identity)) return false;
    record=Record{};
    std::memcpy(record.magic,"UXHEVC-GATE-1",14);
    std::memcpy(record.boot_id,identity.boot_id.data(),36);
    std::memcpy(record.commit,identity.commit.data(),40);
    record.device=identity.device; record.inode=identity.inode;
    record.size=identity.size; record.mtime=identity.mtime;
    return true;
}
bool read_small(const std::string& path, char *buffer, size_t capacity, size_t& length) {
    const int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if (fd<0) return false;
    const ssize_t n=read(fd,buffer,capacity-1);
    close(fd);
    if (n<0 || size_t(n)>=capacity-1) return false;
    length=size_t(n); buffer[length]=0; return true;
}
}
bool collect_gate_identity(const std::string& app_dir, GateIdentity& identity,
                           std::string& reason) {
    char buffer[192]; size_t length=0;
    if (!read_small("/proc/sys/kernel/random/boot_id",buffer,sizeof(buffer),length)) {
        reason="boot-id-unavailable"; return false;
    }
    identity.boot_id.assign(buffer,length);
    if (!identity.boot_id.empty() && identity.boot_id.back()=='\n') identity.boot_id.pop_back();
    if (!read_small(app_dir+"/build-info.txt",buffer,sizeof(buffer),length)) {
        reason="build-info-unavailable"; return false;
    }
    const char *commit=std::strstr(buffer,"commit=");
    if (!commit || std::strlen(commit+7)<40) {
        reason="build-commit-invalid"; return false;
    }
    identity.commit.assign(commit+7,40);
    struct stat info{};
    if (stat("/proc/self/exe",&info)!=0 || !S_ISREG(info.st_mode)) {
        reason="executable-identity-unavailable"; return false;
    }
    identity.device=static_cast<uint64_t>(info.st_dev);
    identity.inode=static_cast<uint64_t>(info.st_ino);
    identity.size=static_cast<uint64_t>(info.st_size);
    identity.mtime=static_cast<int64_t>(info.st_mtime);
    if (!valid_identity(identity)) { reason="identity-invalid"; return false; }
    reason="ready"; return true;
}
bool clear_uhd_gate(const std::string& app_dir, std::string& reason) {
    if (unlink((app_dir+kName).c_str())==0 || errno==ENOENT) {
        reason="cleared"; return true;
    }
    reason="clear-failed"; return false;
}
bool write_uhd_gate(const std::string& app_dir, const GateIdentity& identity,
                    int64_t completed_ms, int source_pid, std::string& reason) {
    Record record{};
    if (!fill(record,identity) || completed_ms<=0 || source_pid<=0) {
        reason="identity-invalid"; return false;
    }
    record.completed_ms=completed_ms;
    record.source_pid=source_pid;
    const std::string path=app_dir+kName;
    std::string temporary=path+".tmp.XXXXXX";
    const int fd=mkstemp(&temporary[0]); // Unique, exclusive, mode 0600.
    if (fd<0) { reason="create-failed"; return false; }
    const char *data=reinterpret_cast<const char *>(&record);
    size_t offset=0;
    while (offset<sizeof(record)) {
        const ssize_t n=write(fd,data+offset,sizeof(record)-offset);
        if (n<=0) break;
        offset+=size_t(n);
    }
    const bool synced=offset==sizeof(record) && fsync(fd)==0;
    close(fd);
    if (!synced || rename(temporary.c_str(),path.c_str())!=0) {
        unlink(temporary.c_str()); reason="write-failed"; return false;
    }
    reason="written"; return true;
}
bool consume_uhd_gate(const std::string& app_dir, const GateIdentity& identity,
                      int64_t now_ms, int& source_pid, std::string& reason) {
    source_pid=0;
    const std::string path=app_dir+kName;
    const int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if (fd<0) { reason=errno==ENOENT?"missing":"open-failed"; return false; }
    struct stat info{};
    const bool safe=fstat(fd,&info)==0 && S_ISREG(info.st_mode) &&
        info.st_uid==geteuid() && (info.st_mode&077)==0 &&
        info.st_size==static_cast<off_t>(sizeof(Record));
    Record record{};
    const ssize_t n=safe?read(fd,&record,sizeof(record)):-1;
    close(fd);
    if (unlink(path.c_str())!=0) { reason="consume-failed"; return false; }
    if (!safe || n!=static_cast<ssize_t>(sizeof(record))) {
        reason="unsafe-or-truncated"; return false;
    }
    Record expected{};
    if (!fill(expected,identity) || std::memcmp(record.magic,expected.magic,sizeof(record.magic))!=0) {
        reason="version-or-identity-invalid"; return false;
    }
    if (std::memcmp(record.boot_id,expected.boot_id,sizeof(record.boot_id))!=0 ||
        std::memcmp(record.commit,expected.commit,sizeof(record.commit))!=0 ||
        record.device!=expected.device || record.inode!=expected.inode ||
        record.size!=expected.size || record.mtime!=expected.mtime) {
        reason="different-boot-or-binary"; return false;
    }
    if (record.completed_ms<=0 || now_ms<record.completed_ms ||
        now_ms-record.completed_ms>kMaxAgeMs || record.source_pid<=0) {
        reason="expired-or-invalid-time"; return false;
    }
    source_pid=record.source_pid;
    reason="accepted"; return true;
}
}
