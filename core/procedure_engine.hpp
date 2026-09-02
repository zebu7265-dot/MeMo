#pragma once
#include "memory_graph.hpp"
#include "transaction.hpp"
#include "procedural.hpp"
#include <functional>
#include <unordered_map>
#include <shared_mutex>
#include <chrono>
#include <atomic>
#include <stdexcept>
#include <iostream>

namespace om {

using NativeFunction = std::function<std::vector<UUID>(const std::vector<UUID>& inputs, const MemoryGraph& graph, Transaction& tx)>;

class ProcedureEngine {
public:
    explicit ProcedureEngine(MemoryGraph& graph) : graph_(graph) {}

    void register_native(const std::string& name, NativeFunction fn) {
        std::unique_lock<std::shared_mutex> lock(reg_mutex_);
        registry_.emplace(name, std::move(fn));
    }

    struct InternalResult {
        ExecutionStatus status;
        std::string error;
        std::vector<UUID> outputs;
        Duration duration;
    };

    Execution execute(const UUID& procedure_id, const std::vector<UUID>& inputs) {
        Transaction tx = graph_.begin_transaction();
        auto res = execute_internal(procedure_id, inputs, tx, 0);

        // Build Execution object
        UUID exec_id = generate_uuid();
        Timestamp now = std::chrono::system_clock::now();
        Execution exec(exec_id, "engine", now, procedure_id, inputs, res.outputs, res.status, res.error, res.duration);

        // If success, try to stage and commit in main tx
        if (res.status == ExecutionStatus::Success) {
            try {
                tx.stage_existing(std::make_shared<const Execution>(exec));
                bool ok = tx.commit();
                if (ok) {
                    return exec;
                } else {
                    // commit conflict - convert exec to failure for audit, then fallback write
                    Execution exec_conflict(exec.id, exec.owner, exec.recorded_at, exec.procedure_id, {}, {}, ExecutionStatus::Failure,
                                            "Commit conflict: changes were not applied", exec.elapsed_time);
                    // fallback transaction
                    Transaction fallback = graph_.begin_transaction();
                    fallback.stage_existing(std::make_shared<const Execution>(exec_conflict));
                    if (!fallback.commit()) {
                        throw std::runtime_error("Failed to write execution log in fallback transaction");
                    }
                    return exec_conflict;
                }
            } catch (const std::exception& e) {
                // Any exception while staging/committing should lead to fallback log
                Execution exec_err(exec.id, exec.owner, exec.recorded_at, exec.procedure_id, {}, {}, ExecutionStatus::Failure,
                                   std::string("Exception during commit: ") + e.what(), exec.elapsed_time);
                Transaction fallback = graph_.begin_transaction();
                fallback.stage_existing(std::make_shared<const Execution>(exec_err));
                if (!fallback.commit()) {
                    throw std::runtime_error("Failed to write execution log in fallback transaction after exception");
                }
                return exec_err;
            }
        } else {
            // Not success: still must write audit log (fallback or same tx)
            Transaction fallback = graph_.begin_transaction();
            fallback.stage_existing(std::make_shared<const Execution>(exec));
            if (!fallback.commit()) {
                throw std::runtime_error("Failed to write execution log for failed execution");
            }
            return exec;
        }
    }

    static constexpr int MAX_DEPTH = 64;

private:
    MemoryGraph& graph_;
    std::unordered_map<std::string, NativeFunction> registry_;
    std::shared_mutex reg_mutex_;

    InternalResult execute_internal(const UUID& procedure_id, const std::vector<UUID>& inputs, Transaction& tx, int depth) {
        if (depth > MAX_DEPTH) {
            return {ExecutionStatus::Failure, "Max recursion depth exceeded", {}, Duration{}};
        }
        // read procedure
        auto proc_obj = tx.read(procedure_id);
        if (!proc_obj) {
            return {ExecutionStatus::Failure, "Procedure not found", {}, Duration{}};
        }
        auto proc = std::dynamic_pointer_cast<const Procedure>(proc_obj);
        if (!proc) {
            return {ExecutionStatus::Failure, "Object is not a procedure", {}, Duration{}};
        }
        if (proc->state != ProcedureState::Active) {
            return {ExecutionStatus::Failure, "Procedure not active", {}, Duration{}};
        }

        auto start = std::chrono::system_clock::now();
        InternalResult result;
        try {
            std::visit([&](auto&& body) {
                using T = std::decay_t<decltype(body)>;
                if constexpr (std::is_same_v<T, NativeBody>) {
                    result = run_native(body, inputs, tx);
                } else if constexpr (std::is_same_v<T, CompositeBody>) {
                    result = run_composite(body, inputs, tx, depth);
                } else if constexpr (std::is_same_v<T, OpaqueRecipe>) {
                    result = {ExecutionStatus::Failure, "OpaqueRecipe cannot be executed", {}, Duration{}};
                } else {
                    result = {ExecutionStatus::Failure, "Unknown body type", {}, Duration{}};
                }
            }, proc->body);
        } catch (const std::exception& e) {
            result = {ExecutionStatus::Failure, std::string("Exception during execute_internal: ") + e.what(), {}, Duration{}};
        }
        auto end = std::chrono::system_clock::now();
        result.duration = end - start;
        return result;
    }

    InternalResult run_native(const NativeBody& body, const std::vector<UUID>& inputs, Transaction& tx) {
        NativeFunction fn;
        {
            std::shared_lock<std::shared_mutex> lock(reg_mutex_);
            auto it = registry_.find(body.function_name);
            if (it == registry_.end()) {
                return {ExecutionStatus::Failure, "Native function not found: " + body.function_name, {}, Duration{}};
            }
            fn = it->second;
        }
        if (!fn) {
            return {ExecutionStatus::Failure, "Native function not found: " + body.function_name, {}, Duration{}};
        }
        try {
            auto start = std::chrono::system_clock::now();
            auto outputs = fn(inputs, graph_, tx);
            auto end = std::chrono::system_clock::now();
            return {ExecutionStatus::Success, "", outputs, end - start};
        } catch (const std::exception& e) {
            return {ExecutionStatus::Failure, std::string("Exception in native function: ") + e.what(), {}, Duration{}};
        }
    }

    InternalResult run_composite(const CompositeBody& body, const std::vector<UUID>& inputs, Transaction& tx, int depth) {
        std::vector<UUID> current_inputs = inputs;
        std::vector<UUID> accumulated_outputs;
        for (const auto& step_id : body.steps) {
            auto res = execute_internal(step_id, current_inputs, tx, depth + 1);
            if (res.status != ExecutionStatus::Success) {
                return res;
            }
            // next step inputs are outputs
            current_inputs = res.outputs;
            accumulated_outputs.insert(accumulated_outputs.end(), res.outputs.begin(), res.outputs.end());
        }
        return {ExecutionStatus::Success, "", accumulated_outputs, Duration{}};
    }

    static UUID generate_uuid() {
        static std::atomic<uint64_t> counter{1};
        return "uuid-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
    }
};

} // namespace om
