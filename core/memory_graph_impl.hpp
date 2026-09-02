#pragma once
#include "memory_graph.hpp"
#include "transaction.hpp"
#include "third_party/nlohmann-json/include_nlohmann_json.hpp"
#include <shared_mutex>
#include <chrono>
#include <type_traits>
#include <unordered_set>

namespace om {

using nlohmann::json;

// MemoryGraph implementations

inline std::shared_ptr<const MemoryObject> MemoryGraph::get_object(const UUID& id) const {
    // 1) Check authoritative graph store under shared lock first
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = objects_.find(id);
        if (it != objects_.end()) return it->second.obj;
    }

    // 2) Try HotCache (best-effort)
    if (auto h = hot_.get(id)) return h;

    // 3) Not in memory; try cold load (no locks)
    auto loaded = cold_.load(id); // returns optional<StoredObject>
    if (!loaded.has_value()) return nullptr;

    // 4) Insert loaded StoredObject into graph under unique lock and then put into hot while still holding graph lock
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = objects_.find(id);
        if (it == objects_.end()) {
            objects_.emplace(id, *loaded);
            reindex_object(loaded->obj);
            // Eviction callback must persist authoritative version from objects_
            hot_.put(loaded->obj, [&](MemoryObjectPtr ev) -> bool {
                auto git = objects_.find(ev->id);
                if (git == objects_.end()) {
                    throw std::runtime_error("Cold eviction: object not found in graph when persisting to cold: " + ev->id);
                }
                uint64_t authoritative_version = git->second.version;
                // Persist with explicit version
                cold_.save_with_version(ev->id, *ev, authoritative_version);
                return true;
            });
            return loaded->obj;
        } else {
            return it->second.obj;
        }
    }
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
    std::unordered_set<UUID> seen_ids;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& kv : objects_) {
        auto fact = std::dynamic_pointer_cast<const Fact>(kv.second.obj);
        if (!fact) continue;
        if (entity && fact->entity != *entity) continue;
        if (predicate && fact->predicate != *predicate) continue;
        if (owner && fact->owner != *owner) continue;
        if (seen_ids.insert(fact->id).second) out.push_back(fact);
    }
    lock.unlock();

    // A restarted graph has no in-memory entries yet; include persisted Cold objects.
    for (const auto& id : cold_.list_all_ids()) {
        if (auto so = cold_.load(id)) {
            auto fact = std::dynamic_pointer_cast<const Fact>(so->obj);
            if (!fact) continue;
            if (entity && fact->entity != *entity) continue;
            if (predicate && fact->predicate != *predicate) continue;
            if (owner && fact->owner != *owner) continue;
            if (seen_ids.insert(fact->id).second) out.push_back(fact);
        }
    }
    return out;
}

inline Transaction MemoryGraph::begin_transaction() {
    return Transaction(*this);
}

inline void MemoryGraph::reindex_object(const std::shared_ptr<const MemoryObject>& obj) {
    if (!obj) return;
    auto &vec = owner_index_[obj->owner];
    if (std::find(vec.begin(), vec.end(), obj->id) == vec.end()) vec.push_back(obj->id);
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
    : graph_(graph), snapshot_version_(graph.global_version_counter_.load(std::memory_order_acquire))
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
                // capture observed version
                std::shared_lock<std::shared_mutex> lock2(graph_.mutex_);
                auto it2 = graph_.objects_.find(id);
                uint64_t observed_v = (it2 != graph_.objects_.end()) ? it2->second.version : 0;
                read_set_.emplace(id, std::make_pair(obj, observed_v));
                return obj;
            }
            return nullptr;
        }
        // store pointer and observed version
        read_set_.emplace(id, std::make_pair(it->second.obj, it->second.version));
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
        // Do not set final version here; will be set in Phase 2 after assigning v
        j["type"] = static_cast<int>(obj->type());
        j["recorded_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(obj->recorded_at.time_since_epoch()).count();
        j["last_accessed"] = std::chrono::duration_cast<std::chrono::milliseconds>(obj->last_accessed.time_since_epoch()).count();
        try {
            j["payload"] = json::parse(obj->serialize());
        } catch (...) {
            j["payload"] = obj->serialize();
        }
        // write temp payload (may throw)
        graph_.cold_.write_temp(id, j.dump(2));
        serialized_ids.push_back(id);
    }

    // Phase 2: assign versions, write final files (outside graph lock), then insert into graph while holding lock.
    std::vector<UUID> final_written;
    try {
        // Assign versions and atomically write final wrapper files with assigned versions.
        std::unordered_map<UUID, uint64_t> assigned_versions;
        for (const auto& id : serialized_ids) {
            auto itw = write_set_.find(id);
            if (itw == write_set_.end()) { throw std::runtime_error("Internal error: write_set missing id in phase2"); }
            const auto& obj = itw->second;
            uint64_t v = graph_.global_version_counter_.fetch_add(1);
            json wrapper;
            wrapper["version"] = v;
            wrapper["type"] = static_cast<int>(obj->type());
            wrapper["recorded_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(obj->recorded_at.time_since_epoch()).count();
            wrapper["last_accessed"] = std::chrono::duration_cast<std::chrono::milliseconds>(obj->last_accessed.time_since_epoch()).count();
            try {
                wrapper["payload"] = json::parse(obj->serialize());
            } catch (...) {
                wrapper["payload"] = obj->serialize();
            }
            // write final wrapper atomically (may throw)
            graph_.cold_.write_final(id, wrapper.dump(2));
            final_written.push_back(id);
            assigned_versions[id] = v;
        }

        // Now acquire graph lock and do overwrite/conflict/reference checks and insert
        std::unique_lock<std::shared_mutex> lock(graph_.mutex_);

        // 1) Overwrite check
        for (const auto& id : serialized_ids) {
            if (graph_.objects_.find(id) != graph_.objects_.end()) {
                failed_ = true;
                // Best-effort cleanup: remove the final files we wrote
                lock.unlock();
                for (const auto& fid : final_written) { std::error_code ec; fs::remove((fs::path(graph_.cold_.make_filename(fid))).string(), ec); }
                return false;
            }
        }

        // 2) Conflict detection on read_set (strict equality to observed versions)
        for (const auto& kv : read_set_) {
            auto it = graph_.objects_.find(kv.first);
            if (it == graph_.objects_.end()) {
                failed_ = true; return false;
            }
            uint64_t observed_version = kv.second.second;
            if (it->second.version != observed_version) { failed_ = true; return false; }
        }

        // 3) Global references validation
        if (!validate_references_nolock()) { failed_ = true; return false; }

        // 4) Insert into graph and put into hot (still holding graph lock)
        for (const auto& id : serialized_ids) {
            const auto& obj = write_set_.at(id);
            uint64_t assigned_v = assigned_versions.at(id);
            StoredObject so{obj, assigned_v};
            graph_.objects_.emplace(id, so);
            graph_.reindex_object(obj);
            // Call hot_.put while holding graph lock (preserve graph->hot order)
            hot_.put(obj, [assigned_v, this](MemoryObjectPtr ev) -> bool {
                // persist authoritative assigned_v
                cold_.save_with_version(ev->id, *ev, assigned_v);
                return true;
            });
        }

        committed_ = true;
        return true;
    } catch (const std::exception& e) {
        // If any write_final or other step failed, try to cleanup files written and abort
        for (const auto& fid : final_written) {
            std::error_code ec;
            fs::remove((fs::path(graph_.cold_.make_filename(fid))).string(), ec);
        }
        failed_ = true;
        throw; // propagate to caller
    }
}

inline void Transaction::rollback() {
    write_set_.clear();
    read_set_.clear();
    failed_ = true;
}

} // namespace om
