#pragma once
#include <string>
#include <fstream>
#include <iostream>
#include <vector>

namespace Flan {
    class Scale {
    private:
        double m_note_values[128]{}; // Array that maps input midi keys to an output pitch multiplier, where 1.0f corresponds to midi key 60
        std::string m_description;
    public:
        Scale() {
            init_default();
        }

        explicit Scale(const std::string& path) {
            from_file(path);
        }
        double operator[](const size_t index) const {
            return m_note_values[index];
        }
        bool from_file(const std::string& path) {
            std::ifstream file;
            file.open(path);
            if (!file) {
                return false;
            }

            int n_notes_in_scale = -1;
            int line_number = 0;
            std::vector<double> note_intervals;
            note_intervals.push_back(0.0);

            while (true) {
                std::string line;
                getline(file, line);

                if (line.empty()) {
                    break;
                }

                // Remove spaces, tabs, and eventual
                std::string filtered_line = line;

                auto s_end = filtered_line.end();
                s_end = remove(filtered_line.begin(), s_end, ' ');
                s_end = remove(filtered_line.begin(), s_end, '\t');
                s_end = remove(filtered_line.begin(), s_end, '\r');
                s_end = remove(filtered_line.begin(), s_end, '\n');

                filtered_line.resize(s_end - filtered_line.begin());

                // Ignore empty lines
                if (filtered_line.empty()) {
                    continue;
                }

                // Ignore comments
                if (filtered_line[0] == '!') {
                    continue;
                }

                // Store description
                if (line_number == 0) {
                    m_description = line;
                    line_number++;
                    continue;
                }

                // Get number of notes in scale
                else if (line_number == 1) {
                    n_notes_in_scale = std::stoi(filtered_line);
                    line_number++;
                    continue;
                }

                // Handle note values
                else {
                    // If string contains a slash, it's a ratio
                    size_t slash_pos = filtered_line.find('/');
                    if (slash_pos != std::string::npos) {
                        // Split string into 2 parts; before and after the slash
                        std::string dividend = filtered_line.substr(0, slash_pos);
                        std::string divisor = filtered_line.substr(slash_pos + 1);

                        // Divide the 2 numbers and push it to the array
                        double interval = std::stod(dividend) / std::stod(divisor);
                        note_intervals.push_back(interval);
                        line_number++;
                        continue;
                    }

                    // Otherwise, if the string contains a period, it's a value in cents
                    size_t period_pos = filtered_line.find('.');
                    if (period_pos != std::string::npos) {
                        double cents = std::stod(filtered_line);
                        double interval = pow(2.0, cents / 1200.0);
                        note_intervals.push_back(interval);
                        line_number++;
                        continue;
                    }

                    // Finally, if it's just an integer, it's a multiplier
                    {
                        double interval = std::stod(filtered_line);
                        note_intervals.push_back(interval);
                        line_number++;
                        continue;
                    }
                }
            }

            // Turn the list of ratios into a proper lookup table
            m_note_values[60] = 1.0;

            // First, let's go from 61 to 127
            for (int i = 61; i <= 127; i++) {
                // Get indices for interval and base
                int index_interval = (i - 60) % n_notes_in_scale;
                if (index_interval == 0) {
                    index_interval = n_notes_in_scale;
                }
                int index_base = i - index_interval;

                // Get the values for indices
                double interval = note_intervals[index_interval];
                double base = m_note_values[index_base];

                // Calculate pitch multiplier
                double multiplier = base * interval;
                m_note_values[i] = multiplier;
            }

            // Then, let's go from 59 to 0
            for (int i = 59; i >= 0; i--) {
                // Get indices for interval and base
                int index_interval = (i - 60 + (128 * n_notes_in_scale)) % n_notes_in_scale;
                int index_base = i - index_interval + n_notes_in_scale;

                // Get the values for indices
                double interval = note_intervals[n_notes_in_scale - index_interval];
                double base = m_note_values[index_base];

                // Calculate pitch multiplier
                double multiplier = base / interval;
                m_note_values[i] = multiplier;
            }

            return true;
        }

        void init_default() { // Initializes a default scale using the 12-TET scale
            for (int i = 0; i < 128; i++) {
                m_note_values[i] = pow(2.0, static_cast<double>(i - 60) / 12.0);
            }
        }

        std::string description() {
            return m_description;
        }
    };
}