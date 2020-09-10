/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2014, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file ReadStreamPostgres.h
 *
 ***********************************************************************/

#pragma once

#include "souffle/RamTypes.h"
#include "souffle/SymbolTable.h"
#include "souffle/io/ReadStream.h"
#include "souffle/utility/MiscUtil.h"
#include "souffle/utility/StringUtil.h"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <pqxx/pqxx>

namespace souffle {
class RecordTable;

class ReadStreamPostgres : public ReadStream {
public:
    ReadStreamPostgres(const std::map<std::string, std::string>& rwOperation, SymbolTable& symbolTable,
            RecordTable& recordTable)
            : ReadStream(rwOperation, symbolTable, recordTable),
              tableName(rwOperation.at("table")) {
        // Get type info 
        init = false;
        std::string parseErrors;
        params = Json::parse(rwOperation.at("params"), parseErrors);
        assert(parseErrors.size() == 0 && "Internal JSON parsing failed.");
       
        // Validate
        checkTableExists();
    }

protected:
    Own<RamDomain[]> readNextTuple() override {
        if (!init) {
            auto&& relInfo = params["relation"];
            auto&& paramNames = relInfo["params"].array_items();

            std::vector<std::string> params;
            
            for (size_t i = 0; i < arity + auxiliaryArity; ++i) {
                params.push_back(paramNames[i].string_value());
            }

            tx = new pqxx::work(db);
            stream = new pqxx::stream_from(
                *tx, pqxx::from_table, tableName, params
            );
            init = true;
        }

        if (!stream) {
            stream->complete();
            delete stream;
            delete tx;
            return nullptr;
        }

        Own<RamDomain[]> tuple = std::make_unique<RamDomain[]>(arity + auxiliaryArity);

        auto row = stream->read_row();

        if (row == nullptr) {
            stream->complete();
            delete stream;
            delete tx;
            return nullptr;
        }

        uint32_t column;
        for (column = 0; column < arity; column++) {
            char type = typeAttributes[column][0];

            try {
                if (typeAttributes[column] == "i:bool") {
                    const char * cstr = row->at(column).c_str();
                    tuple[column] = (
                        cstr != nullptr && cstr[0] == 't' ? 1 : 0
                    );
                }
                else if (type == 's') {
                    tuple[column] = symbolTable.unsafeLookup(row->at(column).c_str());
                } else if (type == 'i') {
                    tuple[column] = RamSignedFromString(row->at(column).c_str());
                } else {
                    std::cout << "Unknown type: " << typeAttributes[column] << std::endl;
                }
            } catch (...) {
                std::stringstream errorMessage;
                errorMessage << "Error on '" << tableName << "' parsing column " << (column) + 1;
                errorMessage << std::endl << "  value:='" << row->at(column) << "'";
                throw std::invalid_argument(errorMessage.str());
            }
        }

        return tuple;
    }

    void checkTableExists() {
        std::stringstream selectSQL;
        selectSQL << "SELECT tablename FROM pg_tables WHERE schemaname = 'public' AND";
        selectSQL << " tablename = '" << tableName << "';";

        pqxx::work w{db};
        pqxx::result r{w.exec(selectSQL)};

        if (r.empty()) {
            throw std::invalid_argument(
                "Required table or view '" + tableName + "' does not exist.");
        }
    }

    const std::string tableName;
    pqxx::work* tx;
    pqxx::connection db;
    pqxx::stream_from* stream;
    bool init = false;
    Json params;
};

class ReadStreamPostgresArray : public ReadStream {
public:
    ReadStreamPostgresArray(const std::map<std::string, std::string>& rwOperation, SymbolTable& symbolTable,
            RecordTable& recordTable)
            : ReadStream(rwOperation, symbolTable, recordTable),
              tableName(rwOperation.at("table")),
              arrayName(rwOperation.at("array")) {
        checkTableExists();
        init = false;

        if (arity != 3) {
            throw std::invalid_argument(
                "IO postgres_array only for relations with arity==3.");
        }
    }

protected:
    Own<RamDomain[]> readNextTuple() override {
        if (!init) {
            std::stringstream select;

            select << "SELECT gid, element, idx";
            select << " FROM " << tableName << ", UNNEST(" << arrayName << ")";
            select << " WITH ORDINALITY AS T (element, idx)";

            tx = new pqxx::work(db);
            stream = new pqxx::stream_from(
                *tx, pqxx::from_query, select.str()
            );
            init = true;
        }

        if (!stream) {
            stream->complete();
            delete stream;
            delete tx;
            return nullptr;
        }

        auto row = stream->read_row();

        if (row == nullptr) {
            stream->complete();
            delete stream;
            delete tx;
            return nullptr;
        }

        Own<RamDomain[]> tuple = std::make_unique<RamDomain[]>(arity + auxiliaryArity);
        
        tuple[0] = RamSignedFromString(row->at(0).c_str());
        tuple[1] = RamSignedFromString(row->at(1).c_str());
        tuple[2] = RamSignedFromString(row->at(2).c_str());

        return tuple;
    }

    void checkTableExists() {
        std::stringstream selectSQL;
        selectSQL << "SELECT tablename FROM pg_tables WHERE schemaname = 'public' AND";
        selectSQL << " tablename = '" << tableName << "';";

        pqxx::work w{db};
        pqxx::result r{w.exec(selectSQL)};

        if (r.empty()) {
            throw std::invalid_argument(
                "Required table or view '" + tableName + "' does not exist.");
        }
    }

    const std::string tableName;
    const std::string arrayName;
    pqxx::work* tx;
    pqxx::connection db;
    pqxx::stream_from* stream;
    bool init = false;
};

class ReadPostgresFactory : public ReadStreamFactory {
public:
    Own<ReadStream> getReader(const std::map<std::string, std::string>& rwOperation, SymbolTable& symbolTable,
            RecordTable& recordTable) override {
        return mk<ReadStreamPostgres>(rwOperation, symbolTable, recordTable);
    }

    const std::string& getName() const override {
        static const std::string name = "postgres";
        return name;
    }
    ~ReadPostgresFactory() override = default;
};

class ReadPostgresArrayFactory : public ReadStreamFactory {
public:
    Own<ReadStream> getReader(const std::map<std::string, std::string>& rwOperation, SymbolTable& symbolTable,
            RecordTable& recordTable) override {
        return mk<ReadStreamPostgresArray>(rwOperation, symbolTable, recordTable);
    }

    const std::string& getName() const override {
        static const std::string name = "postgres_array";
        return name;
    }
    ~ReadPostgresArrayFactory() override = default;
};

} /* namespace souffle */
