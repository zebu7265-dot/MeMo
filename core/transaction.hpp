#pragma once
#include "memory_graph.hpp"
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace om {

class Transaction {
public:
    explicit Transaction(MemoryGraph& graph);
    ~Transaction();

    std::shared_ptr<const MemoryObject> read(const UUID& id);
    template<typename T, typename... Args>
    std::shared_ptr<const T> stage_create(Args&&... args) {
        static_assert(std::is_base_of<MemoryObject, T>::value, "T must derive from MemoryObject");
        auto obj = std::make_shared<const T>(std::forward<Args>(args)...);
        const UUID& id = obj->id;
        // duplicate check in staged set
        if (write_set_.find(id) != write_set_.end()) {
            throw std::runtime_error("Duplicate id staged in this transaction");
        }
        // validate is_valid
        if (!obj->is_valid()) {
            throw std::runtime_error("Object validation failed during stage_create");
        }
        write_set_.emplace(id, obj);
        return std::static_pointer_cast<const T>(obj);
    }

    void stage_existing(std::shared_ptr<const MemoryObject> obj) {
        if (!obj) throw std::runtime_error("Null object cannot be staged");
        if (!obj->is_valid()) throw std::runtime_error("Object invalid in stage_existing");
        const UUID& id = obj->id;
        if (write_set_.find(id) != write_set_.end()) {
            throw std::runtime_error("Duplicate id staged in this transaction");
        }
        write_set_.emplace(id, std::move(obj));
    }

    bool commit();
    void rollback();

    bool committed() const noexcept { return committed_; }
    bool failed() const noexcept { return failed_; }

private:
    MemoryGraph& graph_;
    std::unordered_map<UUID, std::shared_ptr<const MemoryObject>> read_set_;
    std::unordered_map<UUID, std::shared_ptr<const MemoryObject>> write_set_;
    bool committed_{false};
    bool failed_{false};

    bool validate_references_nolock() const;
};

} // namespace om
