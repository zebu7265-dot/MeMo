#pragma once
#include "memory_object.hpp"
#include "types.hpp"
#include <string>
#include <variant>
#include <vector>
#include <optional>
#include <unordered_map>
#include <functional>
#include "third_party/nlohmann-json/include_nlohmann_json.hpp"

namespace om {

enum class ProcedureState { Draft, Active, Deprecated, Archived };

struct Contract {
    std::unordered_map<std::string, std::string> input_schema;
    std::unordered_map<std::string, std::string> output_schema;
    std::vector<std::string> preconditions;
};

struct NativeBody {
    std::string function_name;
};

struct CompositeBody {
    std::vector<UUID> steps;
};

struct OpaqueRecipe {
    std::string mime_type;
    std::string content;
};

enum class ExecutionStatus { Success, Failure, Partial };

struct Procedure : public MemoryObject {
    const std::string name;
    const std::string description;
    const Contract contract;
    const ProcedureState state;
    const std::optional<UUID> superseded_by;
    const std::variant<NativeBody, CompositeBody, OpaqueRecipe> body;

    Procedure(const UUID& id_, const AgentID& owner_, const Timestamp& recorded_at_,
              std::string name_, std::string description_, Contract contract_, ProcedureState state_,
              std::optional<UUID> superseded_by_, std::variant<NativeBody, CompositeBody, OpaqueRecipe> body_)
        : MemoryObject(id_, owner_, recorded_at_)
        , name(std::move(name_))
        , description(std::move(description_))
        , contract(std::move(contract_))
        , state(state_)
        , superseded_by(std::move(superseded_by_))
        , body(std::move(body_))
    {}

    ObjectType type() const noexcept override { return ObjectType::Procedure; }

    bool is_valid() const noexcept override {
        if (superseded_by.has_value()) {
            return state == ProcedureState::Deprecated;
        }
        return true;
    }

    std::string serialize() const override {
        using nlohmann::json;
        json j;
        j["id"] = id;
        j["owner"] = owner;
        j["recorded_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(recorded_at.time_since_epoch()).count();
        j["type"] = static_cast<int>(type());
        j["name"] = name;
        j["description"] = description;
        json jc = json::object();
        jc["input_schema"] = json::object();
        for (const auto& kv : contract.input_schema) jc["input_schema"][kv.first] = kv.second;
        jc["output_schema"] = json::object();
        for (const auto& kv : contract.output_schema) jc["output_schema"][kv.first] = kv.second;
        jc["preconditions"] = json::array();
        for (const auto& p : contract.preconditions) jc["preconditions"].push_back(p);
        j["contract"] = jc;
        j["state"] = static_cast<int>(state);
        if (superseded_by) j["superseded_by"] = *superseded_by; else j["superseded_by"] = nullptr;
        json jb = json::object();
        if (std::holds_alternative<NativeBody>(body)) {
            jb["function_name"] = std::get<NativeBody>(body).function_name;
        } else if (std::holds_alternative<CompositeBody>(body)) {
            jb["steps"] = json::array();
            for (const auto& s : std::get<CompositeBody>(body).steps) jb["steps"].push_back(s);
        } else if (std::holds_alternative<OpaqueRecipe>(body)) {
            jb["mime_type"] = std::get<OpaqueRecipe>(body).mime_type;
            jb["content"] = std::get<OpaqueRecipe>(body).content;
        }
        j["body"] = jb;
        return j.dump();
    }
};

struct Execution : public MemoryObject {
    const UUID procedure_id;
    const std::vector<UUID> input_facts;
    const std::vector<UUID> output_facts;
    const ExecutionStatus status;
    const std::string error_log;
    const Duration elapsed_time;

    Execution(const UUID& id_, const AgentID& owner_, const Timestamp& recorded_at_,
              UUID procedure_id_, std::vector<UUID> input_facts_, std::vector<UUID> output_facts_,
              ExecutionStatus status_, std::string error_log_, Duration elapsed_time_)
        : MemoryObject(id_, owner_, recorded_at_)
        , procedure_id(std::move(procedure_id_))
        , input_facts(std::move(input_facts_))
        , output_facts(std::move(output_facts_))
        , status(status_)
        , error_log(std::move(error_log_))
        , elapsed_time(elapsed_time_)
    {}

    ObjectType type() const noexcept override { return ObjectType::Execution; }

    bool is_valid() const noexcept override {
        if (status == ExecutionStatus::Failure) {
            return !error_log.empty();
        }
        return true;
    }

    std::string serialize() const override {
        using nlohmann::json;
        json j;
        j["id"] = id;
        j["owner"] = owner;
        j["recorded_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(recorded_at.time_since_epoch()).count();
        j["type"] = static_cast<int>(type());
        j["procedure_id"] = procedure_id;
        j["input_facts"] = json::array();
        for (const auto& f : input_facts) j["input_facts"].push_back(f);
        j["output_facts"] = json::array();
        for (const auto& f : output_facts) j["output_facts"].push_back(f);
        j["status"] = static_cast<int>(status);
        j["error_log"] = error_log;
        j["elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_time).count();
        return j.dump();
    }
};

} // namespace om
