/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2014, 2015 Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file SymbolTable.h
 *
 * Encodes/decodes symbols to numbers (and vice versa).
 *
 ***********************************************************************/

#pragma once

#include "souffle/RamTypes.h"
#include "souffle/utility/MiscUtil.h"
#include "souffle/utility/ParallelUtil.h"
#include "souffle/utility/StreamUtil.h"
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <initializer_list>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace souffle {

/**
 * @class SymbolTable
 *
 * SymbolTable encodes symbols to numbers and decodes numbers to symbols.
 */
class SymbolTable {
private:
    /** A lock to synchronize parallel accesses */
    mutable Lock access;

    /** Stores symbol indices to symbols information */
    std::deque<std::string> numToStr;

    /** Stores symbols to symbol indices information */
    std::unordered_map<std::string, std::size_t> strToNum;

    /** Convenience method to place a new symbol in the table, if it does not exist, and return the index of
     * it; otherwise return the index. */
    inline std::size_t newSymbolOfIndex(const std::string& symbol) {
        std::size_t index;
        auto it = strToNum.find(symbol);
        if (it == strToNum.end()) {
            index = numToStr.size();
            strToNum[symbol] = index;
            numToStr.push_back(symbol);
        } else {
            index = it->second;
        }
        return index;
    }

public:
    SymbolTable() = default;
    SymbolTable(std::initializer_list<std::string> symbols) {
        strToNum.reserve(symbols.size());
        for (const auto& symbol : symbols) {
            if (strToNum.find(symbol) == strToNum.end()) {
                strToNum[symbol] = numToStr.size();
                numToStr.push_back(symbol);
            }
        }
    }

    virtual ~SymbolTable() = default;

    /* Obtain the size of the symbol table. */
    std::size_t size() const {
        return numToStr.size();
    }

    /** Encode a symbol to a symbol index; this method is thread-safe.  */
    RamDomain encode(const std::string& symbol) {
        {
            auto lease = access.acquire();
            (void)lease;  // avoid warning;
            return static_cast<RamDomain>(newSymbolOfIndex(symbol));
        }
    }

    /** Decode a symbol index to a symbol; this method is thread-safe.  */
    const std::string& decode(const RamDomain index) const {
        {
            auto lease = access.acquire();
            (void)lease;  // avoid warning;
            auto pos = static_cast<std::size_t>(index);
            if (pos >= size()) {
                // TODO: use different error reporting here!!
                fatal("Error index out of bounds in call to `SymbolTable::decode`. index = `%d`", index);
            }
            return numToStr[pos];
        }
    }

    /** Acquire symbol table lock */
    Lock::Lease acquireLock() const {
        return access.acquire();
    }

    /**
     * Encode a symbol to a symbol index; this method is not thread-safe.
     * The lock must be acquired explicitly.
     */
    RamDomain unsafeEncode(const std::string& symbol) {
        return static_cast<RamDomain>(newSymbolOfIndex(symbol));
    }

    /**
     * Decode an symbol index to symbol; this method is not thread-safe.
     * The lock must be acquired explicitly.
     */
    const std::string& unsafeDecode(const RamDomain index) const {
        return numToStr[static_cast<std::size_t>(index)];
    }
};

}  // namespace souffle
