/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2014, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file RecordTable.h
 *
 * Data container implementing a map between records and their references.
 * Records are separated by arity, i.e., stored in different RecordMaps.
 *
 ***********************************************************************/

#pragma once

#include "souffle/RamTypes.h"
#include "souffle/utility/span.h"
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace souffle {

/** @brief Bidirectional mappping between records and record references */
class RecordMap {
    /** arity of record */
    const std::size_t arity;

    /** hash function for unordered record map */
    struct RecordHash {
        std::size_t operator()(std::vector<RamDomain> record) const {
            std::size_t seed = 0;
            std::hash<RamDomain> domainHash;
            for (RamDomain value : record) {
                seed ^= domainHash(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };

    /** map from records to references */
    // TODO (b-scholz): replace vector<RamDomain> with something more memory-frugal
    std::unordered_map<std::vector<RamDomain>, RamDomain, RecordHash> recordToIndex;

    /** array of records; index represents record reference */
    // TODO (b-scholz): replace vector<RamDomain> with something more memory-frugal
    std::vector<std::vector<RamDomain>> indexToRecord;

public:
    explicit RecordMap(std::size_t arity)
            : arity(arity), indexToRecord(1) {}  // note: index 0 element left free

    /** @brief converts record to a record reference */
    // TODO (b-scholz): replace vector<RamDomain> with something more memory-frugal
    RamDomain pack(std::vector<RamDomain> vector) {
        RamDomain index;
#pragma omp critical(record_pack)
        {
            auto pos = recordToIndex.find(vector);
            if (pos != recordToIndex.end()) {
                index = pos->second;
            } else {
#pragma omp critical(record_unpack)
                {
                    assert(indexToRecord.size() <= std::numeric_limits<RamUnsigned>::max());
                    index = ramBitCast(RamUnsigned(indexToRecord.size()));
                    recordToIndex[vector] = index;
                    indexToRecord.push_back(std::move(vector));
                }
            }
        }
        return index;
    }

    /** @brief convert record pointer to a record reference */
    RamDomain pack(const RamDomain* tuple) {
        // TODO (b-scholz): data is unnecessarily copied
        // for a successful lookup. To avoid this, we should
        // compute a hash of the pointer-array and traverse through
        // the bucket list of the unordered map finding the record.
        // Note that in case of non-existence, the record still needs to be
        // copied for the newly created entry but this will be the less
        // frequent case.
        std::vector<RamDomain> tmp(arity);
        for (std::size_t i = 0; i < arity; i++) {
            tmp[i] = tuple[i];
        }
        return pack(std::move(tmp));
    }

    /** @brief convert record reference to a record pointer */
    const RamDomain* unpack(RamDomain index) const {
        const RamDomain* res;
#pragma omp critical(record_unpack)
        res = indexToRecord[index].data();
        return res;
    }
};

class RecordTable {
public:
    RecordTable() = default;
    virtual ~RecordTable() = default;

    /** @brief convert record to record reference */
    RamDomain pack(const RamDomain* tuple, std::size_t arity) {
        return lookupArity(arity).pack(tuple);
    }
    /** @brief convert record reference to a record */
    const RamDomain* unpack(RamDomain ref, std::size_t arity) const {
        std::unordered_map<std::size_t, RecordMap>::const_iterator iter;
#pragma omp critical(RecordTableGetForArity)
        {
            // Find a previously emplaced map
            iter = maps.find(arity);
        }
        assert(iter != maps.end() && "Attempting to unpack record for non-existing arity");
        return (iter->second).unpack(ref);
    }

private:
    /** @brief lookup RecordMap for a given arity; if it does not exist, create new RecordMap */
    RecordMap& lookupArity(std::size_t arity) {
        std::unordered_map<std::size_t, RecordMap>::iterator mapsIterator;
#pragma omp critical(RecordTableGetForArity)
        {
            // This will create a new map if it doesn't exist yet.
            mapsIterator = maps.emplace(arity, arity).first;
        }
        return mapsIterator->second;
    }

    /** Arity/RecordMap association */
    std::unordered_map<std::size_t, RecordMap> maps;
};

/** @brief helper to convert tuple to record reference for the synthesiser */
template <std::size_t Arity>
RamDomain pack(RecordTable& recordTab, Tuple<RamDomain, Arity> const& tuple) {
    return recordTab.pack(tuple.data(), Arity);
}

/** @brief helper to convert tuple to record reference for the synthesiser */
template <std::size_t Arity>
RamDomain pack(RecordTable& recordTab, span<const RamDomain, Arity> tuple) {
    return recordTab.pack(tuple.data(), Arity);
}

}  // namespace souffle
