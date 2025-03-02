#include <algorithm>
#include <cstddef>
#include <functional>
#include <ostream>
#include <vector>

#include <boost/none.hpp>

#include "backends/p4tools/common/lib/formulae.h"
#include "backends/p4tools/common/lib/ir.h"
#include "backends/p4tools/common/lib/symbolic_env.h"
#include "backends/p4tools/common/lib/trace_events.h"
#include "ir/ir.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"
#include "lib/safe_vector.h"

#include "backends/p4tools/testgen//lib/exceptions.h"
#include "backends/p4tools/testgen/core/externs.h"
#include "backends/p4tools/testgen/core/program_info.h"
#include "backends/p4tools/testgen/core/small_step/expr_stepper.h"
#include "backends/p4tools/testgen/core/small_step/small_step.h"
#include "backends/p4tools/testgen/lib/continuation.h"
#include "backends/p4tools/testgen/lib/execution_state.h"

namespace P4Tools {

namespace P4Testgen {

void ExprStepper::setFields(ExecutionState* nextState,
                            const std::vector<const IR::Member*>& flatFields, int varBitFieldSize) {
    for (const auto* fieldRef : flatFields) {
        const auto* fieldType = nextState->get(fieldRef)->type;
        // If the header had a varbit, the header needs to be updated.
        // We assign @param varbitFeldSize to the varbit field.
        if (const auto* varbit = fieldType->to<IR::Extracted_Varbits>()) {
            BUG_CHECK(varBitFieldSize >= 0,
                      "varBitFieldSize should be larger or equals to zero at this "
                      "point. The value is %1%.",
                      varBitFieldSize);
            auto* newVarbit = varbit->clone();

            newVarbit->assignedSize = varBitFieldSize;
            auto* newRef = fieldRef->clone();
            newRef->type = newVarbit;
            fieldRef = newRef;
            fieldType = newVarbit;
        }
        auto fieldWidth = fieldType->width_bits();

        // If the width is zero, do not bother with extracting.
        if (fieldWidth == 0) {
            continue;
        }

        // Slice from the buffer and append to the packet, if necessary.
        const auto* pktVar = nextState->slicePacketBuffer(fieldWidth);
        // We need to cast the generated variable to the appropriate type.
        if (fieldType->is<IR::Extracted_Varbits>()) {
            pktVar = new IR::Cast(fieldType, pktVar);
        } else if (const auto* bits = fieldType->to<IR::Type_Bits>()) {
            if (bits->isSigned) {
                pktVar = new IR::Cast(fieldType, pktVar);
            }
        } else if (fieldRef->type->is<IR::Type_Boolean>()) {
            const auto* boolType = IR::Type_Boolean::get();
            pktVar = new IR::Cast(boolType, pktVar);
        }
        // Update the field and add a trace event.
        nextState->add(new TraceEvent::Extract(fieldRef, pktVar));
        nextState->set(fieldRef, pktVar);
    }
}

ExprStepper::PacketCursorAdvanceInfo ExprStepper::calculateSuccessfulParserAdvance(
    const ExecutionState& state, int advanceSize) const {
    // Calculate the necessary size of the packet to extract successfully.
    // The minimum required size for a packet is the current cursor and the amount we are
    // advancing into the packet minus whatever has been buffered in the current buffer.
    auto minSize =
        std::max(0, state.getInputPacketCursor() + advanceSize - state.getPacketBufferSize());
    auto* cond = new IR::Geq(IR::Type::Boolean::get(), ExecutionState::getInputPacketSizeVar(),
                             IRUtils::getConstant(ExecutionState::getPacketSizeVarType(), minSize));
    return {advanceSize, cond, advanceSize, new IR::LNot(cond)};
}

ExprStepper::PacketCursorAdvanceInfo ExprStepper::calculateAdvanceExpression(
    const ExecutionState& state, const IR::Expression* advanceExpr,
    const IR::Expression* restrictions) const {
    const auto* packetSizeVarType = ExecutionState::getPacketSizeVarType();

    const auto* cursorConst = IRUtils::getConstant(packetSizeVarType, state.getInputPacketCursor());
    const auto* bufferSizeConst =
        IRUtils::getConstant(packetSizeVarType, state.getPacketBufferSize());
    auto* minSize =
        new IR::Sub(packetSizeVarType, new IR::Add(packetSizeVarType, cursorConst, advanceExpr),
                    bufferSizeConst);
    // The packet size must be larger than the current parser cursor minus what is already
    // present in the buffer. The advance expression, i.e., the size of the advance can be freely
    // chosen.
    auto* cond =
        new IR::Geq(IR::Type::Boolean::get(), ExecutionState::getInputPacketSizeVar(), minSize);

    // Compute the accept case.
    int advanceVal = 0;
    const auto* advanceCond = new IR::LAnd(cond, restrictions);
    const auto* advanceConst = evaluateExpression(advanceExpr, advanceCond);
    // Compute the reject case.
    int notAdvanceVal = 0;
    const auto* notAdvanceCond = new IR::LAnd(new IR::LNot(cond), restrictions);
    const auto* notAdvanceConst = evaluateExpression(advanceExpr, notAdvanceCond);
    // If we can not satisfy the advance, set the condition to nullptr.
    if (advanceConst == nullptr) {
        advanceCond = nullptr;
    } else {
        // Otherwise store both the condition and the computed value in the AdvanceInfo data
        // structure. Do not forget to add the condition that the expression must be equal to the
        // computed value.
        advanceVal = advanceConst->checkedTo<IR::Constant>()->asInt();
        advanceCond = new IR::LAnd(advanceCond, new IR::Equ(advanceConst, advanceExpr));
    }
    // If we can not satisfy the reject, set the condition to nullptr.
    if (notAdvanceConst == nullptr) {
        notAdvanceCond = nullptr;
    } else {
        // Otherwise store both the condition and the computed value in the AdvanceInfo data
        // structure. Do not forget to add the condition that the expression must be equal to the
        // computed value.
        notAdvanceVal = notAdvanceConst->checkedTo<IR::Constant>()->asInt();
        notAdvanceCond = new IR::LAnd(notAdvanceCond, new IR::Equ(notAdvanceConst, advanceExpr));
    }
    return {advanceVal, advanceCond, notAdvanceVal, notAdvanceCond};
}

void ExprStepper::generateCopyIn(ExecutionState* nextState, const IR::Expression* targetPath,
                                 const IR::Expression* srcPath, cstring dir,
                                 bool forceTaint) const {
    // If the direction is out, we do not copy in external values.
    // We set the parameter to uninitialized.
    if (dir == "out") {
        nextState->set(targetPath,
                       programInfo.createTargetUninitialized(targetPath->type, forceTaint));
    } else {
        // Otherwise we use a conventional assignment.
        nextState->set(targetPath, nextState->get(srcPath));
    }
}

void ExprStepper::evalInternalExternMethodCall(const IR::MethodCallExpression* call,
                                               const IR::Expression* receiver, IR::ID name,
                                               const IR::Vector<IR::Argument>* args,
                                               const ExecutionState& state) {
    static const ExternMethodImpls INTERNAL_EXTERN_METHOD_IMPLS({
        /* ======================================================================================
         *  prepend_to_prog_header
         *  This internal extern prepends the input argument to the program packet. This emulates
         *  the prepending of metadata that some P4 targets perform.
         * ======================================================================================
         */
        {"*.prepend_to_prog_header",
         {"hdr"},
         [](const IR::MethodCallExpression* /*call*/, const IR::Expression* /*receiver*/,
            IR::ID& /*methodName*/, const IR::Vector<IR::Argument>* args,
            const ExecutionState& state, SmallStepEvaluator::Result& result) {
             const auto* prependVar = args->at(0)->expression;
             if (!(prependVar->is<IR::Member>() || prependVar->is<IR::PathExpression>() ||
                   prependVar->is<IR::TaintExpression>() || prependVar->is<IR::Constant>())) {
                 TESTGEN_UNIMPLEMENTED("Prepend input %1% of type %2% not supported", prependVar,
                                       prependVar->type);
             }
             auto* nextState = new ExecutionState(state);

             if (const auto* prependType = prependVar->type->to<IR::Type_StructLike>()) {
                 // We only support flat assignments, so retrieve all fields from the input
                 // argument.
                 const auto flatFields = nextState->getFlatFields(prependVar, prependType);
                 // Iterate through the fields in reverse order.
                 // We need to append in reverse order since we are prepending to the input
                 // packet.
                 for (auto fieldIt = flatFields.rbegin(); fieldIt != flatFields.rend(); ++fieldIt) {
                     const auto* fieldRef = *fieldIt;
                     // Prepend the field to the packet buffer.
                     nextState->prependToPacketBuffer(nextState->get(fieldRef));
                 }

             } else if (prependVar->type->is<IR::Type_Bits>()) {
                 // Prepend the field to the packet buffer.
                 if (const auto* prependVarMember = prependVar->to<IR::Member>()) {
                     nextState->add(
                         new TraceEvent::Extract(prependVarMember, prependVarMember, "prepend"));
                 }
                 nextState->prependToPacketBuffer(prependVar);

             } else {
                 TESTGEN_UNIMPLEMENTED("Prepend input %1% of type %2% not supported", prependVar,
                                       prependVar->type);
             }
             nextState->popBody();
             result->emplace_back(nextState);
         }},
        /* ======================================================================================
         *  append_to_prog_header
         *  This internal extern appends the input argument to the program packet. This emulates
         *  the appending of metadata that some P4 targets perform.
         * ======================================================================================
         */
        {"*.append_to_prog_header",
         {"hdr"},
         [](const IR::MethodCallExpression* /*call*/, const IR::Expression* /*receiver*/,
            IR::ID& /*methodName*/, const IR::Vector<IR::Argument>* args,
            const ExecutionState& state, SmallStepEvaluator::Result& result) {
             const auto* appendVar = args->at(0)->expression;
             if (!(appendVar->is<IR::Member>() || appendVar->is<IR::PathExpression>() ||
                   appendVar->is<IR::TaintExpression>() || appendVar->is<IR::Constant>())) {
                 TESTGEN_UNIMPLEMENTED("append input %1% of type %2% not supported", appendVar,
                                       appendVar->type);
             }
             auto* nextState = new ExecutionState(state);

             if (const auto* appendType = appendVar->type->to<IR::Type_StructLike>()) {
                 // We only support flat assignments, so retrieve all fields from the input
                 // argument.
                 const auto flatFields = nextState->getFlatFields(appendVar, appendType);
                 for (const auto* fieldRef : flatFields) {
                     nextState->appendToPacketBuffer(nextState->get(fieldRef));
                 }
             } else if (appendVar->type->is<IR::Type_Bits>()) {
                 if (const auto* appendVarMember = appendVar->to<IR::Member>()) {
                     nextState->add(
                         new TraceEvent::Extract(appendVarMember, appendVarMember, "append"));
                 }
                 nextState->appendToPacketBuffer(appendVar);
             } else {
                 TESTGEN_UNIMPLEMENTED("Append input %1% of type %2% not supported", appendVar,
                                       appendVar->type);
             }
             nextState->popBody();
             result->emplace_back(nextState);
         }},
        /* ======================================================================================
         *  prepend_emit_buffer
         *  This internal extern appends the emit buffer which was assembled with emit calls to the
         * live packet buffer.The combination of the emit buffer and live packet buffer will form
         * the output packet, which can either be emitted or forwarded to the next parser.
         * ======================================================================================
         */
        {"*.prepend_emit_buffer",
         {},
         [](const IR::MethodCallExpression* /*call*/, const IR::Expression* /*receiver*/,
            IR::ID& /*methodName*/, const IR::Vector<IR::Argument>* /*args*/,
            const ExecutionState& state, SmallStepEvaluator::Result& result) {
             auto* nextState = new ExecutionState(state);
             const auto* emitBuffer = state.getEmitBuffer();
             nextState->prependToPacketBuffer(emitBuffer);
             nextState->add(
                 new TraceEvent::Generic("Prepending the emit buffer to the program packet."));
             nextState->popBody();
             result->emplace_back(nextState);
         }},
        /* ======================================================================================
         *  drop_and_exit
         *  This internal extern drops the entire packet and exits.
         *  We do this by clearing the packet variable and pushing an exit continuation.
         * ======================================================================================
         */
        {"*.drop_and_exit",
         {},
         [this](const IR::MethodCallExpression* /*call*/, const IR::Expression* /*receiver*/,
                IR::ID& /*methodName*/, const IR::Vector<IR::Argument>* /*args*/,
                const ExecutionState& state, SmallStepEvaluator::Result& result) {
             auto* nextState = new ExecutionState(state);
             // If the drop variable is tainted, we also mark the port tainted.
             if (state.hasTaint(programInfo.dropIsActive())) {
                 nextState->set(programInfo.getTargetOutputPortVar(),
                                programInfo.createTargetUninitialized(
                                    programInfo.getTargetOutputPortVar()->type, true));
             }
             nextState->add(new TraceEvent::Generic("Packet marked dropped."));
             nextState->setProperty("drop", true);
             nextState->replaceTopBody(Continuation::Exception::Drop);
             result->emplace_back(nextState);
         }},
        /* ======================================================================================
         *  copy_in
         *  Copies values from @param srcRef to @param targetParam following the
         *  copy-in/copy-out semantics of P4. We use this function to copy values in and out of
         *  individual program pipes.
         *  @param direction signifies the qualified class of the targetParam ("in", "inout", "out",
         *  or "<none>").
         *  All parameters that have direction "out" are set uninitialized.
         * ======================================================================================
         */
        {"*.copy_in",
         {"srcRef", "targetParam", "direction", "forceUninitialized"},
         [this](const IR::MethodCallExpression* /*call*/, const IR::Expression* /*receiver*/,
                IR::ID& /*methodName*/, const IR::Vector<IR::Argument>* args,
                const ExecutionState& state, SmallStepEvaluator::Result& result) {
             const auto* globalRef = args->at(0)->expression;
             if (!(globalRef->is<IR::Member>() || globalRef->is<IR::PathExpression>())) {
                 TESTGEN_UNIMPLEMENTED("Global input %1% of type %2% not supported", globalRef,
                                       globalRef->type);
             }

             const auto* argRef = args->at(1)->expression;
             if (!(argRef->is<IR::Member>() || argRef->is<IR::PathExpression>())) {
                 TESTGEN_UNIMPLEMENTED("Param input %1% of type %2% not supported", argRef,
                                       argRef->type);
             }

             const auto* direction = args->at(2)->expression->checkedTo<IR::StringLiteral>();
             const auto* forceTaint = args->at(3)->expression->checkedTo<IR::BoolLiteral>();

             auto* nextState = new ExecutionState(state);
             // Get the current level and disable it for these operations to avoid overtainting.
             auto currentTaint = state.getProperty<bool>("inUndefinedState");
             nextState->setProperty("inUndefinedState", false);

             auto dir = direction->value;
             const auto* assignType = globalRef->type;
             if (const auto* ts = assignType->to<IR::Type_StructLike>()) {
                 std::vector<const IR::Member*> flatRefValids;
                 std::vector<const IR::Member*> flatParamValids;
                 auto flatRefFields = nextState->getFlatFields(globalRef, ts, &flatRefValids);
                 auto flatParamFields = nextState->getFlatFields(argRef, ts, &flatParamValids);
                 // In case of a header, we also need to copy the validity bits.
                 // The only difference here is that, in the case of a copy-in out parameter, we
                 // set validity to false.
                 // TODO: Find a more elegant method to handle this instead of code duplication.
                 for (size_t idx = 0; idx < flatRefValids.size(); ++idx) {
                     const auto* fieldGlobalValid = flatRefValids[idx];
                     const auto* fieldParamValid = flatParamValids[idx];
                     // If the validity bit did not exist before, initialize it to be false.
                     if (!nextState->exists(fieldGlobalValid)) {
                         nextState->set(fieldGlobalValid, IRUtils::getBoolLiteral(false));
                     }
                     // Set them false in case of an out copy-in.
                     if (dir == "out") {
                         nextState->set(fieldParamValid, IRUtils::getBoolLiteral(false));
                     } else {
                         nextState->set(fieldParamValid, nextState->get(fieldGlobalValid));
                     }
                 }
                 // First, complete the assignments for the data structure.
                 for (size_t idx = 0; idx < flatRefFields.size(); ++idx) {
                     const auto* fieldGlobalRef = flatRefFields[idx];
                     const auto* fieldargRef = flatParamFields[idx];
                     generateCopyIn(nextState, fieldargRef, fieldGlobalRef, dir, forceTaint->value);
                 }
             } else if (assignType->is<IR::Type_Base>()) {
                 generateCopyIn(nextState, argRef, globalRef, dir, forceTaint->value);
             } else {
                 P4C_UNIMPLEMENTED("Unsupported copy_out type %1%", assignType->node_type_name());
             }
             // Push back the current taint level.
             nextState->setProperty("inUndefinedState", currentTaint);
             nextState->popBody();
             result->emplace_back(nextState);
             return false;
         }},
        /* ======================================================================================
         *  copy_out
         *  copies values from @param srcRef to @param targetParam following the
         *  copy-in/copy-out semantics of P4. We use this function to copy values in and out of
         *  individual program pipes. We copy all values
         *  that are (In)out from srcRef to inputRef. @param direction signifies the
         *  qualified class of the srcRef ("in", "inout", "out", or "<none>").
         * ======================================================================================
         */
        {"*.copy_out",
         {"targetParam", "srcRef", "direction"},
         [](const IR::MethodCallExpression* /*call*/, const IR::Expression* /*receiver*/,
            IR::ID& /*methodName*/, const IR::Vector<IR::Argument>* args,
            const ExecutionState& state, SmallStepEvaluator::Result& result) {
             const auto* globalRef = args->at(0)->expression;
             if (!(globalRef->is<IR::Member>() || globalRef->is<IR::PathExpression>())) {
                 TESTGEN_UNIMPLEMENTED("Global input %1% of type %2% not supported", globalRef,
                                       globalRef->type);
             }

             const auto* argRef = args->at(1)->expression;
             if (!(argRef->is<IR::Member>() || argRef->is<IR::PathExpression>())) {
                 TESTGEN_UNIMPLEMENTED("Param input %1% of type %2% not supported", argRef,
                                       argRef->type);
             }

             const auto* direction = args->at(2)->expression->checkedTo<IR::StringLiteral>();

             auto* nextState = new ExecutionState(state);
             // Get the current level and disable it for these operations to avoid overtainting.
             auto currentTaint = state.getProperty<bool>("inUndefinedState");
             nextState->setProperty("inUndefinedState", false);

             auto dir = direction->value;
             const auto* assignType = globalRef->type;
             if (const auto* ts = assignType->to<IR::Type_StructLike>()) {
                 std::vector<const IR::Member*> flatRefValids;
                 std::vector<const IR::Member*> flatParamValids;
                 auto flatRefFields = nextState->getFlatFields(globalRef, ts, &flatRefValids);
                 auto flatParamFields = nextState->getFlatFields(argRef, ts, &flatParamValids);
                 // In case of a header, we also need to copy the validity bits.
                 for (size_t idx = 0; idx < flatRefValids.size(); ++idx) {
                     const auto* fieldGlobalValid = flatRefValids[idx];
                     const auto* fieldParamValid = flatParamValids[idx];
                     if (dir == "inout" || dir == "out") {
                         nextState->set(fieldGlobalValid, nextState->get(fieldParamValid));
                     }
                 }
                 // First, complete the assignments for the data structure.
                 for (size_t idx = 0; idx < flatRefFields.size(); ++idx) {
                     const auto* fieldGlobalRef = flatRefFields[idx];
                     const auto* fieldargRef = flatParamFields[idx];
                     if (dir == "inout" || dir == "out") {
                         nextState->set(fieldGlobalRef, nextState->get(fieldargRef));
                     }
                 }
             } else if (assignType->is<IR::Type_Base>()) {
                 if (dir == "inout" || dir == "out") {
                     nextState->set(globalRef, nextState->get(argRef));
                 }
             } else {
                 P4C_UNIMPLEMENTED("Unsupported copy_out type %1%", assignType->node_type_name());
             }
             // Push back the current taint level.
             nextState->setProperty("inUndefinedState", currentTaint);
             nextState->popBody();
             result->emplace_back(nextState);
             return false;
         }},
    });
    // Provides implementations of extern calls internal to the interpreter.
    // These calls do not exist in P4.
    if (!INTERNAL_EXTERN_METHOD_IMPLS.exec(call, receiver, name, args, state, result)) {
        BUG("Unknown or unimplemented extern method: %1%", name);
    }
}

void ExprStepper::evalExternMethodCall(const IR::MethodCallExpression* call,
                                       const IR::Expression* receiver, IR::ID name,
                                       const IR::Vector<IR::Argument>* args,
                                       ExecutionState& state) {
    // Provides implementations of all known extern methods built into P4 core.
    static const ExternMethodImpls CORE_EXTERN_METHOD_IMPLS({
        /* ======================================================================================
         *  packet_in.lookahead
         *  Read bits from the packet without advancing the cursor.
         *  @returns: the bits read from the packet.
         *  T may be an arbitrary fixed-size type.
         *  T lookahead<T>();
         * ======================================================================================
         */
        {"packet_in.lookahead",
         {},
         [this](const IR::MethodCallExpression* call, const IR::Expression* /*receiver*/,
                IR::ID& /*methodName*/, const IR::Vector<IR::Argument>* /*args*/,
                const ExecutionState& state, SmallStepEvaluator::Result& result) {
             const auto* typeArgs = call->typeArguments;
             BUG_CHECK(typeArgs->size() == 1, "Lookahead should have exactly one type argument.");
             const auto* lookaheadType = call->typeArguments->at(0);
             if (!lookaheadType->is<IR::Type_Base>()) {
                 TESTGEN_UNIMPLEMENTED(
                     "Lookahead type %1% not supported. Expected a base type. Got %2%",
                     lookaheadType, lookaheadType->node_type_name());
             }
             // Calculate the conditions for a failed or successful lookahead, given the size.
             auto lookaheadSize = lookaheadType->width_bits();
             auto condInfo = calculateSuccessfulParserAdvance(state, lookaheadType->width_bits());

             // Evaluate the case where the packet is large enough.
             if (condInfo.advanceCond != nullptr) {
                 // Record the condition we evaluate as string.
                 std::stringstream condStream;
                 condStream << "Lookahead Condition: ";
                 condInfo.advanceCond->dbprint(condStream);
                 auto* nextState = new ExecutionState(state);
                 // Peek into the buffer, we do NOT slice from it.
                 const auto* lookaheadVar = nextState->peekPacketBuffer(lookaheadSize);
                 nextState->add(new TraceEvent::Expression(lookaheadVar, "Lookahead result"));
                 // Record the condition we are passing at this at this point.
                 nextState->add(new TraceEvent::Generic(condStream.str()));
                 nextState->replaceTopBody(Continuation::Return(lookaheadVar));
                 result->emplace_back(condInfo.advanceCond, state, nextState);
             }
             // Handle the case where the packet is too short.
             if (condInfo.advanceFailCond != nullptr) {
                 auto* rejectState = new ExecutionState(state);
                 // Record the condition we are failing at this at this point.
                 rejectState->add(new TraceEvent::Generic("Lookahead: Packet too short"));
                 rejectState->replaceTopBody(Continuation::Exception::PacketTooShort);
                 result->emplace_back(condInfo.advanceFailCond, state, rejectState);
             }
         }},
        /* ======================================================================================
         *  packet_in.advance
         *  Advance the packet cursor by the specified number of bits.
         * ======================================================================================
         */
        {"packet_in.advance",
         {"sizeInBits"},
         [this](const IR::MethodCallExpression* call, const IR::Expression* /*receiver*/,
                IR::ID& /*methodName*/, const IR::Vector<IR::Argument>* args,
                const ExecutionState& state, SmallStepEvaluator::Result& result) {
             const auto* advanceExpr = args->at(0)->expression;

             if (!SymbolicEnv::isSymbolicValue(advanceExpr)) {
                 stepToSubexpr(advanceExpr, result, state,
                               [call](const Continuation::Parameter* v) {
                                   auto* clonedCall = call->clone();
                                   auto* arguments = clonedCall->arguments->clone();
                                   auto* arg = arguments->at(0)->clone();
                                   arg->expression = v->param;
                                   (*arguments)[0] = arg;
                                   clonedCall->arguments = arguments;
                                   return Continuation::Return(clonedCall);
                               });
                 return;
             }

             // There are two possibilities. Either the advance amount is a constant or a runtime
             // expression. In the first case, we just need to retrieve the value from the constant.
             PacketCursorAdvanceInfo condInfo{};
             if (const auto* advanceConst = advanceExpr->to<IR::Constant>()) {
                 auto advanceValue = advanceConst->asInt();
                 // Calculate the conditions for a failed or successful advance, given the size.
                 condInfo = calculateSuccessfulParserAdvance(state, advanceValue);
             } else {
                 // Check whether advance expression is tainted.
                 // If that is the case, we have no control or idea how much the cursor can be
                 // advanced.
                 auto advanceIsTainted = state.hasTaint(advanceExpr);
                 if (advanceIsTainted) {
                     TESTGEN_UNIMPLEMENTED(
                         "The advance expression of %1% is tainted. We can not predict how much "
                         "this call will advance the parser cursor. Abort.",
                         call);
                 }
                 // In the second case, where the advance amount is a runtime expression, we need to
                 // invoke the solver.
                 // The size of the advance expression should be smaller than the maximum packet
                 // size.
                 auto* sizeRestriction = new IR::Leq(
                     advanceExpr, IRUtils::getConstant(advanceExpr->type,
                                                       ExecutionState::getMaxPacketLength_bits()));
                 // The advance expression should ideally have a size that is a multiple of 8 bits.
                 auto* bytesRestriction = new IR::Equ(
                     new IR::Mod(advanceExpr, IRUtils::getConstant(advanceExpr->type, 8)),
                     IRUtils::getConstant(advanceExpr->type, 0));
                 auto* restrictions = new IR::LAnd(sizeRestriction, bytesRestriction);
                 condInfo = calculateAdvanceExpression(state, advanceExpr, restrictions);
             }

             // Evaluate the case where the packet is large enough.
             if (condInfo.advanceCond != nullptr) {
                 // Record the condition we evaluate as string.
                 std::stringstream condStream;
                 condStream << "Advance Condition: ";
                 condInfo.advanceCond->dbprint(condStream);
                 // Advancing by zero can be considered a no-op.
                 if (condInfo.advanceSize == 0) {
                     auto* nextState = new ExecutionState(state);
                     nextState->add(new TraceEvent::Generic("Advance: 0 bits."));
                     nextState->popBody();
                     result->emplace_back(nextState);
                 } else {
                     auto* nextState = new ExecutionState(state);
                     // Slice from the buffer and append to the packet, if necessary.
                     nextState->slicePacketBuffer(condInfo.advanceSize);

                     // Record the condition we are passing at this at this point.
                     nextState->add(new TraceEvent::Generic(condStream.str()));
                     nextState->popBody();
                     result->emplace_back(condInfo.advanceCond, state, nextState);
                 }
             }
             if (condInfo.advanceFailCond != nullptr) {
                 // Handle the case where the packet is too short.
                 auto* rejectState = new ExecutionState(state);
                 // Record the condition we are failing at this at this point.
                 rejectState->add(new TraceEvent::Generic("Advance: Packet too short"));
                 rejectState->replaceTopBody(Continuation::Exception::PacketTooShort);
                 result->emplace_back(condInfo.advanceFailCond, state, rejectState);
             }
         }},
        /* ======================================================================================
         *  packet_in.extract
         *  When we call extract, we assign a value to the input by slicing a section of the
         *  active program packet. We then advance the parser cursor. The parser cursor
         *  remains in the most recent position until we enter a new start parser.
         * ======================================================================================
         */
        {"packet_in.extract",
         {"hdr"},
         [this](const IR::MethodCallExpression* call, const IR::Expression* /*receiver*/,
                IR::ID& /*methodName*/, const IR::Vector<IR::Argument>* args,
                const ExecutionState& state, SmallStepEvaluator::Result& result) {
             // This argument is the structure being written by the extract.
             const auto* extractOutput = args->at(0)->expression;

             // Get the extractedType
             const auto* typeArgs = call->typeArguments;
             BUG_CHECK(typeArgs->size() == 1, "Must have exactly 1 type argument for extract. %1%",
                       call);

             const auto* initialType = state.resolveType(typeArgs->at(0));
             const auto* extractedType = initialType->checkedTo<IR::Type_StructLike>();

             // Calculate the conditions for a failed or successful advance, given the size.
             auto extractSize = extractedType->width_bits();
             auto condInfo = calculateSuccessfulParserAdvance(state, extractSize);

             // Evaluate the case where the packet is large enough.
             if (condInfo.advanceCond != nullptr) {
                 auto* nextState = new ExecutionState(state);

                 // If we are dealing with a header, set the header valid.
                 if (extractedType->is<IR::Type_Header>()) {
                     setHeaderValidity(extractOutput, true, nextState);
                 }

                 // We only support flat assignments, so retrieve all fields from the input
                 // argument.
                 const std::vector<const IR::Member*> flatFields =
                     nextState->getFlatFields(extractOutput, extractedType);
                 nextState->add(new TraceEvent::Generic("Extract: Succeeded"));
                 /// Iterate over all the fields that need to be set.
                 setFields(nextState, flatFields, 0);

                 // Record the condition we are passing at this at this point.
                 // Record the condition we evaluate as string.
                 std::stringstream condStream;
                 condStream << "Extract Condition: ";
                 condInfo.advanceCond->dbprint(condStream);
                 condStream << " | Extract Size: " << condInfo.advanceSize;
                 nextState->add(new TraceEvent::Generic(condStream.str()));
                 nextState->popBody();
                 result->emplace_back(condInfo.advanceCond, state, nextState);
             }

             // Handle the case where the packet is too short.
             if (condInfo.advanceFailCond != nullptr) {
                 auto* rejectState = new ExecutionState(state);
                 // Record the condition we are failing at this at this point.
                 rejectState->add(new TraceEvent::Generic("Extract: Packet too short"));
                 std::stringstream condStream;
                 condStream << "Extract Failure Condition: ";
                 condInfo.advanceFailCond->dbprint(condStream);
                 condStream << " | Extract Size: " << condInfo.advanceFailSize;
                 rejectState->add(new TraceEvent::Generic(condStream.str()));
                 rejectState->replaceTopBody(Continuation::Exception::PacketTooShort);

                 result->emplace_back(condInfo.advanceFailCond, state, rejectState);
             }
         }},
        {"packet_in.extract",
         {"hdr", "sizeInBits"},
         [this](const IR::MethodCallExpression* call, const IR::Expression* /*receiver*/,
                IR::ID& /*methodName*/, const IR::Vector<IR::Argument>* args,
                const ExecutionState& state, SmallStepEvaluator::Result& result) {
             // This argument is the structure being written by the extract.
             const auto* extractOutput = args->at(0)->expression;
             const auto* varbitExtractExpr = args->at(1)->expression;
             if (!SymbolicEnv::isSymbolicValue(varbitExtractExpr)) {
                 stepToSubexpr(varbitExtractExpr, result, state,
                               [call](const Continuation::Parameter* v) {
                                   auto* clonedCall = call->clone();
                                   auto* arguments = clonedCall->arguments->clone();
                                   auto* arg = arguments->at(1)->clone();
                                   arg->expression = v->param;
                                   (*arguments)[1] = arg;
                                   clonedCall->arguments = arguments;
                                   return Continuation::Return(clonedCall);
                               });
                 return;
             }

             // Get the extractedType
             const auto* typeArgs = call->typeArguments;
             BUG_CHECK(typeArgs->size() == 1, "Must have exactly 1 type argument for extract. %1%",
                       call);

             const auto* initialType = state.resolveType(typeArgs->at(0));
             const auto* extractedType = initialType->checkedTo<IR::Type_StructLike>();
             auto extractSize = extractedType->width_bits();

             // Try to find the varbit inside the header we are extracting.
             const IR::Extracted_Varbits* varbit = nullptr;
             for (const auto* fieldRef : extractedType->fields) {
                 if (const auto* varbitTmp = fieldRef->type->to<IR::Extracted_Varbits>()) {
                     varbit = varbitTmp;
                     break;
                 }
             }
             BUG_CHECK(varbit != nullptr, "No varbit type present in this structure! %1%", call);

             int varBitFieldSize = 0;
             PacketCursorAdvanceInfo condInfo{};
             if (const auto* varbitConst = varbitExtractExpr->to<IR::Constant>()) {
                 varBitFieldSize = varbitConst->asInt();
                 condInfo = calculateSuccessfulParserAdvance(state, varBitFieldSize + extractSize);
             } else {
                 // Check whether advance expression is tainted.
                 // If that is the case, we have no control or idea how much the cursor can be
                 // advanced.
                 auto advanceIsTainted = state.hasTaint(varbitExtractExpr);
                 if (advanceIsTainted) {
                     TESTGEN_UNIMPLEMENTED(
                         "The varbit expression of %1% is tainted. We can not predict how much "
                         "this call will advance the parser cursor. Abort.",
                         call);
                 }
                 // The size of the advance expression should be smaller than the maximum packet
                 // size.
                 auto maxVarbit = std::min(ExecutionState::getMaxPacketLength_bits(), varbit->size);
                 auto* sizeRestriction = new IR::Leq(
                     varbitExtractExpr, IRUtils::getConstant(varbitExtractExpr->type, maxVarbit));
                 // The advance expression should ideally fit into a multiple of 8 bits.
                 auto* bytesRestriction =
                     new IR::Equ(new IR::Mod(varbitExtractExpr,
                                             IRUtils::getConstant(varbitExtractExpr->type, 8)),
                                 IRUtils::getConstant(varbitExtractExpr->type, 0));
                 // The advance expression should not be larger than the varbit maximum width.
                 auto* restrictions = new IR::LAnd(sizeRestriction, bytesRestriction);
                 // In the second case, where the advance amount is a runtime expression, we need to
                 // invoke the solver.
                 varbitExtractExpr = new IR::Add(
                     varbitExtractExpr, IRUtils::getConstant(varbitExtractExpr->type, extractSize));
                 condInfo = calculateAdvanceExpression(state, varbitExtractExpr, restrictions);
                 varBitFieldSize = std::max(0, condInfo.advanceSize - extractSize);
             }
             // Evaluate the case where the packet is large enough.
             if (condInfo.advanceCond != nullptr) {
                 // If the extract amount exceeds the size of the varbit field, fail.
                 if (varbit->size < varBitFieldSize) {
                     auto* nextState = new ExecutionState(state);
                     nextState->set(state.getCurrentParserErrorLabel(),
                                    IRUtils::getConstant(programInfo.getParserErrorType(), 4));
                     nextState->replaceTopBody(Continuation::Exception::Reject);
                     result->emplace_back(condInfo.advanceCond, state, nextState);
                     return;
                 }
                 auto* nextState = new ExecutionState(state);
                 // If we are dealing with a header, set the header valid.
                 if (extractedType->is<IR::Type_Header>()) {
                     setHeaderValidity(extractOutput, true, nextState);
                 }

                 // We only support flat assignments, so retrieve all fields from the input
                 // argument.
                 const std::vector<const IR::Member*> flatFields =
                     nextState->getFlatFields(extractOutput, extractedType);

                 /// Iterate over all the fields that need to be set.
                 setFields(nextState, flatFields, varBitFieldSize);

                 nextState->add(new TraceEvent::Extract(IRUtils::getHeaderValidity(extractOutput),
                                                        IRUtils::getHeaderValidity(extractOutput)));
                 // Record the condition we are passing at this at this point.
                 std::stringstream condStream;
                 condStream << "Extract Condition: ";
                 condInfo.advanceCond->dbprint(condStream);
                 condStream << " | Extract Size: " << condInfo.advanceSize;
                 nextState->add(new TraceEvent::Generic(condStream.str()));
                 nextState->popBody();
                 result->emplace_back(condInfo.advanceCond, state, nextState);
             }

             // Handle the case where the packet is too short.
             if (condInfo.advanceFailCond != nullptr) {
                 auto* rejectState = new ExecutionState(state);
                 // Record the condition we are failing at this at this point.
                 rejectState->add(new TraceEvent::Generic("Extract: Packet too short"));
                 std::stringstream condStream;
                 condStream << "Extract Failure Condition: ";
                 condInfo.advanceFailCond->dbprint(condStream);
                 condStream << " | Extract Size: " << condInfo.advanceFailSize;
                 rejectState->replaceTopBody(Continuation::Exception::PacketTooShort);
                 result->emplace_back(condInfo.advanceFailCond, state, rejectState);
             }
         }},
        /* ======================================================================================
         *  packet_out.emit
         *  When we call emit, we append the emitted value to the active program packet.
         *  We use a concatenation for this.
         * ======================================================================================
         */
        {"packet_out.emit",
         {"hdr"},
         [](const IR::MethodCallExpression* /*call*/, const IR::Expression* /*receiver*/,
            IR::ID& /*methodName*/, const IR::Vector<IR::Argument>* args,
            const ExecutionState& state, SmallStepEvaluator::Result& result) {
             const auto* emitOutput = args->at(0)->expression;
             const auto* emitType = emitOutput->type->checkedTo<IR::Type_StructLike>();
             if (!emitOutput->is<IR::Member>()) {
                 TESTGEN_UNIMPLEMENTED("Emit input %1% of type %2% not supported", emitOutput,
                                       emitType);
             }
             const auto& validVar = IRUtils::getHeaderValidity(emitOutput);

             // Check whether the validity bit of the header is tainted. If it is, the entire
             // emit is tainted. There is not much we can do here, so throw an error.
             auto emitIsTainted = state.hasTaint(validVar);
             if (emitIsTainted) {
                 TESTGEN_UNIMPLEMENTED(
                     "The validity bit of %1% is tainted. Tainted emit calls can not be "
                     "mitigated "
                     "because it is unclear whether the header will be emitted. Abort.",
                     emitOutput);
             }
             // This call assumes that the "expandEmit" midend pass is being used. expandEmit
             // unravels emit calls on structs into emit calls on the header members.
             {
                 auto* nextState = new ExecutionState(state);
                 for (const auto* field : emitType->fields) {
                     const auto* fieldType = field->type;
                     if (fieldType->is<IR::Type_StructLike>()) {
                         BUG("Unexpected emit field %1% of type %2%", field, fieldType);
                     }
                     const auto* fieldRef = new IR::Member(fieldType, emitOutput, field->name);
                     const IR::Expression* fieldExpr = nextState->get(fieldRef);
                     fieldType = fieldExpr->type;
                     if (const auto* varbits = fieldType->to<IR::Extracted_Varbits>()) {
                         fieldType = IRUtils::getBitType(varbits->assignedSize);
                     }

                     auto fieldWidth = fieldType->width_bits();
                     // If the width is zero, do not bother with emitting.
                     if (fieldWidth == 0) {
                         continue;
                     }

                     nextState->add(new TraceEvent::Emit(fieldRef, fieldExpr));
                     // This check is necessary because the argument for Concat must be a
                     // Type_Bits expression.
                     if (fieldType->is<IR::Type_Boolean>()) {
                         fieldExpr = new IR::Cast(IR::Type_Bits::get(1), fieldExpr);
                     }
                     // If the bit type is signed, we also need to cast to unsigned. This is
                     // necessary to prevent incorrect constant folding.
                     if (const auto* bits = fieldType->to<IR::Type_Bits>()) {
                         if (bits->isSigned) {
                             auto* cloneType = bits->clone();
                             cloneType->isSigned = false;
                             fieldExpr = new IR::Cast(cloneType, fieldExpr);
                         }
                     }
                     // Append to the emit buffer.
                     nextState->appendToEmitBuffer(fieldExpr);
                 }
                 // Adjust the packet delta, an emit increases the delta.
                 std::stringstream totalStream;
                 nextState->add(new TraceEvent::Emit(validVar, validVar));
                 nextState->popBody();
                 // Only when the header is valid, the members are emitted and the packet
                 // delta is adjusted.
                 result->emplace_back(state.get(validVar), state, nextState);
             }
             {
                 auto* invalidState = new ExecutionState(state);
                 std::stringstream traceString;
                 traceString << "Invalid emit: ";
                 validVar->dbprint(traceString);
                 invalidState->add(new TraceEvent::Expression(validVar, traceString));
                 invalidState->popBody();
                 result->emplace_back(new IR::LNot(IR::Type::Boolean::get(), validVar), state,
                                      invalidState);
             }
         }},
        /* ======================================================================================
         *  verify
         *  The verify statement provides a simple form of error handling.
         *  If the first argument is true, then executing the statement has no side-effect.
         *  However, if the first argument is false, it causes an immediate transition to
         *  reject, which causes immediate parsing termination; at the same time, the
         *  parserError associated with the parser is set to the value of the second
         *  argument.
         * ======================================================================================
         */
        {"*method.verify",
         {"bool", "error"},
         [this](const IR::MethodCallExpression* call, const IR::Expression* /*receiver*/,
                IR::ID& /*name*/, const IR::Vector<IR::Argument>* args, const ExecutionState& state,
                SmallStepEvaluator::Result& result) {
             const auto* cond = args->at(0)->expression;
             const auto* error = args->at(1)->expression->checkedTo<IR::Constant>();
             if (!SymbolicEnv::isSymbolicValue(cond)) {
                 // Evaluate the condition.
                 stepToSubexpr(cond, result, state, [call](const Continuation::Parameter* v) {
                     auto* clonedCall = call->clone();
                     const auto* error = clonedCall->arguments->at(1);
                     auto* arguments = new IR::Vector<IR::Argument>();
                     arguments->push_back(new IR::Argument(v->param));
                     arguments->push_back(error);
                     clonedCall->arguments = arguments;
                     return Continuation::Return(clonedCall);
                 });
                 return;
             }

             // If the verify condition is tainted, the error is also tainted.
             if (state.hasTaint(cond)) {
                 auto* taintedState = new ExecutionState(state);
                 std::stringstream traceString;
                 traceString << "Tainted verify: ";
                 cond->dbprint(traceString);
                 taintedState->add(new TraceEvent::Expression(cond, traceString));
                 const auto* errVar = state.getCurrentParserErrorLabel();
                 taintedState->set(errVar, IRUtils::getTaintExpression(errVar->type));
                 taintedState->popBody();
                 result->emplace_back(taintedState);
                 return;
             }

             // Handle the case where the condition is true.
             auto* nextState = new ExecutionState(state);
             nextState->popBody();
             result->emplace_back(cond, state, nextState);
             // Handle the case where the condition is false.
             auto* falseState = new ExecutionState(state);
             const auto* errVar = state.getCurrentParserErrorLabel();
             falseState->set(errVar,
                             IRUtils::getConstant(programInfo.getParserErrorType(), error->value));
             falseState->replaceTopBody(Continuation::Exception::Reject);
             result->emplace_back(new IR::LNot(IR::Type::Boolean::get(), cond), state, falseState);
         }},
    });

    if (!CORE_EXTERN_METHOD_IMPLS.exec(call, receiver, name, args, state, result)) {
        // Lastly, check whether we are calling an internal extern method.
        evalInternalExternMethodCall(call, receiver, name, args, state);
    }
}

}  // namespace P4Testgen

}  // namespace P4Tools
