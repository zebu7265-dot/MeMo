#pragma once
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <cstdint>
#include <cmath>

namespace om {

using UUID = std::string;
using AgentID = std::string;
using Timestamp = std::chrono::system_clock::time_point;
using Duration = std::chrono::system_clock::duration;

 // Recursive Value: C++17 allows containers of incomplete types here (vector<Value>, map<string,Value>)
using Value = std::variant<
    std::monostate,
    bool,
    int64_t,
    double,
    std::string,
    std::vector<Value>,
    std::unordered_map<std::string, Value>
>;

struct TimeRange {
    Timestamp valid_from;
    Timestamp valid_to;

    bool is_valid() const noexcept {
        return valid_from <= valid_to;
    }
};

struct Confidence {
    double mean;
    double variance;

    bool is_valid() const noexcept {
        return (variance >= 0.0) && std::isfinite(mean) && std::isfinite(variance);
    }
};

enum class ObjectType {
    Fact,
    Event,
    Belief,
    Procedure,
    Execution,
    Evaluation,
    Conflict,
    Pattern,
    Skill,
    Goal,
    MemoryPolicy
};

} // namespace om
