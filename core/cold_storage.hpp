#pragma once
#include "memory_object.hpp"
#include "memory_graph.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <shared_mutex>

namespace om {

class ColdStorage {
public:
    explicit ColdStorage(std::string base_path);

    // Persist an object to disk (JSON). Returns true on success.
    bool save(const UUID& id, const MemoryObject& obj);

    // Write serialized wrapper to a temp file (phase 1 of two-phase commit)
    bool write_temp(const UUID& id, const std::string& wrapper_json);

    // Promote temp file to final (phase 2 of commit). Returns true on success.
    bool promote_temp(const UUID& id);

    // Write a final file (wrapper JSON that includes assigned version) atomically.
    // Throws std::runtime_error on failure.
    void write_final(const UUID& id, const std::string& wrapper_json);

    // Persist an object to disk with an explicit MVCC version.
    // Throws std::runtime_error on failure.
    void save_with_version(const UUID& id, const MemoryObject& obj, uint64_t version);

    // Load object from disk. Returns StoredObject (obj + persisted version) when present, std::nullopt otherwise.
    std::optional<StoredObject> load(const UUID& id);

    // Remove object from cold storage (used when moving to Freeze). Returns true on success.
    bool remove(const UUID& id);

    // List all UUIDs known to cold storage (from index)
    std::vector<UUID> list_all_ids() const;

    // Find by entity (scans files; simple but slow)
    std::vector<UUID> find_by_entity(const std::string& entity) const;

    // Total size of cold storage (bytes)
    size_t total_size() const;

    // Rebuild index by scanning directory
    void rebuild_indexes();

private:
    std::string make_filename(const UUID& id) const;
    std::string make_tmp_filename(const UUID& id) const;
    void persist_index_nolock();

    std::string base_path_;
    std::unordered_map<UUID, std::string> file_map_; // id -> filename
    mutable std::shared_mutex mutex_;
};

} // namespace om
