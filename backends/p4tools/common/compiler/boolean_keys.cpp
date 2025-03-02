#include "backends/p4tools/common/compiler/boolean_keys.h"

#include "backends/p4tools/common/lib/ir.h"

namespace P4Tools {

const IR::Node* CastBooleanTableKeys::postorder(IR::KeyElement* key) {
    if (key->expression->type->is<IR::Type_Boolean>()) {
        key->expression =
            new IR::Cast(key->expression->srcInfo, IRUtils::getBitType(1), key->expression);
    }
    return key;
}

const IR::Node* CastBooleanTableKeys::postorder(IR::Entry* entry) {
    auto* keyExprs = entry->keys->clone();
    for (size_t idx = 0; idx < keyExprs->size(); ++idx) {
        const auto* keyExpr = keyExprs->components.at(idx);
        if (const auto* boolLiteral = keyExpr->to<IR::BoolLiteral>()) {
            int v = boolLiteral->value ? 1 : 0;
            keyExprs->components[idx] = IRUtils::getConstant(IRUtils::getBitType(1), v);
        }
    }
    entry->keys = keyExprs;
    return entry;
}

}  // namespace P4Tools
