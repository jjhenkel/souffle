/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2018, The Souffle Developers. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file StratifyInputRelations.h
 *
 * Transformation pass to stratify input relations 
 * based on literal values and columns loaded
 * E.g.: a(x) :- input(x, "y"). -> a(x) :- input__2y_1(x)
 *
 ***********************************************************************/

#pragma once

#include "ast/TranslationUnit.h"
#include "ast/transform/Transformer.h"
#include <string>

namespace souffle::ast::transform {

/**
 * Transformation pass to stratify input relations 
 * based on literal values and columns loaded
 * E.g.: a(x) :- input(x, "y"). -> a(x) :- input__2y_1(x)
 */
class StratifyInputRelationsTransformer : public Transformer {
public:
    std::string getName() const override {
        return "StratifyInputRelationsTransformer";
    }

private:
    StratifyInputRelationsTransformer* cloning() const override {
        return new StratifyInputRelationsTransformer();
    }

    bool transform(TranslationUnit& translationUnit) override;
};

}  // namespace souffle::ast::transform