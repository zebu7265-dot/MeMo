*** Begin Patch
*** Update File: core/memory_graph_impl.hpp
@@
-        if (from_cold) {
-        // put into hot; ensure eviction persists to cold
-        hot_.put(from_cold, [&](MemoryObjectPtr ev) -> bool {
-            // Eviction callback - persist evicted object to cold storage
-            try { return cold_.save(ev->id, *ev); } catch (...) { return false; }
-        });
-        return from_cold;
-    }
+    if (from_cold) {
+        // Insert loaded StoredObject into graph under lock and put into hot while holding graph lock
+        std::unique_lock<std::shared_mutex> lock(mutex_);
+        auto it2 = objects_.find(id);
+        if (it2 == objects_.end()) {
+            // 'from_cold' is a StoredObject-like; emplace into objects_
+            objects_.emplace(id, *from_cold);
+            reindex_object(from_cold->obj);
+            // Eviction callback must persist authoritative version from objects_
+            hot_.put(from_cold->obj, [&](MemoryObjectPtr ev) -> bool {
+                auto git = objects_.find(ev->id);
+                if (git == objects_.end()) {
+                    throw std::runtime_error("Cold eviction: object not found in graph when persisting to cold: " + ev->id);
+                }
+                uint64_t authoritative_version = git->second.version;
+                // Persist with explicit version
+                save_with_version(ev->id, *ev, authoritative_version);
+                return true;
+            });
+            return from_cold->obj;
+        } else {
+            return it2->second.obj;
+        }
+    }
*** End Patch
