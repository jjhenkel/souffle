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
        assert(parseErrors.size() == 0 && "Internal JSON parsing failed.");

        for (uint32_t i = 0; i < params.size(); i++) {
            paramNames.push_back(params[i].dump());
        }
    }

protected:
    std::shared_ptr<arrow::Table> FilterAndSelectDataset() {
        std::string root_path;

        auto fs = fs::FileSystemFromUri("file:///" + baseDir, &root_path).ValueOrDie();

        fs::FileSelector selector;
        selector.base_dir = ".";
        
        auto factory = ds::FileSystemDatasetFactory::Make(
            fs, selector, std::make_shared<ds::ParquetFileFormat>(), ds::FileSystemFactoryOptions()
        ).ValueOrDie();

        auto dataset = factory->Finish().ValueOrDie();
        auto scan_builder = dataset->NewScan().ValueOrDie();
    
        scan_builder->Project(paramNames);
        // scan_builder->Filter(ds::less(ds::field_ref("b"), ds::literal(4)));
        auto scanner = scan_builder->Finish().ValueOrDie();
        return scanner->ToTable().ValueOrDie();
    }

    Own<RamDomain[]> readNextTuple() override {
        Own<RamDomain[]> tuple = std::make_unique<RamDomain[]>(arity + auxiliaryArity);

        auto table = FilterAndSelectDataset();

        uint32_t column;
        for (column = 0; column < arity; column++) {

        }

        return tuple;
    }

    const std::string fileName;
    const std::string baseDir;
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
