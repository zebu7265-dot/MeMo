#pragma once
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <memory>

namespace om {

using UUID = std::string;
using AgentID = std::string;
using Timestamp = std::chrono::system_clock::time_point;
using Duration = std::chrono::system_clock::duration;

// Value: recursive, indirected representation using shared_ptr for containers.
// This avoids C++17 recursive variant instantiation issues while allowing nested
// heterogeneous arrays/objects.
struct Value {
    using array_t = std::shared_ptr<std::vector<std::shared_ptr<Value>>>;
    using object_t = std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<Value>>>;
    using variant_t = std::variant<
        std::monostate,
        bool,
        int64_t,
        double,
        std::string,
        array_t,
        object_t
    >;

    variant_t v;

    Value() noexcept : v(std::monostate{}) {}
    Value(std::nullptr_t) noexcept : v(std::monostate{}) {}
    Value(bool b) noexcept : v(b) {}
    Value(int64_t i) noexcept : v(i) {}
    Value(double d) noexcept : v(d) {}
    Value(const char* s) : v(std::string(s)) {}
    Value(const std::string& s) : v(s) {}
    Value(std::string&& s) : v(std::move(s)) {}

    // Factory helpers for array/object
    static array_t make_array() { return std::make_shared<std::vector<std::shared_ptr<Value>>>(); }
    static object_t make_object() { return std::make_shared<std::unordered_map<std::string, std::shared_ptr<Value>>>(); }
    static Value from_array(const std::vector<std::shared_ptr<Value>>& arr) { return Value(array_t(std::make_shared<std::vector<std::shared_ptr<Value>>>(arr))); }
    static Value from_object(const std::unordered_map<std::string, std::shared_ptr<Value>>& obj) { return Value(object_t(std::make_shared<std::unordered_map<std::string, std::shared_ptr<Value>>>(obj))); }

    bool is_null() const noexcept { return std::holds_alternative<std::monostate>(v); }
    bool is_bool() const noexcept { return std::holds_alternative<bool>(v); }
    bool is_int() const noexcept { return std::holds_alternative<int64_t>(v); }
    bool is_double() const noexcept { return std::holds_alternative<double>(v); }
    bool is_string() const noexcept { return std::holds_alternative<std::string>(v); }
    bool is_array() const noexcept { return std::holds_alternative<array_t>(v); }
    bool is_object() const noexcept { return std::holds_alternative<object_t>(v); }

    bool as_bool() const { return std::get<bool>(v); }
    int64_t as_int() const { return std::get<int64_t>(v); }
    double as_double() const { return std::get<double>(v); }
    const std::string& as_string() const { return std::get<std::string>(v); }
    array_t as_array() const { return std::get<array_t>(v); }
    object_t as_object() const { return std::get<object_t>(v); }

private:
    // private ctors for array/object wrappers
    explicit Value(array_t a) : v(std::move(a)) {}
    explicit Value(object_t o) : v(std::move(o)) {}
};

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
