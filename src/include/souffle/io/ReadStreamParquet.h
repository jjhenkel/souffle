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

namespace ds = arrow::dataset;
namespace fs = arrow::fs;

namespace souffle {
class RecordTable;

class ReadStreamParquet : public ReadStream {
public:
    ReadStreamParquet(const std::map<std::string, std::string>& rwOperation, SymbolTable& symbolTable,
            RecordTable& recordTable)
            : ReadStream(rwOperation, symbolTable, recordTable),
              fileName(rwOperation.at("filename")),
              baseDir(getOr(rwOperation, "fact-dir", ".") + "/") {
       
        // Get type info 
        std::string parseErrors;
        params = Json::parse(
            rwOperation.at("params"), parseErrors
        )["relation"]["params"].array_items();
        assert(parseErrors.size() == 0 && "Internal JSON parsing failed (params).");

        std::string partitioning = getOr(rwOperation, "partitioning", "{}");
        auto partitions = Json::parse(partitioning, parseErrors);
        assert(parseErrors.size() == 0 && "Internal JSON parsing failed (partitioning).");

        for (uint32_t i = 0; i < params.size(); i++) {
            paramNames.push_back(params[i].string_value());
        }

        std::string root_path;

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
        
        // for (const auto& fragment : dataset->GetFragments().ValueOrDie()) {
        //     std::cerr << "Found fragment: " << (*fragment)->ToString() << std::endl;
        //     std::cerr << "Partition expression: "
        //         << (*fragment)->partition_expression().ToString() << std::endl;
        // }

        auto scan_builder = dataset->NewScan().ValueOrDie();
        scan_builder->Project(paramNames);

        bool filtered = false;
        for (auto const& [key, value] : partitions.object_items()) {
            scan_builder->Filter(arrow::compute::equal(
                arrow::compute::field_ref(key), arrow::compute::literal(value.string_value())
            ));
            filtered = true;
        }

        // TODO: for each value in partitions metadata make an equals(VAL) filter
        
        auto scanner = scan_builder->Finish().ValueOrDie();

        batchIdx = 0;
        rowIdx = 0;
        auto batch_iterator = scanner->ScanBatches().ValueOrDie();
        while (true) {
            auto batch = batch_iterator.Next().ValueOrDie();
            if (arrow::IsIterationEnd(batch)) break;
            batches.push_back(batch.record_batch);
        }
    }

protected:

    Own<RamDomain[]> readNextTuple() override {
        Own<RamDomain[]> tuple = std::make_unique<RamDomain[]>(arity + auxiliaryArity);

        // std::cerr << "HERE Batch := " << batchIdx << " Row := " << rowIdx << std::endl;

        if (batchIdx >= batches.size()) {
            return nullptr;
        }
        
        auto batch = batches[batchIdx];
        if (rowIdx >= batch->num_rows()) {
            batchIdx += 1;
            if (batchIdx >= batches.size()) {
                return nullptr;
            }
            batch = batches[batchIdx];
            rowIdx = 0;
        }

        for (uint32_t c = 0; c < arity; c++) {
            // std::cerr << "Get: " << typeAttributes[c] << std::endl;
            if (typeAttributes[c][0] == 's') {
                // std::cerr << batch->GetColumnByName(paramNames[c])->ToString() << std::endl;
                tuple[c] = symbolTable.unsafeEncode(
                    std::dynamic_pointer_cast<arrow::StringArray>(
                        batch->GetColumnByName(paramNames[c])
                    )->GetString(rowIdx)
                );
            } else if (typeAttributes[c] == "i:Fid") {
                // std::cerr << batch->GetColumnByName(paramNames[c])->ToString() << std::endl;
                auto val = std::dynamic_pointer_cast<arrow::StringArray>(
                    batch->GetColumnByName(paramNames[c])
                )->GetString(rowIdx);
                tuple[c] = RamSignedFromString(val);
            } else if (typeAttributes[c][0] == 'i') {
                // std::cerr << batch->GetColumnByName(paramNames[c])->ToString() << std::endl;
                tuple[c] = std::dynamic_pointer_cast<arrow::Int64Array>(
                    batch->GetColumnByName(paramNames[c])
                )->Value(rowIdx);
            } else {
                std::cerr << "Unknown type: " << typeAttributes[c] << std::endl;
            }
        }

        rowIdx += 1;

        return tuple;
    }

    const std::string fileName;
    const std::string baseDir;
    uint64_t batchIdx;
    int64_t rowIdx;
    std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
    std::vector<Json> params;
    std::vector<std::string> paramNames;
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
