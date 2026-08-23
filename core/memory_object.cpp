#include "third_party/nlohmann-json/include_nlohmann_json.hpp"
#include "core/memory_object.hpp"
#include "core/declarative.hpp"
#include "core/procedural.hpp"
#include <chrono>
#include <sstream>

namespace om {

using nlohmann::json;

// Helpers to parse Value from JSON
static Value json_to_value(const json& j) {
    if (j.is_null()) return std::monostate{};
    if (j.is_boolean()) return j.get<bool>();
    if (j.is_number_integer()) return (int64_t)j.get<int64_t>();
    if (j.is_number_float()) return j.get<double>();
    if (j.is_string()) return j.get<std::string>();
    if (j.is_array()) {
        std::vector<std::string> arr;
        arr.reserve(j.size());
        for (const auto& e : j) arr.push_back(e.dump());
        return arr;
    }
    if (j.is_object()) {
        std::unordered_map<std::string, std::string> m;
        for (auto it = j.begin(); it != j.end(); ++it) {
            m[it.key()] = it.value().dump();
        }
        return m;
    }
    return std::monostate{};
}

static Timestamp json_to_timestamp_ms(const json& j) {
    if (j.is_number_integer()) {
        auto ms = j.get<int64_t>();
        return Timestamp(std::chrono::system_clock::time_point(std::chrono::milliseconds(ms)));
    }
    // fallback: now
    return std::chrono::system_clock::now();
}

std::shared_ptr<const MemoryObject> MemoryObject::deserialize(const std::string& data) {
    try {
        json payload = json::parse(data);
        // Determine type if present
        if (payload.contains("type") && payload["type"].is_number_integer()) {
            ObjectType ot = static_cast<ObjectType>(payload["type"].get<int>());
            json body = payload.contains("payload") ? payload["payload"] : payload;
            switch (ot) {
                case ObjectType::Fact: {
                    UUID id = body.at("id").get<std::string>();
                    AgentID owner = body.at("owner").get<std::string>();
                    Timestamp recorded_at = json_to_timestamp_ms(body.at("recorded_at"));
                    std::string entity = body.at("entity").get<std::string>();
                    std::string predicate = body.at("predicate").get<std::string>();
                    Value value = json_to_value(body.at("value"));
                    TimeRange tr{json_to_timestamp_ms(body.at("validity")["from"]), json_to_timestamp_ms(body.at("validity")["to"])};
                    Confidence conf{body.at("confidence")["mean"].get<double>(), body.at("confidence")["variance"].get<double>()};
                    std::optional<UUID> sup = std::nullopt;
                    if (body.contains("superseded_by") && !body["superseded_by"].is_null()) sup = body["superseded_by"].get<std::string>();
                    auto f = std::make_shared<Fact>(id, owner, recorded_at, entity, predicate, value, tr, conf, sup);
                    f->serialized_size = data.size();
                    f->last_accessed = std::chrono::system_clock::now();
                    return f;
                }
                case ObjectType::Belief: {
                    UUID id = body.at("id").get<std::string>();
                    AgentID owner = body.at("owner").get<std::string>();
                    Timestamp recorded_at = json_to_timestamp_ms(body.at("recorded_at"));
                    std::string entity = body.at("entity").get<std::string>();
                    std::string predicate = body.at("predicate").get<std::string>();
                    Value value = json_to_value(body.at("value"));
                    TimeRange tr{json_to_timestamp_ms(body.at("validity")["from"]), json_to_timestamp_ms(body.at("validity")["to"])};
                    Confidence conf{body.at("confidence")["mean"].get<double>(), body.at("confidence")["variance"].get<double>()};
                    std::vector<UUID> evidence;
                    if (body.contains("evidence") && body["evidence"].is_array()) {
                        for (auto& e : body["evidence"]) evidence.push_back(e.get<std::string>());
                    }
                    std::optional<UUID> sup = std::nullopt;
                    if (body.contains("superseded_by") && !body["superseded_by"].is_null()) sup = body["superseded_by"].get<std::string>();
                    auto b = std::make_shared<Belief>(id, owner, recorded_at, entity, predicate, value, tr, conf, evidence, sup);
                    b->serialized_size = data.size();
                    b->last_accessed = std::chrono::system_clock::now();
                    return b;
                }
                case ObjectType::Event: {
                    UUID id = body.at("id").get<std::string>();
                    AgentID owner = body.at("owner").get<std::string>();
                    Timestamp recorded_at = json_to_timestamp_ms(body.at("recorded_at"));
                    std::string action = body.at("action").get<std::string>();
                    std::vector<UUID> involved;
                    if (body.contains("involved_facts") && body["involved_facts"].is_array()) {
                        for (auto& e : body["involved_facts"]) involved.push_back(e.get<std::string>());
                    }
                    Value payload_val = json_to_value(body.at("payload"));
                    auto ev = std::make_shared<Event>(id, owner, recorded_at, action, involved, payload_val);
                    ev->serialized_size = data.size();
                    ev->last_accessed = std::chrono::system_clock::now();
                    return ev;
                }
                case ObjectType::Procedure: {
                    UUID id = body.at("id").get<std::string>();
                    AgentID owner = body.at("owner").get<std::string>();
                    Timestamp recorded_at = json_to_timestamp_ms(body.at("recorded_at"));
                    std::string name = body.at("name").get<std::string>();
                    std::string desc = body.at("description").get<std::string>();
                    Contract contract{};
                    if (body.contains("contract")) {
                        auto jc = body["contract"];
                        if (jc.contains("input_schema")) for (auto it = jc["input_schema"].begin(); it != jc["input_schema"].end(); ++it) contract.input_schema[it.key()] = it.value().get<std::string>();
                        if (jc.contains("output_schema")) for (auto it = jc["output_schema"].begin(); it != jc["output_schema"].end(); ++it) contract.output_schema[it.key()] = it.value().get<std::string>();
                        if (jc.contains("preconditions") && jc["preconditions"].is_array()) for (auto& s : jc["preconditions"]) contract.preconditions.push_back(s.get<std::string>());
                    }
                    ProcedureState state = ProcedureState::Draft;
                    if (body.contains("state")) state = static_cast<ProcedureState>(body["state"].get<int>());
                    std::optional<UUID> sup = std::nullopt;
                    if (body.contains("superseded_by") && !body["superseded_by"].is_null()) sup = body["superseded_by"].get<std::string>();
                    // parse body variant
                    std::variant<NativeBody, CompositeBody, OpaqueRecipe> vb;
                    if (body.contains("body")) {
                        auto jb = body["body"];
                        if (jb.contains("function_name")) vb = NativeBody{jb["function_name"].get<std::string>()};
                        else if (jb.contains("steps") && jb["steps"].is_array()) {
                            CompositeBody cb; for (auto& s : jb["steps"]) cb.steps.push_back(s.get<std::string>()); vb = cb;
                        } else if (jb.contains("mime_type") && jb.contains("content")) vb = OpaqueRecipe{jb["mime_type"].get<std::string>(), jb["content"].get<std::string>()};
                    }
                    auto p = std::make_shared<Procedure>(id, owner, recorded_at, name, desc, contract, state, sup, vb);
                    p->serialized_size = data.size();
                    p->last_accessed = std::chrono::system_clock::now();
                    return p;
                }
                case ObjectType::Execution: {
                    UUID id = body.at("id").get<std::string>();
                    AgentID owner = body.at("owner").get<std::string>();
                    Timestamp recorded_at = json_to_timestamp_ms(body.at("recorded_at"));
                    UUID proc = body.at("procedure_id").get<std::string>();
                    std::vector<UUID> ins, outs;
                    if (body.contains("input_facts") && body["input_facts"].is_array()) for (auto& s : body["input_facts"]) ins.push_back(s.get<std::string>());
                    if (body.contains("output_facts") && body["output_facts"].is_array()) for (auto& s : body["output_facts"]) outs.push_back(s.get<std::string>());
                    ExecutionStatus status = ExecutionStatus::Success;
                    if (body.contains("status")) status = static_cast<ExecutionStatus>(body["status"].get<int>());
                    std::string error_log = body.value("error_log", std::string());
                    Duration elapsed = Duration(std::chrono::milliseconds(body.value("elapsed_ms", 0)));
                    auto ex = std::make_shared<Execution>(id, owner, recorded_at, proc, ins, outs, status, error_log, elapsed);
                    ex->serialized_size = data.size();
                    ex->last_accessed = std::chrono::system_clock::now();
                    return ex;
                }
                default:
                    return nullptr;
            }
        }
    } catch (const std::exception& ex) {
        // parse or construction error
        (void)ex;
        return nullptr;
    }
    return nullptr;
}

} // namespace om
