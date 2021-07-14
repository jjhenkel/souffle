/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2018, The Souffle Developers. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file StratifyInputRelations.cpp
 *
 ***********************************************************************/

#include "ast/transform/StratifyInputRelations.h"
#include "Global.h"
#include "ast/Aggregator.h"
#include "ast/Attribute.h"
#include "ast/BinaryConstraint.h"
#include "ast/Constant.h"
#include "ast/Directive.h"
#include "ast/Functor.h"
#include "ast/Node.h"
#include "ast/NumericConstant.h"
#include "ast/Program.h"
#include "ast/QualifiedName.h"
#include "ast/RecordInit.h"
#include "ast/Relation.h"
#include "ast/StringConstant.h"
#include "ast/TranslationUnit.h"
#include "ast/UnnamedVariable.h"
#include "ast/analysis/IOType.h"
#include "ast/analysis/PolymorphicObjects.h"
#include "ast/analysis/PrecedenceGraph.h"
#include "ast/analysis/RelationDetailCache.h"
#include "ast/analysis/SCCGraph.h"
#include "ast/utility/BindingStore.h"
#include "ast/utility/NodeMapper.h"
#include "ast/utility/Utils.h"
#include "ast/utility/Visitor.h"
#include "parser/SrcLocation.h"
#include "souffle/BinaryConstraintOps.h"
#include "souffle/RamTypes.h"
#include "souffle/utility/ContainerUtil.h"
#include "souffle/utility/MiscUtil.h"
#include "souffle/utility/StringUtil.h"
#include "souffle/utility/json11.h"
#include <cstddef>
#include <memory>
#include <map>
#include <set>
#include <ostream>
#include <vector>
#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <iterator>

// classic https://stackoverflow.com/a/217605
static inline void rtrim(std::string &s, char c) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [c](unsigned char ch) {
        return ch != c;
    }).base(), s.end());
}

namespace souffle::ast::transform {

bool StratifyInputRelationsTransformer::transform(TranslationUnit& translationUnit) {
    auto& program = translationUnit.getProgram();

    bool changed = false;

    // Let's find all the input relations and get the partition keys from them
    std::map<std::string, std::vector<uint32_t>> inputRelDirectives;
    std::map<std::string, std::vector<std::string>> inputRelNames;
    std::map<std::string, std::map<std::string, std::string>> inputRelParams;
    visit(program, [&](const Directive& directive) {
        // We're looking for input directives
        if (directive.getType() != ast::DirectiveType::input) {
            return;
        }

        // That have a `partition="a b c..."` key
        if (!directive.hasParameter("partition")) {
            return;
        }

        // Split on space and put into vector
        std::istringstream iss(directive.getParameter("partition"));
        std::vector<std::string> cols {
            std::istream_iterator<std::string>{iss},
            std::istream_iterator<std::string>{}
        };

        // Go through relation attributes and get indices of keys
        auto* relation = getRelation(program, directive.getQualifiedName());
        if (relation == nullptr) {
            return;
        }

        // Map columns to their indices (1-based)
        std::vector<uint32_t> indices;
        std::vector<std::string> names;
        auto attrs = relation->getAttributes();
        for (uint32_t i = 0; i < relation->getArity(); ++i) {
            if (std::find(cols.begin(), cols.end(), attrs[i]->getName()) != cols.end()) {
                indices.push_back(i + 1);
                names.push_back(attrs[i]->getName());
            }
        }

        // Map qualified names (of input relations) to their 
        // partition keys (1-based indices) for later use
        inputRelDirectives.insert(std::make_pair(
            directive.getQualifiedName().toString(), indices
        ));
        inputRelNames.insert(std::make_pair(
            directive.getQualifiedName().toString(), names
        ));
        inputRelParams.insert(std::make_pair(
            directive.getQualifiedName().toString(), directive.getParameters()
        ));
    });

    // Let's walk all the relations
    uint32_t globalIdx = 0;
    std::set<std::string> inputVariants;
    for (auto* rel : program.getRelations()) {
        // And let's walk all of their clauses
        for (auto* clause : getClauses(program, rel->getQualifiedName())) {
            VecOwn<Literal> keep;
            std::vector<const void*> ignore;
            
            std::map<std::string, std::string> constMap;
            // First, let's record a == "foo" constraints so we can inline them
            for (const auto* literal : clause->getBodyLiterals()) {
                // Needs to be a binary constraint
                const auto* binaryCon = as<ast::BinaryConstraint>(literal);
                if (binaryCon == nullptr) {
                    continue;
                }

                // Needs to have a constant rhs 
                const auto* rhs = as<ast::StringConstant>(binaryCon->getRHS());
                if (rhs == nullptr) {
                    continue;
                }

                // Needs to have a variable lhs
                const auto* lhs = as<ast::Variable>(binaryCon->getLHS());
                if (lhs == nullptr) {
                    continue;
                }

                constMap.insert(std::make_pair(lhs->getName(), rhs->getConstant()));
                ignore.push_back(literal);
            }
            
            // Next, let's copy things but re-map vars in constMap
            for (const auto* literal : clause->getBodyLiterals()) {
                // Ignore the a = "foo" statements we handle above
                if (std::find(ignore.begin(), ignore.end(), literal) != ignore.end()) {
                    continue;
                }

                // See if we're an atom
                const auto* atom = as<ast::Atom>(literal);
                
                // If not that's fine, just keep/continue
                if (atom == nullptr) {
                    keep.push_back(clone(literal));
                    continue;
                }

                // Make a copy of this atom (that'll have new args, maybe)
                auto newAtom = mk<Atom>();
                newAtom->setQualifiedName(QualifiedName(
                    atom->getQualifiedName().toString()
                ));

                // Look for args that are vars
                for (const auto* arg : atom->getArguments()) {
                    // Look for vars in const map
                    const auto* var = as<ast::Variable>(arg);
                    if (var != nullptr && constMap.find(var->getName()) != constMap.end()) {
                        newAtom->addArgument(mk<ast::StringConstant>(
                            constMap[var->getName()]
                        ));
                        continue;
                    }

                    // Not var, just keep
                    newAtom->addArgument(clone(arg));
                }

                // Keep the (possibly re-written atom)
                keep.push_back(clone(newAtom));
            }
            
            // Let's walk all of the literals in each clauses body
            clause->setBodyLiterals(std::move(keep)); // First update

            VecOwn<Literal> keep2;
            for (const auto* literal : clause->getBodyLiterals()) {
                // See if we're an atom
                const auto* atom = as<ast::Atom>(literal);
                
                // If not that's fine, just keep/continue
                if (atom == nullptr) {
                    keep2.push_back(clone(literal));
                    continue;
                }

                // Get our qualified name
                auto qualName = atom->getQualifiedName().toString();

                // Skip things that don't match any input relation
                if (inputRelDirectives.find(qualName) == inputRelDirectives.end()) {
                    keep2.push_back(clone(literal));
                    continue;
                }

                // Let's find the directive associated with us too
                std::vector<uint32_t> pcols = inputRelDirectives[qualName];
                std::map<std::string, json11::Json> pvals;
                std::vector<uint32_t> kcols;

                auto newAtom = mk<Atom>();
                std::string modifier = "";
                std::string modCols = "";

                uint32_t cidx = 0;

                for (const auto* arg : atom->getArguments()) {
                    cidx += 1;
                    
                    // Maybe we are a constant
                    const auto* constant = as<ast::Constant>(arg);
                    const auto it = std::find(pcols.begin(), pcols.end(), cidx);
                    if (constant != nullptr && it != pcols.end()) {
                        int pidx = it - pcols.begin();
                        pvals.insert(std::make_pair(
                            inputRelNames[qualName][pidx],
                            json11::Json(constant->getConstant())
                        ));
                        modifier += "p" + std::to_string(cidx) + "_" + std::to_string(std::hash<std::string>{}(constant->getConstant()));
                        continue;
                    } 

                    // Maybe we are unnamed
                    const auto* unnamed = as<ast::UnnamedVariable>(arg);
                    if (unnamed != nullptr) {
                        continue;
                    }

                    // We were neither, keep this column
                    newAtom->addArgument(clone(arg));
                    kcols.push_back(cidx);
                    modCols += std::to_string(cidx) + "_";
                }

                uint32_t arity = kcols.size();

                // Maybe make a modification, if conditions were met
                if (modifier != "") {
                    globalIdx += 1;
                    changed = true;

                    auto keepCol = [&kcols](uint32_t i){
                        return std::find(kcols.begin(), kcols.end(), i + 1) != kcols.end();
                    };

                    // Re-name the stratified atom
                    auto newName = atom->getQualifiedName().toString() + "__pq_" + modifier + "_" + modCols;
                    rtrim(newName, '_');
                    newAtom->setQualifiedName(QualifiedName(newName));

                    // Make the input/decl for this new variant
                    if (inputVariants.find(newName) == inputVariants.end()) {
                        auto* relation = getRelation(program, qualName);

                        VecOwn<Attribute> newAttrs;

                        for (uint32_t i = 0; i < relation->getAttributes().size(); ++i) {
                            if (keepCol(i)) {
                                newAttrs.push_back(clone(relation->getAttributes()[i]));
                            }
                        }

                        auto newRel = clone(relation);
                        newRel->setAttributes(std::move(newAttrs));
                        newRel->setQualifiedName(QualifiedName(newName));
                        program.addRelation(std::move(newRel));

                        auto directive = mk<ast::Directive>(
                            ast::DirectiveType::input, newName, SrcLocation()
                        );

                        for (auto const& [key, value] : inputRelParams[qualName]) {
                            if (key == "attributeNames") {
                                std::istringstream iss(value);
                                std::vector<std::string> cols {
                                    std::istream_iterator<std::string>{iss},
                                    std::istream_iterator<std::string>{}
                                };

                                std::string newAttributes = "";
                                for (uint32_t i = 0; i < cols.size(); ++i) {
                                    if (keepCol(i)) {
                                        newAttributes += cols[i] + "\t";
                                    }
                                }
                                
                                rtrim(newAttributes, '\t');
                                directive->addParameter(key, newAttributes);
                            } else if (key == "name") {
                                directive->addParameter(key, newName);
                            } else if (key == "types") {
                                // Parse and get at the types array
                                std::string errors;
                                auto asJson = json11::Json::parse(value, errors);
                                auto typeArray = asJson["relation"]["types"].array_items();
                                
                                // Assert some stuff
                                assert(!asJson["ADTs"].is_null() && "StratifyInputs: can't handle ADTs");
                                assert(!asJson["records"].is_null() && "StratifyInputs: can't handle records");

                                // Keep only the columns we marked
                                std::vector<json11::Json> newArray;
                                for (uint32_t i = 0; i < typeArray.size(); ++i) {
                                    if (keepCol(i)) {
                                        newArray.push_back(typeArray[i]);
                                    }
                                }
                                
                                // Json obj is immutable, so build a new one
                                json11::Json newJson = json11::Json::object {
                                    { "ADTs", {} },
                                    { "records", {} },
                                    { "relation", json11::Json::object {
                                        { "arity", static_cast<long long>(arity) },
                                        { "types", newArray }
                                    }}
                                };

                                // Add it back
                                directive->addParameter(key, newJson.dump());
                            } else if (key == "params") {
                                // Parse and get at the params array
                                std::string errors;
                                auto asJson = json11::Json::parse(value, errors);
                                auto paramsArray = asJson["relation"]["params"].array_items();
                                
                                // Assert some stuff
                                assert(!asJson["records"].is_null() && "StratifyInputs: can't handle records");

                                // Keep only the columns we marked
                                std::vector<json11::Json> newArray;
                                for (uint32_t i = 0; i < paramsArray.size(); ++i) {
                                    if (keepCol(i)) {
                                        newArray.push_back(paramsArray[i]);
                                    }
                                }
                                
                                // Json obj is immutable, so build a new one
                                json11::Json newJson = json11::Json::object {
                                    { "records", {} },
                                    { "relation", json11::Json::object {
                                        { "arity", static_cast<long long>(arity) },
                                        { "params", newArray }
                                    }}
                                };

                                // Add it back
                                directive->addParameter(key, newJson.dump());
                            } else if (key == "partition") {
                                json11::Json newJson = json11::Json(pvals);
                                directive->addParameter(key + "ing", newJson.dump());
                            } else {
                                directive->addParameter(key, value);
                            }
                        }

                        program.addDirective(std::move(directive));
                        inputVariants.insert(newName);
                    }

                    keep2.push_back(clone(newAtom));
                } else {
                    keep2.push_back(clone(literal));
                }

            }
        
            clause->setBodyLiterals(std::move(keep2)); // Final update
        }
    }

    return changed;
}

}  // namespace souffle::ast::transform
