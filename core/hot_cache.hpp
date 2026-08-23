#pragma once
#include "memory_object.hpp"
#include <unordered_map>
#include <list>
#include <shared_mutex>
#include <functional>
#include <vector>
#include <mutex>

namespace om {

class HotCache {
public:
    using EvictCallback = std::function<void(MemoryObjectPtr)>;

    explicit HotCache(size_t max_size = 1024) : max_size_(max_size) {}

    // Return object if present in cache, nullptr otherwise.
    MemoryObjectPtr get(const UUID& id) const {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(id);
        if (it == cache_.end()) return nullptr;
        // move to front (most recent)
        auto lit_it = lru_iterators_.find(id);
        if (lit_it != lru_iterators_.end()) {
            lru_list_.erase(lit_it->second);
        }
        lru_list_.push_front(id);
        lru_iterators_[id] = lru_list_.begin();
        // update last_accessed
        it->second->last_accessed = std::chrono::system_clock::now();
        return it->second;
    }

    // Put object into cache. If insertion causes eviction, on_evicted will be called for each evicted object (outside lock).
    void put(MemoryObjectPtr obj, EvictCallback on_evicted = nullptr) {
        if (!obj) return;
        std::vector<MemoryObjectPtr> evicted;
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            const UUID& id = obj->id;
            auto it = cache_.find(id);
            if (it != cache_.end()) {
                // replace existing pointer (objects are immutable but pointer may differ)
                it->second = obj;
                // move to front
                auto lit_it = lru_iterators_.find(id);
                if (lit_it != lru_iterators_.end()) {
                    lru_list_.erase(lit_it->second);
                }
                lru_list_.push_front(id);
                lru_iterators_[id] = lru_list_.begin();
            } else {
                cache_.emplace(id, obj);
                lru_list_.push_front(id);
                lru_iterators_[id] = lru_list_.begin();
            }
            // update last_accessed
            obj->last_accessed = std::chrono::system_clock::now();

            // evict if needed and collect evicted objects to call callback outside lock
            while (cache_.size() > max_size_) {
                // remove least-recent (back)
                auto tail_it = lru_list_.end();
                --tail_it;
                UUID tail_id = *tail_it;
                auto cit = cache_.find(tail_id);
                if (cit != cache_.end()) {
                    evicted.push_back(cit->second);
                    cache_.erase(cit);
                }
                // remove iterator
                lru_iterators_.erase(tail_id);
                lru_list_.pop_back();
            }
        }
        // call callbacks outside lock
        if (on_evicted) {
            for (auto& e : evicted) {
                try {
                    on_evicted(e);
                } catch (...) {
                    // swallow exceptions from callback to avoid unexpected crashes in cache
                }
            }
        }
    }

    void remove(const UUID& id) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(id);
        if (it != cache_.end()) cache_.erase(it);
        auto lit = lru_iterators_.find(id);
        if (lit != lru_iterators_.end()) {
            lru_list_.erase(lit->second);
            lru_iterators_.erase(lit);
        }
    }

    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_.size();
    }

    void set_max_size(size_t s) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        max_size_ = s;
    }

private:
    mutable std::shared_mutex mutex_;
    mutable std::unordered_map<UUID, MemoryObjectPtr> cache_;
    mutable std::list<UUID> lru_list_; // front = most recent
    mutable std::unordered_map<UUID, std::list<UUID>::iterator> lru_iterators_;
    size_t max_size_{1024};
};

} // namespace om
