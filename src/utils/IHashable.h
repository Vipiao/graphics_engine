// IHashable.h
#pragma once

#include <cstddef>

/**
 * @brief Interface for objects that can provide deterministic hash values
 * Used for determinism checking and debugging
 */
class IHashable {
public:
    virtual ~IHashable() = default;
    virtual size_t computeHash() const = 0;
};