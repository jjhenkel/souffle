/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2014, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file ReadStreamParquet.h
 *
 ***********************************************************************/

#pragma once

#include "souffle/RamTypes.h"
#include "souffle/SymbolTable.h"
#include "souffle/io/ReadStream.h"
#include "souffle/utility/MiscUtil.h"
#include "souffle/utility/StringUtil.h"

#include <arrow/api.h>
#include <arrow/compute/cast.h>
#include <arrow/compute/exec/expression.h>
#include <arrow/dataset/dataset.h>
#include <arrow/dataset/discovery.h>
#include <arrow/dataset/file_base.h>
#include <arrow/dataset/file_ipc.h>
#include <arrow/dataset/file_parquet.h>
#include <arrow/dataset/scanner.h>
#include <arrow/filesystem/filesystem.h>
#include <arrow/ipc/writer.h>
#include <arrow/util/iterator.h>
#include <parquet/arrow/writer.h>

#include <cassert>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <numeric>
#include <chrono>
#include <iomanip>

namespace ds = arrow::dataset;
namespace fs = arrow::fs;

namespace souffle {
class RecordTable;

class ReadStreamParquet : public ReadStream {
public:
    ReadStreamParquet(const std::map<std::string, std::string>& rwOperation, SymbolTable& symbolTable,
            RecordTable& recordTable)
            : ReadStream(rwOperation, symbolTable, recordTable) {
        
        std::string fileName = rwOperation.at("filename");
        std::string baseDir = getOr(rwOperation, "fact-dir", ".") + "/"; 

        std::cerr << std::fixed << std::setprecision(9) << std::left;
        std::cerr << "Enter: ReadStreamParquet[" << rwOperation.at("name") << "]()" << std::endl;

        tidx = 0;
        totalTuples = 0;

        auto meta_start = std::chrono::high_resolution_clock::now();

        // Get type info 
        std::string parseErrors;
        auto params = Json::parse(
            rwOperation.at("params"), parseErrors
        )["relation"]["params"].array_items();
        assert(parseErrors.size() == 0 && "Internal JSON parsing failed (params).");

        std::string partitioning = getOr(rwOperation, "partitioning", "{}");
        auto partitions = Json::parse(partitioning, parseErrors);
        std::cerr << " partition_info=" << partitions.dump() << std::endl;
        assert(parseErrors.size() == 0 && "Internal JSON parsing failed (partitioning).");

        std::vector<std::string> paramNames;
        for (uint32_t i = 0; i < params.size(); i++) {
            paramNames.push_back(params[i].string_value());
        }

        auto meta_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = meta_end-meta_start;
        std::cerr << "  + Metadata parse := " << std::setw(9) << (diff).count() << "s" << std::endl;

        std::string root_path;

        auto ds_start = std::chrono::high_resolution_clock::now();

        // chrono get time start/stop

        auto fs = fs::FileSystemFromUri("file:///" + baseDir, &root_path).ValueOrDie();

        fs::FileSelector selector;
        selector.base_dir = baseDir + fileName;
        selector.recursive = true;

        ds::FileSystemFactoryOptions options;
        options.partitioning = ds::HivePartitioning::MakeFactory();
        
        auto factory = ds::FileSystemDatasetFactory::Make(
            fs, selector, std::make_shared<ds::ParquetFileFormat>(), options
        ).ValueOrDie();

        auto dataset = factory->Finish().ValueOrDie();

        auto ds_end = std::chrono::high_resolution_clock::now();
        diff = ds_end-ds_start;
        std::cerr << "  + Dataset create := " << std::setw(9) << (diff).count() << "s" << std::endl;
        
        // for (const auto& fragment : dataset->GetFragments().ValueOrDie()) {
        //     std::cerr << "Found fragment: " << (*fragment)->ToString() << std::endl;
        //     std::cerr << "Partition expression: "
        //         << (*fragment)->partition_expression().ToString() << std::endl;
        // }

        auto scan_start = std::chrono::high_resolution_clock::now();

        auto scan_builder = dataset->NewScan().ValueOrDie();
        scan_builder->Project(paramNames);

        bool filtered = false;
        for (auto const& [key, value] : partitions.object_items()) {
            scan_builder->Filter(arrow::compute::equal(
                arrow::compute::field_ref(key), arrow::compute::literal(value.string_value())
            ));
            filtered = true;
        }

        auto scanner = scan_builder->Finish().ValueOrDie();

        auto scan_end = std::chrono::high_resolution_clock::now();
        diff = scan_end-scan_start;
        std::cerr << "  + Scanner create := " << std::setw(9) << (diff).count() << "s" << std::endl;

        auto build_batches_start = std::chrono::high_resolution_clock::now();
            
        for (uint32_t c = 0; c < arity; c++) {
            std::vector<RamDomain> column;
            processedColumns.push_back(column);
        }
                
        auto batch_iterator = scanner->ScanBatches().ValueOrDie();
        while (true) {
            auto batch = batch_iterator.Next().ValueOrDie();
            
            if (arrow::IsIterationEnd(batch)) break;

            totalTuples += batch.record_batch->num_rows();
            
            for (uint32_t c = 0; c < arity; c++) {
                // std::cerr << "Get: " << typeAttributes[c] << std::endl;
                if (typeAttributes[c][0] == 's') {
                    // std::cerr << batch->GetColumnByName(paramNames[c])->ToString() << std::endl;
                    auto colAsStr = std::dynamic_pointer_cast<arrow::StringArray>(
                        batch.record_batch->GetColumnByName(paramNames[c])
                    );

                    for (int64_t r = 0; r < batch.record_batch->num_rows(); ++r) {
                        processedColumns[c].push_back(symbolTable.unsafeEncode(
                            colAsStr->GetString(r)
                        ));
                    }
                } else if (typeAttributes[c] == "i:Fid") {
                    // std::cerr << batch->GetColumnByName(paramNames[c])->ToString() << std::endl;
                    auto colAsStr = std::dynamic_pointer_cast<arrow::StringArray>(
                        batch.record_batch->GetColumnByName(paramNames[c])
                    );

                    for (int64_t r = 0; r < batch.record_batch->num_rows(); ++r) {
                        processedColumns[c].push_back(RamSignedFromString(colAsStr->GetString(r)));
                    }
                } else if (typeAttributes[c][0] == 'i') {
                    // std::cerr << batch->GetColumnByName(paramNames[c])->ToString() << std::endl;
                    auto colAsInt = std::dynamic_pointer_cast<arrow::Int64Array>(
                        batch.record_batch->GetColumnByName(paramNames[c])
                    );

                    for (int64_t r = 0; r < batch.record_batch->num_rows(); ++r) {
                        processedColumns[c].push_back(colAsInt->Value(r));
                    }
                } else {
                    std::cerr << "Unknown type: " << typeAttributes[c] << std::endl;
                }
            }
        }

        auto build_batch_end = std::chrono::high_resolution_clock::now();
        diff = build_batch_end-build_batches_start;
        std::cerr << "  + Build batches := " << std::setw(9) << (diff).count() << "s" << std::endl;
        std::cerr << "Exit: ReadStreamParquet[" << fileName << "]()" << std::endl;
    }

protected:

    Own<RamDomain[]> readNextTuple() override {
        if (tidx < totalTuples) {
            Own<RamDomain[]> tuple = mk<RamDomain[]>(typeAttributes.size());

            for (uint32_t c = 0; c < arity; c++) {
                tuple[c] = processedColumns[c][tidx];
            }

            tidx += 1;
            return tuple;
        }
        
        return nullptr;
    }

    int64_t tidx;
    int64_t totalTuples;
    std::vector<std::vector<RamDomain>> processedColumns;
};

class ReadParquetFactory : public ReadStreamFactory {
public:
    Own<ReadStream> getReader(const std::map<std::string, std::string>& rwOperation, SymbolTable& symbolTable,
            RecordTable& recordTable) override {
        return mk<ReadStreamParquet>(rwOperation, symbolTable, recordTable);
    }

    const std::string& getName() const override {
        static const std::string name = "parquet";
        return name;
    }
    ~ReadParquetFactory() override = default;
};

} /* namespace souffle */
