#pragma once
#include "types.hpp"
#include <memory>
#include <chrono>

namespace om {

class MemoryObject {
public:
    const UUID id;
    const AgentID owner;
    const Timestamp recorded_at;

    virtual ~MemoryObject() = default;

    virtual ObjectType type() const noexcept = 0;
    virtual bool is_valid() const noexcept { return true; }

protected:
    // Existing constructor (keeps full backwards compatibility)
    MemoryObject(const UUID& id_, const AgentID& owner_, const Timestamp& recorded_at_)
        : id(id_), owner(owner_), recorded_at(recorded_at_) {}

    // Convenience constructor: set recorded_at to now()
    MemoryObject(const UUID& id_, const AgentID& owner_)
        : id(id_), owner(owner_), recorded_at(std::chrono::system_clock::now()) {}

    // immutable: disable copy/move
    MemoryObject(const MemoryObject&) = delete;
    MemoryObject& operator=(const MemoryObject&) = delete;
    MemoryObject(MemoryObject&&) = delete;
    MemoryObject& operator=(MemoryObject&&) = delete;
};

using MemoryObjectPtr = std::shared_ptr<const MemoryObject>;

} // namespace om
