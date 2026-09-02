#pragma once
#include "memory_object.hpp"
#include "types.hpp"
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <algorithm>
#include "third_party/nlohmann-json/include_nlohmann_json.hpp"
#include <chrono>

namespace om {

using nlohmann::json;

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

    std::string serialize() const override {
        json j;
        j["id"] = id;
        j["owner"] = owner;
        j["recorded_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(recorded_at.time_since_epoch()).count();
        j["type"] = static_cast<int>(type());
        j["entity"] = entity;
        j["predicate"] = predicate;
        // value: recursively convert
        std::function<json(const Value&)> val_to_json = [&](const Value& v) -> json {
            if (std::holds_alternative<std::monostate>(v)) return nullptr;
            if (std::holds_alternative<bool>(v)) return std::get<bool>(v);
            if (std::holds_alternative<int64_t>(v)) return std::get<int64_t>(v);
            if (std::holds_alternative<double>(v)) return std::get<double>(v);
            if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
            if (std::holds_alternative<std::vector<Value>>(v)) {
                json a = json::array();
                for (const auto& e : std::get<std::vector<Value>>(v)) a.push_back(val_to_json(e));
                return a;
            }
            if (std::holds_alternative<std::unordered_map<std::string, Value>>(v)) {
                json o = json::object();
                for (const auto& kv : std::get<std::unordered_map<std::string, Value>>(v)) o[kv.first] = val_to_json(kv.second);
                return o;
            }
            return nullptr;
        };
        j["value"] = val_to_json(value);
        j["validity"] = { {"from", std::chrono::duration_cast<std::chrono::milliseconds>(validity.valid_from.time_since_epoch()).count()}, {"to", std::chrono::duration_cast<std::chrono::milliseconds>(validity.valid_to.time_since_epoch()).count()} };
        j["confidence"] = { {"mean", confidence.mean}, {"variance", confidence.variance} };
        if (superseded_by) j["superseded_by"] = *superseded_by; else j["superseded_by"] = nullptr;
        return j.dump();
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

    std::string serialize() const override {
        json j;
        j["id"] = id;
        j["owner"] = owner;
        j["recorded_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(recorded_at.time_since_epoch()).count();
        j["type"] = static_cast<int>(type());
        j["action"] = action;
        j["involved_facts"] = json::array();
        for (const auto& f : involved_facts) j["involved_facts"].push_back(f);
        // reuse value converter
        std::function<json(const Value&)> val_to_json = [&](const Value& v) -> json {
            if (std::holds_alternative<std::monostate>(v)) return nullptr;
            if (std::holds_alternative<bool>(v)) return std::get<bool>(v);
            if (std::holds_alternative<int64_t>(v)) return std::get<int64_t>(v);
            if (std::holds_alternative<double>(v)) return std::get<double>(v);
            if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
            if (std::holds_alternative<std::vector<Value>>(v)) {
                json a = json::array();
                for (const auto& e : std::get<std::vector<Value>>(v)) a.push_back(val_to_json(e));
                return a;
            }
            if (std::holds_alternative<std::unordered_map<std::string, Value>>(v)) {
                json o = json::object();
                for (const auto& kv : std::get<std::unordered_map<std::string, Value>>(v)) o[kv.first] = val_to_json(kv.second);
                return o;
            }
            return nullptr;
        };
        j["payload"] = val_to_json(payload);
        return j.dump();
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

    std::string serialize() const override {
        // Start from Fact::serialize() but add evidence
        json j = json::parse(Fact::serialize());
        j["evidence"] = json::array();
        for (const auto& e : evidence) j["evidence"].push_back(e);
        return j.dump();
    }
};

} // namespace om
