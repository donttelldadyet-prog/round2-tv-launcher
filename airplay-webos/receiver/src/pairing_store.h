#pragma once

#include <cstdio>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <vector>

// Persist a snapshot prepared by the caller. This can take time on webOS flash
// storage, so callers must not hold locks used by the UI while invoking it.
inline bool write_pairing_keys_atomic(const std::string& path,
                                      const std::vector<std::string>& keys) {
    const std::string temporary = path + ".tmp";
    FILE *file = fopen(temporary.c_str(), "w");
    if (!file) return false;

    bool ok = true;
    for (const auto& key : keys) {
        if (fprintf(file, "%s\n", key.c_str()) < 0) ok = false;
    }
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    if (ok && rename(temporary.c_str(), path.c_str()) == 0) return true;
    unlink(temporary.c_str());
    return false;
}
