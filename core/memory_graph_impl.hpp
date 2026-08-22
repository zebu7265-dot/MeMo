#pragma once
#include "memory_graph.hpp"
#include "transaction.hpp"
#include "third_party/nlohmann-json/include_nlohmann_json.hpp"
#include <shared_mutex>
#include <chrono>
#include <type_traits>

namespace om {

using nlohmann::json;

// MemoryGraph implementations

inline std::shared_ptr<const MemoryObject> MemoryGraph::get_object(const UUID& id) const {
    // Try hot cache first
    if (auto h = hot_.get(id)) return h;

    // Not in hot, try cold storage (may be slow). If found, load into hot and return.
    auto from_cold = cold_.load(id);
    if (from_cold) {
        // put into hot; ensure eviction persists to cold
        hot_.put(from_cold, [&](MemoryObjectPtr ev) {
            // Eviction callback - persist evicted object to cold storage
            try { cold_.save(ev->id, *ev); } catch (...) {}
        });
        return from_cold;
    }

    // fallback: check in-memory graph under shared lock
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = objects_.find(id);
    if (it == objects_.end()) return nullptr;
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
        const auto& obj = kv.second.obj;
        if (!obj->is_valid()) return false;
    }
    return true;
}

// Transaction implementations

inline Transaction::Transaction(MemoryGraph& graph)
    : graph_(graph)
{}

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
    if (rit != read_set_.end()) return rit->second;

    // Lazy snapshot: read from graph under shared lock
    {
        std::shared_lock<std::shared_mutex> lock(graph_.mutex_);
        auto it = graph_.objects_.find(id);
        if (it == graph_.objects_.end()) {
            // also try hot/cold layers
            lock.unlock();
            auto obj = graph_.get_object(id);
            if (obj) {
                read_set_.emplace(id, obj);
                return obj;
            }
            return nullptr;
        }
        read_set_.emplace(id, it->second.obj);
        return it->second.obj;
    }
}

inline bool Transaction::validate_references_nolock() const {
    auto has_id = [&](const UUID& id) -> bool {
        if (write_set_.find(id) != write_set_.end()) return true;
        if (graph_.objects_.find(id) != graph_.objects_.end()) return true;
        return false;
    };

    for (const auto& kv : write_set_) {
        const auto& obj = kv.second;
        if (!obj) return false;
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
    // Phase 1: serialize and write temps (no graph lock)
    std::vector<UUID> serialized_ids;
    serialized_ids.reserve(write_set_.size());

    for (const auto& kv : write_set_) {
        const UUID& id = kv.first;
        const auto& obj = kv.second;
        if (!obj) { failed_ = true; return false; }
        json j;
        j["version"] = 1;
        j["type"] = static_cast<int>(obj->type());
        j["recorded_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(obj->recorded_at.time_since_epoch()).count();
        j["last_accessed"] = std::chrono::duration_cast<std::chrono::milliseconds>(obj->last_accessed.time_since_epoch()).count();
        try {
            j["payload"] = json::parse(obj->serialize());
        } catch (...) {
            j["payload"] = obj->serialize();
        }
        if (!graph_.cold_.write_temp(id, j.dump(2))) {
            failed_ = true;
            // attempt best-effort cleanup could be added here
            return false;
        }
        serialized_ids.push_back(id);
    }

    // Phase 2: acquire graph lock and perform validations + promote files + insert into graph
    std::unique_lock<std::shared_mutex> lock(graph_.mutex_);

    // 1) Overwrite check
    for (const auto& id : serialized_ids) {
        if (graph_.objects_.find(id) != graph_.objects_.end()) {
            failed_ = true;
            return false;
        }
    }

    // 2) Conflict detection on read_set
    for (const auto& kv : read_set_) {
        auto it = graph_.objects_.find(kv.first);
        if (it == graph_.objects_.end()) {
            failed_ = true; return false;
        }
        if (it->second.obj != kv.second) { failed_ = true; return false; }
    }

    // 3) Global references validation
    if (!validate_references_nolock()) { failed_ = true; return false; }

    // 4) Promote temp files and insert into graph with versions
    for (const auto& kv : write_set_) {
        const UUID& id = kv.first;
        const auto& obj = kv.second;
        if (!graph_.cold_.promote_temp(id)) { failed_ = true; return false; }
        uint64_t v = graph_.global_version_counter_.fetch_add(1);
        StoredObject so{obj, v};
        graph_.objects_.emplace(id, so);
        graph_.reindex_object(obj);
        // insert into hot cache; eviction will write to cold via callback
        graph_.hot_.put(obj, [&](MemoryObjectPtr ev) {
            try { graph_.cold_.save(ev->id, *ev); } catch (...) {}
        });
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
