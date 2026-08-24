#pragma once
#include "memory_graph.hpp"
#include "transaction.hpp"
#include <shared_mutex>
#include <chrono>
#include <type_traits>

namespace om {

// MemoryGraph implementations

inline std::shared_ptr<const MemoryObject> MemoryGraph::get_object(const UUID& id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = objects_.find(id);
    if (it == objects_.end()) return nullptr;
    // objects_ stores StoredObject; return the contained pointer
    return it->second.obj;
}

inline std::shared_ptr<const Fact> MemoryGraph::get_fact(const UUID& id) const {
    auto obj = get_object(id);
    return std::dynamic_pointer_cast<const Fact>(obj);
}

inline std::shared_ptr<const Procedure> MemoryGraph::get_procedure(const UUID& id) const {
    auto obj = get_object(id);
    return std::dynamic_pointer_cast<const Procedure>(obj);
}

inline std::vector<std::shared_ptr<const Fact>> MemoryGraph::find_facts(const std::optional<std::string>& entity,
                                                                         const std::optional<std::string>& predicate,
                                                                         const std::optional<AgentID>& owner) const
{
    std::vector<std::shared_ptr<const Fact>> out;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& kv : objects_) {
        // kv.second is StoredObject; inspect the inner shared_ptr
        auto fact = std::dynamic_pointer_cast<const Fact>(kv.second.obj);
        if (!fact) continue;
        if (entity && fact->entity != *entity) continue;
        if (predicate && fact->predicate != *predicate) continue;
        if (owner && fact->owner != *owner) continue;
        out.push_back(fact);
    }
    return out;
}

inline Transaction MemoryGraph::begin_transaction() {
    return Transaction(*this);
}

inline void MemoryGraph::reindex_object(const std::shared_ptr<const MemoryObject>& obj) {
    if (!obj) return;
    owner_index_[obj->owner].push_back(obj->id);
}

inline bool MemoryGraph::validate_global_invariants() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& kv : objects_) {
        const auto& objptr = kv.second.obj;
        if (!objptr) return false;
        if (!objptr->is_valid()) return false;
        // Additional referential checks could be added here similar to Transaction::validate_references_nolock
    }
    return true;
}

// Transaction implementations

inline Transaction::Transaction(MemoryGraph& graph)
    : graph_(graph)
{
    // Capture a repeatable-read snapshot version at transaction start
    snapshot_version_ = graph_.global_version_counter_.load(std::memory_order_acquire);
}

inline Transaction::~Transaction() {
    if (!committed_ && !failed_) {
        rollback();
    }
}

inline std::shared_ptr<const MemoryObject> Transaction::read(const UUID& id) {
    // Check write_set first
    auto wit = write_set_.find(id);
    if (wit != write_set_.end()) return wit->second;

    // Then check read_set
    auto rit = read_set_.find(id);
    if (rit != read_set_.end()) return rit->second.first;

    // Lazy snapshot: read from graph under shared lock
    {
        std::shared_lock<std::shared_mutex> lock(graph_.mutex_);
        auto it = graph_.objects_.find(id);
        if (it == graph_.objects_.end()) {
            return nullptr;
        }
        // Only return the object if its version is visible to our snapshot
        if (it->second.version > snapshot_version_) {
            return nullptr;
        }
        // store the inner shared_ptr and observed version in the read_set
        read_set_.emplace(id, std::make_pair(it->second.obj, it->second.version));
        return it->second.obj;
    }
}

inline bool Transaction::validate_references_nolock() const {
    // For each staged object, ensure all referenced UUIDs exist either in graph or in write_set_
    auto has_id = [&](const UUID& id) -> bool {
        if (write_set_.find(id) != write_set_.end()) return true;
        if (graph_.objects_.find(id) != graph_.objects_.end()) return true;
        return false;
    };

    for (const auto& kv : write_set_) {
        const auto& obj = kv.second;
        if (!obj) return false;
        // Inspect based on dynamic type
        if (auto fact = std::dynamic_pointer_cast<const Fact>(obj)) {
            if (fact->superseded_by.has_value() && !has_id(*fact->superseded_by)) return false;
        }
        if (auto ev = std::dynamic_pointer_cast<const Event>(obj)) {
            for (const auto& fid : ev->involved_facts) {
                if (!has_id(fid)) return false;
            }
        }
        if (auto belief = std::dynamic_pointer_cast<const Belief>(obj)) {
            for (const auto& eid : belief->evidence) {
                if (!has_id(eid)) return false;
            }
            if (belief->superseded_by.has_value() && !has_id(*belief->superseded_by)) return false;
        }
        if (auto proc = std::dynamic_pointer_cast<const Procedure>(obj)) {
            if (proc->superseded_by.has_value() && !has_id(*proc->superseded_by)) return false;
            if (std::holds_alternative<CompositeBody>(proc->body)) {
                const auto& cb = std::get<CompositeBody>(proc->body);
                for (const auto& step : cb.steps) {
                    if (!has_id(step)) return false;
                }
            }
            // OpaqueRecipe and NativeBody have no external refs by contract
        }
        if (auto exec = std::dynamic_pointer_cast<const Execution>(obj)) {
            if (!has_id(exec->procedure_id)) return false;
            for (const auto& in : exec->input_facts) if (!has_id(in)) return false;
            for (const auto& out : exec->output_facts) if (!has_id(out)) return false;
        }
    }
    return true;
}

inline bool Transaction::commit() {
    // Acquire unique lock on graph
    std::unique_lock<std::shared_mutex> lock(graph_.mutex_);

    // 1) Overwrite check: no staged IDs already exist in graph
    for (const auto& kv : write_set_) {
        if (graph_.objects_.find(kv.first) != graph_.objects_.end()) {
            failed_ = true;
            return false;
        }
    }

    // 2) Conflict detection: ensure read_set entries' versions match what's in graph
    for (const auto& kv : read_set_) {
        auto it = graph_.objects_.find(kv.first);
        if (it == graph_.objects_.end()) {
            // read something that disappeared
            failed_ = true;
            return false;
        }
        // version equality check
        const uint64_t observed_version = kv.second.second;
        if (it->second.version != observed_version) {
            failed_ = true;
            return false;
        }
    }

    // 3) Global invariant: references must exist in graph or in write_set
    if (!validate_references_nolock()) {
        failed_ = true;
        return false;
    }

    // 4) Apply atomically; create StoredObject entries with explicit versions
    for (const auto& kv : write_set_) {
        const uint64_t version =
            graph_.global_version_counter_.fetch_add(1, std::memory_order_relaxed);
        StoredObject so{kv.second, version};
        graph_.objects_.emplace(kv.first, so);
        graph_.reindex_object(so.obj);
    }

    committed_ = true;
    return true;
}

inline void Transaction::rollback() {
    write_set_.clear();
    read_set_.clear();
    failed_ = true;
}

} // namespace om
