#pragma once

#include "pairing_store.h"
#include "pairing_ui.h"
#include <mutex>
#include <set>
#include <string>
#include <vector>

enum class PairingRegistrationResult { Added, AlreadyRegistered, LimitReached, PersistenceFailed };

// Shared by the RAOP callback and the host regression test. The store lock
// serializes writes; the UI state lock is held only for memory snapshots and
// the final state transition, never for flash I/O.
inline PairingRegistrationResult register_pairing_key(
        std::mutex& store_mutex, std::mutex& state_mutex,
        std::set<std::string>& registered_keys, PairingOverlayState& pairing,
        std::string& status, const std::string& directory,
        const std::string& key, bool& overlay_dismissed) {
    overlay_dismissed = false;
    std::lock_guard<std::mutex> store_lock(store_mutex);
    std::vector<std::string> snapshot;
    std::string path;
    {
        std::lock_guard<std::mutex> state_lock(state_mutex);
        if (registered_keys.count(key)) return PairingRegistrationResult::AlreadyRegistered;
        if (registered_keys.size() >= 64) return PairingRegistrationResult::LimitReached;
        snapshot.assign(registered_keys.begin(), registered_keys.end());
        path = directory + "/paired.keys";
    }
    snapshot.push_back(key);
    if (!write_pairing_keys_atomic(path, snapshot))
        return PairingRegistrationResult::PersistenceFailed;
    {
        std::lock_guard<std::mutex> state_lock(state_mutex);
        registered_keys.insert(key);
        overlay_dismissed = pairing.completed();
        status = "AirPlay pairing complete";
    }
    return PairingRegistrationResult::Added;
}
