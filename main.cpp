#include "core/memory_graph.hpp"
#include "core/memory_graph_impl.hpp"
#include "core/transaction.hpp"
#include "core/declarative.hpp"
#include "core/procedural.hpp"
#include "core/procedure_engine.hpp"
#include <iostream>
#include <thread>
#include <optional>

using namespace om;

int main() {
    MemoryGraph graph;
    ProcedureEngine engine(graph);

    // Register a simple native function that creates a Fact and returns its id
    engine.register_native("add_test_fact", [](const std::vector<UUID>& inputs, const MemoryGraph& graph, Transaction& tx) -> std::vector<UUID> {
        // Create a fact with generated id
        static std::atomic<uint64_t> local_counter{1000};
        UUID fid = "fact-" + std::to_string(local_counter.fetch_add(1));
        Timestamp now = std::chrono::system_clock::now();
        TimeRange tr{now, now};
        Confidence c{0.5, 0.1};
        auto fact = std::make_shared<const Fact>(fid, "engine", now, "entity1", "predicate1", std::string("value"), tr, c, std::nullopt);
        tx.stage_existing(fact);
        return {fid};
    });

    // 1) Create a Fact in a transaction
    {
        Transaction tx = graph.begin_transaction();
        UUID fid = "fact-1";
        Timestamp now = std::chrono::system_clock::now();
        TimeRange tr{now, now};
        Confidence c{1.0, 0.0};
        auto f = tx.stage_create<Fact>(fid, "userA", now, "person:alice", "age", int64_t(30), tr, c, std::nullopt);
        bool ok = tx.commit();
        std::cout << "First commit ok: " << ok << "\n";
    }

    // 2) Attempt to create same ID in another transaction -> should fail on commit due to overwrite check
    {
        Transaction tx = graph.begin_transaction();
        UUID fid = "fact-1";
        Timestamp now = std::chrono::system_clock::now();
        TimeRange tr{now, now};
        Confidence c{1.0, 0.0};
        try {
            auto f = tx.stage_create<Fact>(fid, "userB", now, "person:alice", "age", int64_t(31), tr, c, std::nullopt);
            bool ok = tx.commit();
            std::cout << "Second commit (duplicate id) ok: " << ok << "\n";
        } catch (const std::exception& e) {
            std::cout << "Second transaction exception: " << e.what() << "\n";
        }
    }

    // 3) Create a Procedure (Active) that points to native function and execute it
    UUID proc_id = "proc-1";
    {
        Transaction tx = graph.begin_transaction();
        Timestamp now = std::chrono::system_clock::now();
        Contract c;
        NativeBody nb{"add_test_fact"};
        Procedure proc(proc_id, "engine", now, "make_fact", "Adds a test fact", c, ProcedureState::Active, std::nullopt, nb);
        tx.stage_existing(std::make_shared<const Procedure>(proc));
        bool ok = tx.commit();
        std::cout << "Procedure created: " << ok << "\n";
    }

    // Execute the procedure
    try {
        Execution exec = engine.execute(proc_id, {});
        std::cout << "Execution status: " << (exec.status == ExecutionStatus::Success ? "Success" : "Failure") << ", outputs: ";
        for (auto& o : exec.output_facts) std::cout << o << " ";
        std::cout << "\n";
    } catch (const std::exception& e) {
        std::cout << "Engine exception: " << e.what() << "\n";
    }

    // Query facts
    auto results = graph.find_facts(std::optional<std::string>("entity1"), std::optional<std::string>("predicate1"), std::nullopt);
    std::cout << "find_facts found: " << results.size() << " facts\n";

    // Validate invariants
    bool inv = graph.validate_global_invariants();
    std::cout << "Global invariants valid: " << inv << "\n";

    return 0;
}
