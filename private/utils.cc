/**
 * (c) Chad Walker, Chris Kirmse
 */

#include "utils.h"

bool Avalanche::stringEndsWith(const std::string &s, const std::string &suffix) {
    if (suffix.size() > s.size()) {
        return false;
    }
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
