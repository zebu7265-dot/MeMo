#pragma once
#include "types.hpp"
#include <memory>
#include <chrono>
#include <cstddef>
#include <string>

namespace om {

class MemoryObject {
public:
    const UUID id;
    const AgentID owner;
    const Timestamp recorded_at;

    // New fields for storage tiers
    mutable std::chrono::system_clock::time_point last_accessed{std::chrono::system_clock::now()};
    mutable size_t serialized_size{0};

    virtual ~MemoryObject() = default;

    virtual ObjectType type() const noexcept = 0;
    virtual bool is_valid() const noexcept { return true; }

    // Serialization helpers (JSON-first). Implementations should override if custom behavior needed.
    virtual std::string serialize() const {
        // Minimal JSON with id and type; override in derived classes to include fields
        return std::string("{\"id\":\"") + id + "\", \"type\": \"" + std::to_string(static_cast<int>(type())) + "\"}";
    }

    static std::shared_ptr<const MemoryObject> deserialize(const std::string& data);

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
