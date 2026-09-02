#include "core/cold_storage.hpp"
#include "core/memory_object.hpp"
#include "third_party/nlohmann-json/include_nlohmann_json.hpp"
#include <filesystem>
#include <fstream>
#include <system_error>
#include <iostream>
#include <stdexcept>
#include <mutex>

namespace om {

namespace fs = std::filesystem;
using nlohmann::json;

static std::string safe_filename_for(const UUID& id) {
    if (id.empty() || id == "." || id == ".." ||
        id.find('/') != std::string::npos ||
        id.find('\\') != std::string::npos) {
        throw std::invalid_argument("Invalid object id for cold storage");
    }
    return id + ".cold.json";
}

ColdStorage::ColdStorage(std::string base_path)
: base_path_(std::move(base_path)) {
    if (!fs::exists(base_path_)) fs::create_directories(base_path_);
    rebuild_indexes();
}

std::string ColdStorage::make_filename(const UUID& id) const {
    return (fs::path(base_path_) / safe_filename_for(id)).string();
}

std::string ColdStorage::make_tmp_filename(const UUID& id) const {
    return make_filename(id) + ".tmp";
}

bool ColdStorage::write_temp(const UUID& id, const std::string& wrapper_json) {
    std::ofstream ofs(make_tmp_filename(id), std::ios::binary | std::ios::trunc);
    if (!ofs) throw std::runtime_error("ColdStorage: cannot open tmp file for write_temp: " + make_tmp_filename(id));
    ofs << wrapper_json;
    ofs.flush();
    if (!ofs) throw std::runtime_error("ColdStorage: failed to write tmp file for " + id);
    return true;
}

bool ColdStorage::promote_temp(const UUID& id) {
    std::error_code ec;
    fs::rename(make_tmp_filename(id), make_filename(id), ec);
    if (ec) throw std::runtime_error(std::string("ColdStorage: rename failed in promote_temp for ") + id + ": " + ec.message());
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        file_map_[id] = fs::path(make_filename(id)).filename().string();
        persist_index_nolock();
    }
    return true;
}

void ColdStorage::persist_index_nolock() {
    auto tmp = fs::path(base_path_) / "index.json.tmp";
    auto finalp = fs::path(base_path_) / "index.json";
    std::ofstream ofs(tmp.string(), std::ios::binary | std::ios::trunc);
    if (!ofs) {
        std::cerr << "ColdStorage: cannot write index tmp\n";
        return;
    }
    json j;
    for (const auto& kv : file_map_) {
        j[kv.first] = kv.second;
    }
    ofs << j.dump(2) << "\n";
    ofs.flush(); ofs.close();
    std::error_code ec;
    fs::rename(tmp, finalp, ec);
    if (ec) std::cerr << "ColdStorage: failed to rename index tmp: " << ec.message() << "\n";
}

bool ColdStorage::save(const UUID& id, const MemoryObject& obj) {
    // Keep existing save for compatibility, but prefer save_with_version for correct MVCC writes.
    json wrapper;
    wrapper["version"] = 1;
    wrapper["type"] = static_cast<int>(obj.type());
    wrapper["recorded_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(obj.recorded_at.time_since_epoch()).count();
    wrapper["last_accessed"] = std::chrono::duration_cast<std::chrono::milliseconds>(obj.last_accessed.time_since_epoch()).count();
    try {
        wrapper["payload"] = json::parse(obj.serialize());
    } catch (...) {
        wrapper["payload"] = obj.serialize();
    }
    std::string tmp_path = make_tmp_filename(id);
    std::string final_path = make_filename(id);
    std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        std::cerr << "ColdStorage: cannot open tmp file for " << id << "\n";
        return false;
    }
    ofs << wrapper.dump(2) << "\n";
    ofs.flush(); ofs.close();
    std::error_code ec;
    fs::rename(tmp_path, final_path, ec);
    if (ec) {
        std::cerr << "ColdStorage: rename failed for " << id << ": " << ec.message() << "\n";
        std::error_code ec2; fs::remove(tmp_path, ec2);
        return false;
    }
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        file_map_[id] = fs::path(final_path).filename().string();
        persist_index_nolock();
    }
    return true;
}

void ColdStorage::write_final(const UUID& id, const std::string& wrapper_json) {
    std::string tmp_final = make_tmp_filename(id) + ".finaltmp";
    std::ofstream ofs(tmp_final, std::ios::binary | std::ios::trunc);
    if (!ofs) throw std::runtime_error("ColdStorage: cannot open final tmp for " + id);
    ofs << wrapper_json;
    ofs.flush(); ofs.close();
    std::error_code ec;
    fs::rename(tmp_final, make_filename(id), ec);
    if (ec) {
        std::error_code ec2;
        fs::remove(tmp_final, ec2);
        throw std::runtime_error(std::string("ColdStorage: rename final failed for ") + id + ": " + ec.message());
    }
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        file_map_[id] = fs::path(make_filename(id)).filename().string();
        persist_index_nolock();
    }
}

void ColdStorage::save_with_version(const UUID& id, const MemoryObject& obj, uint64_t version) {
    json wrapper;
    wrapper["version"] = version;
    wrapper["type"] = static_cast<int>(obj.type());
    wrapper["recorded_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(obj.recorded_at.time_since_epoch()).count();
    wrapper["last_accessed"] = std::chrono::duration_cast<std::chrono::milliseconds>(obj.last_accessed.time_since_epoch()).count();
    try {
        wrapper["payload"] = json::parse(obj.serialize());
    } catch (...) {
        wrapper["payload"] = obj.serialize();
    }

    std::string tmp_final = make_tmp_filename(id) + ".finaltmp";
    std::ofstream ofs(tmp_final, std::ios::binary | std::ios::trunc);
    if (!ofs) throw std::runtime_error("ColdStorage: cannot open final tmp for save_with_version: " + tmp_final);
    ofs << wrapper.dump(2) << "\n";
    ofs.flush(); ofs.close();

    std::error_code ec;
    fs::rename(tmp_final, make_filename(id), ec);
    if (ec) {
        std::error_code ec2; fs::remove(tmp_final, ec2);
        throw std::runtime_error(std::string("ColdStorage: rename final failed for save_with_version ") + id + ": " + ec.message());
    }
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        file_map_[id] = fs::path(make_filename(id)).filename().string();
        persist_index_nolock();
    }
}

std::optional<StoredObject> ColdStorage::load(const UUID& id) {
    std::string path = make_filename(id);
    if (!fs::exists(path)) return std::nullopt;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    json wrapper = json::parse(content);
    // Fallback to 1 for legacy files; explicit version persisted by newer code will be used
    uint64_t file_version = wrapper.value("version", 1u);
    if (!wrapper.contains("payload")) return std::nullopt;
    auto payload = wrapper["payload"].dump();
    auto obj = MemoryObject::deserialize(payload);
    if (obj) {
        obj->last_accessed = std::chrono::system_clock::now();
        if (wrapper.contains("last_accessed") && wrapper["last_accessed"].is_number_integer()) {
            obj->last_accessed = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(wrapper["last_accessed"].get<int64_t>()));
        }
    }
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        file_map_[id] = fs::path(path).filename().string();
    }
    if (obj) return std::optional<StoredObject>{StoredObject{obj, file_version}};
    return std::nullopt;
}

bool ColdStorage::remove(const UUID& id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = file_map_.find(id);
    if (it == file_map_.end()) return true;
    auto path = (fs::path(base_path_) / it->second);
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        std::cerr << "ColdStorage: remove failed for " << id << ": " << ec.message() << "\n";
        return false;
    }
    file_map_.erase(it);
    persist_index_nolock();
    return true;
}

std::vector<UUID> ColdStorage::list_all_ids() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<UUID> out;
    out.reserve(file_map_.size());
    for (const auto& kv : file_map_) out.push_back(kv.first);
    return out;
}

std::vector<UUID> ColdStorage::find_by_entity(const std::string& entity) const {
    std::vector<UUID> out;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& kv : file_map_) {
        auto path = (fs::path(base_path_) / kv.second).string();
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) continue;
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        auto pos = content.find("\"entity\"");
        if (pos == std::string::npos) continue;
        auto quote_pos = content.find('"', pos + 8);
        if (quote_pos == std::string::npos) continue;
        auto quote_pos2 = content.find('"', quote_pos + 1);
        if (quote_pos2 == std::string::npos) continue;
        std::string val = content.substr(quote_pos + 1, quote_pos2 - quote_pos - 1);
        if (val == entity) out.push_back(kv.first);
    }
    return out;
}

size_t ColdStorage::total_size() const {
    size_t total = 0;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& kv : file_map_) {
        auto path = (fs::path(base_path_) / kv.second);
        std::error_code ec;
        auto s = fs::file_size(path, ec);
        if (!ec) total += s;
    }
    return total;
}

void ColdStorage::rebuild_indexes() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    file_map_.clear();
    for (auto& p : fs::directory_iterator(base_path_)) {
        if (!p.is_regular_file()) continue;
        auto fname = p.path().filename().string();
        constexpr size_t suffix_len = 10; // ".cold.json"
        if (fname.size() > suffix_len &&
            fname.compare(fname.size() - suffix_len, suffix_len, ".cold.json") == 0) {
            std::string id = fname.substr(0, fname.size() - suffix_len);
            file_map_[id] = fname;
        }
    }
    persist_index_nolock();
}

} // namespace om
