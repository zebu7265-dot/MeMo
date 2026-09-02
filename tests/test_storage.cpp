#include "core/declarative.hpp"
#include "core/procedural.hpp"
#include "core/memory_object.hpp"
#include "core/cold_storage.hpp"
#include "core/hot_cache.hpp"
#include "third_party/nlohmann-json/include_nlohmann_json.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace om;
using nlohmann::json;

int main() {
    // Fact
    UUID id1 = "fact-1";
    AgentID owner = "agent-A";
    Timestamp now = std::chrono::system_clock::now();
    TimeRange tr{now, now};
    Confidence conf{0.9, 0.01};
    Value val = std::string("hello");
    auto f = std::make_shared<Fact>(id1, owner, now, "entity1", "pred", val, tr, conf, std::nullopt);
    std::string payload = f->serialize();
    json wrapper;
    wrapper["type"] = static_cast<int>(f->type());
    wrapper["payload"] = json::parse(payload);
    wrapper["recorded_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(f->recorded_at.time_since_epoch()).count();
    wrapper["last_accessed"] = std::chrono::duration_cast<std::chrono::milliseconds>(f->last_accessed.time_since_epoch()).count();

    auto obj = MemoryObject::deserialize(wrapper.dump());
    assert(obj != nullptr);
    auto rf = std::dynamic_pointer_cast<const Fact>(obj);
    assert(rf != nullptr);
    assert(rf->entity == "entity1");
    assert(rf->predicate == "pred");

    // Event
    UUID id2 = "event-1";
    Value payload_val = std::string("evpayload");
    std::vector<UUID> involved = {id1};
    auto ev = std::make_shared<Event>(id2, owner, now, "act", involved, payload_val);
    json wrapper2;
    wrapper2["type"] = static_cast<int>(ev->type());
    wrapper2["payload"] = json::parse(ev->serialize());
    auto obj2 = MemoryObject::deserialize(wrapper2.dump());
    assert(obj2 != nullptr);
    auto rev = std::dynamic_pointer_cast<const Event>(obj2);
    assert(rev != nullptr);
    assert(rev->action == "act");
    assert(rev->involved_facts.size() == 1 && rev->involved_facts[0] == id1);

    // Belief
    UUID id3 = "belief-1";
    std::vector<UUID> evidence = {id2};
    auto b = std::make_shared<Belief>(id3, owner, now, "entityB", "predB", val, tr, conf, evidence, std::nullopt);
    json wrapper3;
    wrapper3["type"] = static_cast<int>(b->type());
    wrapper3["payload"] = json::parse(b->serialize());
    auto obj3 = MemoryObject::deserialize(wrapper3.dump());
    assert(obj3 != nullptr);
    auto rb = std::dynamic_pointer_cast<const Belief>(obj3);
    assert(rb != nullptr);
    assert(rb->evidence.size() == 1 && rb->evidence[0] == id2);

    // Procedure
    UUID pid = "proc-1";
    Contract c;
    c.input_schema["in"] = "string";
    c.output_schema["out"] = "int";
    c.preconditions.push_back("none");
    CompositeBody cb; cb.steps.push_back(id1); cb.steps.push_back(id2);
    Procedure p(pid, owner, now, "procName", "desc", c, ProcedureState::Active, std::nullopt, cb);
    json wrapper4;
    wrapper4["type"] = static_cast<int>(p.type());
    wrapper4["payload"] = json::parse(p.serialize());
    auto obj4 = MemoryObject::deserialize(wrapper4.dump());
    assert(obj4 != nullptr);
    auto rp = std::dynamic_pointer_cast<const Procedure>(obj4);
    assert(rp != nullptr);
    assert(rp->name == "procName");
    if (std::holds_alternative<CompositeBody>(rp->body)) {
        auto cb2 = std::get<CompositeBody>(rp->body);
        assert(cb2.steps.size() == 2);
    }

    // Execution
    UUID exid = "exec-1";
    std::vector<UUID> ins = {id1};
    std::vector<UUID> outs = {id3};
    Execution ex(exid, owner, now, pid, ins, outs, ExecutionStatus::Success, std::string(), std::chrono::milliseconds(123));
    json wrapper5;
    wrapper5["type"] = static_cast<int>(ex.type());
    wrapper5["payload"] = json::parse(ex.serialize());
    auto obj5 = MemoryObject::deserialize(wrapper5.dump());
    assert(obj5 != nullptr);
    auto rex = std::dynamic_pointer_cast<const Execution>(obj5);
    assert(rex != nullptr);
    assert(rex->procedure_id == pid);
    assert(rex->input_facts.size() == 1 && rex->input_facts[0] == id1);

    // Cold storage round trip and index rebuild
    const auto cold_path = std::filesystem::temp_directory_path() / "memo-storage-test";
    std::filesystem::remove_all(cold_path);
    {
        ColdStorage cold(cold_path.string());
        assert(cold.save(id1, *f));
        assert(cold.list_all_ids().size() == 1);
        auto loaded = cold.load(id1);
        assert(loaded != nullptr);
        auto loaded_fact = std::dynamic_pointer_cast<const Fact>(loaded);
        assert(loaded_fact && loaded_fact->entity == "entity1");
        assert(cold.total_size() > 0);
        assert(cold.remove(id1));
        assert(cold.list_all_ids().empty());
    }
    std::filesystem::remove_all(cold_path);

    // Hot cache LRU behavior
    HotCache cache(1);
    bool evicted = false;
    cache.put(f, [&](MemoryObjectPtr object) -> bool {
        evicted = object && object->id == id1;
        return true;
    });
    cache.put(ev, [&](MemoryObjectPtr object) -> bool {
        evicted = object && object->id == id1;
        return true;
    });
    assert(evicted);
    assert(cache.size() == 1);

    std::cout << "All tests passed\n";
    return 0;
}
