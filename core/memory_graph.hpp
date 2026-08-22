#pragma once
#include "memory_object.hpp"
#include "declarative.hpp"
#include "procedural.hpp"
#include "types.hpp"
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <optional>
#include <functional>
#include <atomic>

namespace om {

struct StoredObject {
    std::shared_ptr<const MemoryObject> obj;
    uint64_t version;
};

class Transaction; // forward

class MemoryGraph {
public:
    MemoryGraph() = default;
    ~MemoryGraph() = default;

    std::shared_ptr<const MemoryObject> get_object(const UUID& id) const;
    std::shared_ptr<const Fact> get_fact(const UUID& id) const;
    std::shared_ptr<const Procedure> get_procedure(const UUID& id) const;

    std::vector<std::shared_ptr<const Fact>> find_facts(const std::optional<std::string>& entity,
                                                        const std::optional<std::string>& predicate,
                                                        const std::optional<AgentID>& owner) const;

    Transaction begin_transaction();

    bool validate_global_invariants() const;

private:
    friend class Transaction;

    mutable std::shared_mutex mutex_;
    // Map from id -> StoredObject (object pointer + version)
    std::unordered_map<UUID, StoredObject> objects_;
    std::unordered_map<AgentID, std::vector<UUID>> owner_index_;
    std::atomic<uint64_t> global_version_counter_{1};

    void reindex_object(const std::shared_ptr<const MemoryObject>& obj);

    // helper for transaction to apply changes
    bool has_object_nolock(const UUID& id) const {
        return objects_.find(id) != objects_.end();
    }
};

} // namespace om
