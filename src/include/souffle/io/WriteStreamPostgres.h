/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2014, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file WriteStreamPostgres.h
 *
 ***********************************************************************/

#pragma once

#include "souffle/RamTypes.h"
#include "souffle/SymbolTable.h"
#include "souffle/io/WriteStream.h"
#include "souffle/utility/ContainerUtil.h"
#include "souffle/utility/MiscUtil.h"
#include "souffle/utility/ParallelUtil.h"

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <ostream>
#include <string>
#include <vector>
#include <variant>
#include <pqxx/pqxx>

namespace souffle {

class RecordTable;

class WritePostgres : public WriteStream {
public:
    WritePostgres(const std::map<std::string, std::string>& rwOperation, const SymbolTable& symbolTable,
            const RecordTable& recordTable)
            : WriteStream(rwOperation, symbolTable, recordTable),
              tableName(rwOperation.at("table")),
              tableSchema(rwOperation.at("schema")) {
        init = false;
        std::string parseErrors;
        params = Json::parse(rwOperation.at("params"), parseErrors);
        assert(parseErrors.size() == 0 && "Internal JSON parsing failed.");
        setup();
    }

    ~WritePostgres() override {
        stream->complete();
        tx->commit();
        delete stream;
        delete tx;
    }

protected:
    void writeNullary() override { }

    void writeNextTuple(const RamDomain* tuple) override {
        if (!init) {
            auto&& relInfo = params["relation"];
            auto&& paramNames = relInfo["params"].array_items();

            std::vector<std::string> params;
            
            for (size_t i = 0; i < arity + auxiliaryArity; ++i) {
                params.push_back(paramNames[i].string_value());
            }

            tx = new pqxx::work(db);
            stream = new pqxx::stream_to(
                *tx, tableName, params
            );
            init = true;
        }

        std::vector<std::variant<int64_t, std::string>> row;
        for (size_t col = 0; col < arity; ++col) {
            std::variant<int64_t, std::string> item;
            auto type = typeAttributes.at(col);
            switch (type[0]) {
                case 's': item = symbolTable.unsafeResolve(tuple[col]); break;
                case 'i': item = tuple[col]; break;
                default: fatal("unsupported type attribute: `%c`", type[0]);
            }
            row.push_back(item);
        }
        *stream << row;
    }

private:
    void setup() {
        const std::string query = 
            "DROP TABLE IF EXISTS " + tableName + "; " 
            + "CREATE TABLE " + tableName + tableSchema + ";";

        try {
            pqxx::work w{db};
            pqxx::result r{w.exec(query)};
            w.commit();
        }
        catch (const std::exception & ex) {
            std::cerr << ex.what() << std::endl;
            throw std::invalid_argument(
                "Failed to create table '" + tableName + "' using schema '" + tableSchema + "'.");
        }
    }

    const std::string tableName;
    const std::string tableSchema;
    pqxx::work* tx;
    pqxx::connection db;
    pqxx::stream_to* stream;
    bool init = false;
    Json params;
};

class WritePostgresFactory : public WriteStreamFactory {
public:
    Own<WriteStream> getWriter(const std::map<std::string, std::string>& rwOperation,
            const SymbolTable& symbolTable, const RecordTable& recordTable) override {
        return mk<WritePostgres>(rwOperation, symbolTable, recordTable);
    }

    const std::string& getName() const override {
        static const std::string name = "postgres";
        return name;
    }
    ~WritePostgresFactory() override = default;
};

}
