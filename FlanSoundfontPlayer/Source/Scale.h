#pragma once
#include <string>
#include <iostream>

namespace Flan {
    class Scale {
    private:
        double m_note_values[128]{}; // Array that maps input midi keys to an output pitch multiplier, where 1.0f corresponds to midi key 60
        std::string m_description;
        bool m_is_default = true;

        // Checks whether the currently loaded scale is 12 tone equal temperament
        [[nodiscard]] bool is_current_scale_default() const;
    public:
        Scale() {
            init_default();
        }

        explicit Scale(const std::string& path);

        double operator[](size_t index) const;

        // Loads a scale from a .scl file
        bool from_file(const std::string& path);

        // Initializes a default scale using the 12-TET scale
        void init_default();

        // Returns whether this current scale is 12 tone equal temperament
        [[nodiscard]] bool is_default() const { return m_is_default; }

        // Returns the description of the scale as specified in the loaded file
        std::string description() { return m_description; }
    };
}