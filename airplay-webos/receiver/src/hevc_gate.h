#pragma once
#include <cstdint>
#include <string>

namespace hevc_probe {
struct GateIdentity {
    std::string boot_id, commit;
    uint64_t device = 0, inode = 0, size = 0;
    int64_t mtime = 0;
};
// One installed binary, one boot, five-minute lifetime, consumed before UHD.
bool collect_gate_identity(const std::string& app_dir, GateIdentity& identity,
                           std::string& reason);
bool clear_uhd_gate(const std::string& app_dir, std::string& reason);
bool write_uhd_gate(const std::string& app_dir, const GateIdentity& identity,
                    int64_t completed_ms, int source_pid, std::string& reason);
bool consume_uhd_gate(const std::string& app_dir, const GateIdentity& identity,
                      int64_t now_ms, int& source_pid, std::string& reason);
}
