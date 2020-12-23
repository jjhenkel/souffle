/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2018 The Souffle Developers. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file ClauseTranslator.h
 *
 ***********************************************************************/

#pragma once

#include "ast2ram/seminaive/ClauseTranslator.h"
#include <vector>

namespace souffle {
class SymbolTable;
}

namespace souffle::ast {
class Argument;
class Atom;
class Clause;
}  // namespace souffle::ast

namespace souffle::ram {
class Operation;
}

namespace souffle::ast2ram {
class TranslatorContext;
}

namespace souffle::ast2ram::provenance {

class ClauseTranslator : public ast2ram::seminaive::ClauseTranslator {
public:
    ClauseTranslator(const TranslatorContext& context, SymbolTable& symbolTable)
            : ast2ram::seminaive::ClauseTranslator(context, symbolTable) {}

protected:
    Own<ram::Operation> addNegatedDeltaAtom(Own<ram::Operation> op, const ast::Atom* atom) const override;
    Own<ram::Operation> addNegatedAtom(Own<ram::Operation> op, const ast::Atom* atom) const override;
    Own<ram::Operation> createProjection(const ast::Clause& clause) const override;
    void indexAtoms(const ast::Clause& clause) override;

private:
    Own<ram::Expression> getLevelNumber(const ast::Clause& clause) const;
};

}  // namespace souffle::ast2ram::provenance
