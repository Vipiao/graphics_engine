#pragma once
#include <vector>

/**
 * IPointerStorage - A simple interface for storing and retrieving typed pointers
 * 
 * USAGE:
 * 1. Inherit from this class: class MyClass : public IPointerStorage
 * 2. Store pointers: set_pointer<MyType>(&my_object)
 * 3. Get pointers back: MyType* ptr = get_pointer<MyType>()
 * 
 * EXAMPLE:
 *   class GameSystem : public IPointerStorage {};
 *   
 *   GameSystem system;
 *   Player player;
 *   Enemy enemy;
 *   
 *   // Store objects
 *   system.set_pointer<Player>(&player);
 *   system.set_pointer<Enemy>(&enemy);
 *   
 *   // Get them back later
 *   Player* p = system.get_pointer<Player>();  // Returns &player
 *   Enemy* e = system.get_pointer<Enemy>();    // Returns &enemy
 * 
 * NOTE: Each type gets its own storage slot automatically. No coordination needed!
 */

class IPointerStorage {
private:
    std::vector<void*> pointers;
    static inline int next_type_id = 0;  // C++17+ inline static (header-only)
    
public:
    template<typename T>
    static int get_type_id() {
        static int id = next_type_id++;
        return id;
    }
    
    template<typename T>
    void set_pointer(T* ptr) {
        int id = get_type_id<T>();
        if (static_cast<size_t>(id) >= pointers.size()) {
            pointers.resize(id + 1, nullptr);
        }
        pointers[id] = ptr;
    }
    
    template<typename T>
    T* get_pointer() {
        int id = get_type_id<T>();
        if (static_cast<size_t>(id) >= pointers.size()) {
            return nullptr;
        }
        return static_cast<T*>(pointers[id]);
    }
    
    template<typename T>
    bool has_pointer() const {
        int id = get_type_id<T>();
        return id < pointers.size() && pointers[id] != nullptr;
    }
    
    template<typename T>
    void remove_pointer() {
        int id = get_type_id<T>();
        if (static_cast<size_t>(id) < pointers.size()) {
            pointers[id] = nullptr;
        }
    }
    
    // Virtual destructor for proper inheritance
    virtual ~IPointerStorage() = default;
};