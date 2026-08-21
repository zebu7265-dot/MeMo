#pragma once
#include "memory_object.hpp"
#include "types.hpp"
#include <string>
#include <variant>
#include <vector>
#include <optional>
#include <unordered_map>
#include <functional>

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
};

} // namespace om
