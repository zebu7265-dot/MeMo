#pragma once
#include "memory_object.hpp"
#include "types.hpp"
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <algorithm>

namespace om {

struct Fact : public MemoryObject {
    const std::string entity;
    const std::string predicate;
    const Value value;
    const TimeRange validity;
    const Confidence confidence;
    const std::optional<UUID> superseded_by;

    Fact(const UUID& id_, const AgentID& owner_, const Timestamp& recorded_at_,
         std::string entity_, std::string predicate_, Value value_,
         TimeRange validity_, Confidence confidence_, std::optional<UUID> superseded_by_ = std::nullopt)
        : MemoryObject(id_, owner_, recorded_at_)
        , entity(std::move(entity_))
        , predicate(std::move(predicate_))
        , value(std::move(value_))
        , validity(std::move(validity_))
        , confidence(std::move(confidence_))
        , superseded_by(std::move(superseded_by_))
    {}

    ObjectType type() const noexcept override { return ObjectType::Fact; }

    bool is_valid() const noexcept override {
        return !entity.empty() && !predicate.empty() && validity.is_valid() && confidence.is_valid();
    }
};

struct Event : public MemoryObject {
    const std::string action;
    const std::vector<UUID> involved_facts;
    const Value payload;

    Event(const UUID& id_, const AgentID& owner_, const Timestamp& recorded_at_,
          std::string action_, std::vector<UUID> involved_facts_, Value payload_)
        : MemoryObject(id_, owner_, recorded_at_)
        , action(std::move(action_))
        , involved_facts(std::move(involved_facts_))
        , payload(std::move(payload_))
    {}

    ObjectType type() const noexcept override { return ObjectType::Event; }

    bool is_valid() const noexcept override {
        return !action.empty();
    }
};

struct Belief : public Fact {
    const std::vector<UUID> evidence;

    Belief(const UUID& id_, const AgentID& owner_, const Timestamp& recorded_at_,
           std::string entity_, std::string predicate_, Value value_,
           TimeRange validity_, Confidence confidence_, std::vector<UUID> evidence_,
           std::optional<UUID> superseded_by_ = std::nullopt)
        : Fact(id_, owner_, recorded_at_, std::move(entity_), std::move(predicate_), std::move(value_),
               std::move(validity_), std::move(confidence_), std::move(superseded_by_))
        , evidence(std::move(evidence_))
    {}

    ObjectType type() const noexcept override { return ObjectType::Belief; }

    bool is_valid() const noexcept override {
        return Fact::is_valid();
    }
};

} // namespace om
