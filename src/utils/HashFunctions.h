// HashFunctions.h
#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <utility>
#include <glm/gtc/quaternion.hpp>
#include <functional>

class Hash {
public:
    // Compile-time string hash using FNV-1a algorithm
    static constexpr int hashColliderName(const char* name) {
        unsigned int hash = 2166136261u;
        while (*name) {
            hash ^= *name++;
            hash *= 16777619;
        }
        return static_cast<int>(hash);
    }

    // General hash combining function
    static inline size_t combineHashes(size_t hash1, size_t hash2) {
        return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
    }

    // PCG hash functions
    static double toUnit(uint64_t integer) {
        return (double)integer / (double)0xffffffffffffffff;
    }

    static uint64_t rotate(uint64_t x, uint64_t b) {
        return (x << b) ^ (x >> (64 - b));
    }

    static uint64_t pcg(uint64_t a) {
        uint64_t b{ a * 0xff51afd7ed558ccd };
        for (size_t ii = 0; ii < 3; ii++) {
            a = Hash::rotate((a ^ 0xcafebabe) + (b ^ 0xdeadbeef), 23);
            b = Hash::rotate((a ^ 0xcabba6e5) + (b ^ 0xb01dface), 5);
            //a ^= b;
            //b ^= a;
            //a ^= b;
        }
        return a ^ b;
    }

    static double pcgUnit(uint64_t a) {
        return Hash::toUnit(Hash::pcg(a));
    }

    static glm::i64vec3 pcg3(uint64_t a) {
        uint64_t b = a, c = a * 0xff51afd7ed558ccd;
        for (size_t ii = 0; ii < 3; ii++) {
            a = Hash::rotate((a ^ 0xcafebabe) + (b ^ 0xdeadbeef) + (c ^ 0x0b5e55ed), 23);
            b = Hash::rotate((a ^ 0xcabba6e5) + (b ^ 0xb01dface) + (c ^ 0x0b5e55ed), 5);
            c = Hash::rotate((a ^ 0xba5eba11) + (b ^ 0x6a5f1e1d) + (c ^ 0xdead50fa), 17);
        }
        // https://www.dcode.fr/words-containing
        return glm::i64vec3{a,b,c};
    }

    static glm::dvec3 pcgUnit3(uint64_t a) {
        glm::i64vec3 rr{ Hash::pcg3(a) };
        return glm::dvec3{
            Hash::toUnit(rr.x),
            Hash::toUnit(rr.y),
            Hash::toUnit(rr.z),
        };
    }

    static uint64_t pcg(uint64_t a, uint64_t b) {
        a *= 0xff51afd7ed558ccd;
        b *= 0xc4ceb9fe1a85ec53;
        for (size_t ii = 0; ii < 3; ii++) {
            a = Hash::rotate((a ^ 0xcafebabe) + (b ^ 0xdeadbeef), 23);
            b = Hash::rotate((a ^ 0xcabba6e5) + (b ^ 0xb01dface), 5);
        }
        return a ^ b;
    }

    static uint64_t pcg(glm::i64vec2 p) {
        return Hash::pcg(p.x, p.y);
    }

    static double pcgUnit(uint64_t a, uint64_t b) {
        return Hash::toUnit(Hash::pcg(a, b));
    }

    static double pcgUnit(glm::i64vec2 p) {
        return Hash::toUnit(Hash::pcg(p.x, p.y));
    }

    static uint64_t pcg(uint64_t a, uint64_t b, uint64_t c) {
        a *= 0xff51afd7ed558ccd;
        b *= 0xc4ceb9fe1a85ec53;
        c *= 0x9e3779b97f4a7c15;
        for (size_t ii = 0; ii < 3; ii++) {
            a = Hash::rotate((a ^ 0xcafebabe) + (b ^ 0xdeadbeef) + (c ^ 0x0b5e55ed), 23);
            b = Hash::rotate((a ^ 0xcabba6e5) + (b ^ 0xb01dface) + (c ^ 0x0b5e55ed), 5);
            c = Hash::rotate((a ^ 0xba5eba11) + (b ^ 0x6a5f1e1d) + (c ^ 0xdead50fa), 17);
        }
        // https://www.dcode.fr/words-containing
        return a ^ b ^ c;
    }

    static uint64_t pcg(glm::i64vec3 p) {
        return Hash::pcg(p.x, p.y, p.z);
    }

    static double pcgUnit(uint64_t a, uint64_t b, uint64_t c) {
        return Hash::toUnit(Hash::pcg(a, b, c));
    }

    static double pcgUnit(glm::i64vec3 p) {
        return Hash::toUnit(Hash::pcg(p.x, p.y, p.z));
    }

    // Hash function structs for use with std::unordered_map and similar containers
    struct IVec3Hash {
        size_t operator()(const glm::ivec3& coord) const {
            static_assert(sizeof(size_t) == 8, "size_t must be 64-bit");
            size_t hash = static_cast<size_t>(coord.x) ^ 
                          (static_cast<size_t>(coord.y) << 16) ^ 
                          (static_cast<size_t>(coord.z) << 32);
            hash = hash * 73856093;
            return hash;
        }
    };

    struct UintPtrPairHash {
        std::size_t operator()(const std::pair<std::uintptr_t, std::uintptr_t>& p) const noexcept {
            std::size_t h1 = std::hash<std::uintptr_t>{}(p.first);
            std::size_t h2 = std::hash<std::uintptr_t>{}(p.second);
            return h1 ^ (h2 << 1);
        }
    };

    struct IntPairHash {
        std::size_t operator()(const std::pair<int, int>& p) const noexcept {
            return std::hash<int>{}(p.first) ^ (std::hash<int>{}(p.second) << 1);
        }
    };

    struct DVec3Hash {
        size_t operator()(const glm::dvec3& vec) const {
            size_t hash = std::hash<double>{}(vec.x);
            hash ^= std::hash<double>{}(vec.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<double>{}(vec.z) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    struct DQuatHash {
        size_t operator()(const glm::dquat& quat) const {
            size_t hash = std::hash<double>{}(quat.w);
            hash ^= std::hash<double>{}(quat.x) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<double>{}(quat.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<double>{}(quat.z) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            return hash;
        }
    };
};