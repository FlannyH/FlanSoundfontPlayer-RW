#pragma once
#include <string>
#define N_VALUES 256

namespace Flan {
    struct ValuePool {
        char* value_names[256]{};
        uint64_t value_pool[256]{};
        size_t value_pool_idx = 0;

        // Get variable index from name
        size_t get_index_from_name(const std::string& name) {
            // If exists, return index
            for (int i = 0; i < N_VALUES; i++) {
                if (value_names[i] != nullptr && name == value_names[i]) {
                    return i;
                }
            }
            // Otherwise create new variable
            value_names[value_pool_idx] = new char[name.size() + 1];
            strcpy_s(value_names[value_pool_idx], name.size() + 1, name.c_str());
            value_pool[value_pool_idx] = 0;
            return value_pool_idx++;
        }

        // Get value from index
        template<typename T>
        T& get(const size_t index) {
            static_assert(sizeof(T) <= sizeof(value_pool[0]));
            return (T&)(value_pool[index]);
        }

        // Get value from name
        template<typename T>
        T& get(const std::string& name) {
            static_assert(sizeof(T) <= sizeof(value_pool[0]));
            return (T&)(value_pool[get_index_from_name(name)]);
        }

        // Set the current value
        template<typename T>
        void set_value(const std::string& name, T value) {
            static_assert(sizeof(T) <= sizeof(value_pool[0]));
            value_pool[get_index_from_name(name)] = *(uint64_t*)&value;
        }

        // Set the current pointer
        template<typename T>
        void set_ptr(const std::string& name, T* value) {
            value_pool[get_index_from_name(name)] = reinterpret_cast<uint64_t>(value);
        }

        // Bind a name to a value
        void bind(const std::string& name, const size_t index) {
            value_names[index] = new char[name.size() + 1];
            strcpy_s(value_names[index], name.size() + 1, name.c_str());
        }
    };

    enum class VarType {
        none,
        wstring,
        float64
    };

    struct Value {
        // Index into value pool
        size_t index{};
        VarType type{};
        bool has_changed = false;
        ValuePool& value_pool;

        // Assign a new value index
        Value(ValuePool& set_value_pool) : value_pool(set_value_pool) {
            index = value_pool.value_pool_idx++;
        }

        // Assign a new value index with a name
        Value(const std::string& name, const VarType var_type, ValuePool& set_value_pool) : value_pool(set_value_pool) {
            // If this name already exist, bind to that one
            index = value_pool.get_index_from_name(name);
            type = var_type;
        }

        // Get the current value
        template<typename T>
        T& get_as_ref() {
            static_assert(sizeof(T) <= sizeof(value_pool.value_pool[0]));
            return (T&)(value_pool.value_pool[index]);
        }
        template<typename T>
        T* get_as_ptr() {
            return reinterpret_cast<T*>(value_pool.value_pool[index]);
        }

        // Set the current value
        template<typename T>
        void set(T value) {
            static_assert(sizeof(T) <= sizeof(value_pool.value_pool[0]));
            get_as_ref<T>() = (T)value;
            has_changed = true;
        }
    };
}