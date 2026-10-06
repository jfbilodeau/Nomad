// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/Boolean.hpp>
#include <nomad/system/Float.hpp>
#include <nomad/system/Integer.hpp>
#include <nomad/script/FormatString.hpp>

#include <nomad/script/OpCode.hpp>
#include <nomad/script/Runtime.hpp>

#include <cassert>

namespace nomad {

void op_nop(VirtualMachine* /*interpreter*/) {
    // Do nothing...
}

void op_breakpoint(VirtualMachine* /*interpreter*/) {
    // TODO: Replace with std::breakpoint in C++26
    assert(false);
}

void op_stop(VirtualMachine* interpreter) {
    interpreter->stop(interpreter->getResult());
}

void op_yield(VirtualMachine* interpreter) {
    interpreter->yield(interpreter->getResult());
}

void op_return(VirtualMachine* interpreter) {
    interpreter->returnFunction(0);
}

void op_return_n(VirtualMachine* interpreter) {
    const auto variable_count = interpreter->nextIndex();

    interpreter->returnFunction(variable_count);
}

void op_jump(VirtualMachine* interpreter) {
    const auto jumpIndex = interpreter->nextIndex();

    interpreter->jump(jumpIndex);
}

void op_jump_if(VirtualMachine* interpreter) {
    const auto jumpIndex = interpreter->nextIndex();
    const auto condition = interpreter->getBooleanResult();

    if (condition) {
        interpreter->jump(jumpIndex);
    }
}

void op_jump_if_false(VirtualMachine* interpreter) {
    const auto jumpIndex = interpreter->nextIndex();
    const auto condition = interpreter->getBooleanResult();

    if (!condition) {
        interpreter->jump(jumpIndex);
    }
}

void op_push_r(VirtualMachine* interpreter) {
    interpreter->pushResult();
}

void op_push_i(VirtualMachine* interpreter) {
    interpreter->pushIntermediate();
}

void op_pop_r(VirtualMachine* interpreter) {
    interpreter->popResult();
}

void op_pop_i(VirtualMachine* interpreter) {
    interpreter->popIntermediate();
}

void op_pop_1(VirtualMachine* interpreter) {
    interpreter->pop1();
}

void op_pop_2(VirtualMachine* interpreter) {
    interpreter->pop2();
}

void op_pop_3(VirtualMachine* interpreter) {
    interpreter->pop3();
}

void op_pop_n(VirtualMachine* interpreter) {
    const auto popCount = interpreter->nextIndex();

    interpreter->popN(popCount);
}

void op_copy_r_to_i(VirtualMachine* interpreter) {
    interpreter->setIntermediate(interpreter->getResult());
}

void op_copy_string_r_to_i(VirtualMachine* interpreter) {
    interpreter->setStringIntermediate(interpreter->getStringResult());
}

void op_string_move_r_to_i(VirtualMachine* interpreter) {
    interpreter->moveStringResultToIntermediate();
}

void op_string_move_r_to_stack(VirtualMachine* interpreter) {
    interpreter->moveStringResultToStack();
}

void op_call_function(VirtualMachine* interpreter) {
    const auto jumpIndex = interpreter->nextIndex();

    const auto runtime = interpreter->getRuntime();
    const auto function = runtime->getFunctionByStartAddress(jumpIndex);

    if (function == nullptr) {
        interpreter->fault("Invalid function start address: " + toString(jumpIndex));
        return;
    }

    interpreter->callFunction(function);
}

void op_call_native_function(VirtualMachine* interpreter) {
    const auto nativeFunctionId = interpreter->nextId();

    interpreter->callNativeFunction(nativeFunctionId);
}

void op_parameter_load_r(VirtualMachine* interpreter) {
    const auto parameterId = interpreter->nextId();

    auto& value = interpreter->getParameter(parameterId);

    interpreter->setResult(value);
}

void op_parameter_load_i(VirtualMachine* interpreter) {
    const auto parameterId = interpreter->nextId();

    auto& value = interpreter->getParameter(parameterId);

    interpreter->setIntermediate(value);
}

void op_parameter_push(VirtualMachine* interpreter) {
    const auto parameterId = interpreter->nextId();

    auto& value = interpreter->getParameter(parameterId);

    interpreter->pushValue(value);
}

void op_string_parameter_load_r(VirtualMachine* interpreter) {
    const auto parameterId = interpreter->nextId();

    const auto value = interpreter->getStringParameter(parameterId);

    interpreter->setStringResult(value);
}

void op_string_parameter_load_i(VirtualMachine* interpreter) {
    const auto parameterId = interpreter->nextId();

    const auto value = interpreter->getStringParameter(parameterId);

    interpreter->setStringIntermediate(value);
}

void op_string_parameter_push(VirtualMachine* interpreter) {
    const auto parameterId = interpreter->nextId();

    const auto& value = interpreter->getParameter(parameterId);

    const auto string = value.getStringValue();

    interpreter->pushString(string);
}

void op_function_variable_set(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    interpreter->setFunctionVariableValue(variableId, interpreter->getResult());
}


void op_function_variable_get_r(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    RuntimeValue value;
    interpreter->getFunctionVariableValue(variableId, value);

    interpreter->setResult(value);
}

void op_function_variable_string_get_r(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    const auto value = interpreter->getFunctionStringVariableValue(variableId);

    interpreter->setStringResult(value);
}

void op_function_variable_get_i(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    RuntimeValue value;
    interpreter->getFunctionVariableValue(variableId, value);

    interpreter->setIntermediate(value);
}

void op_function_variable_string_get_i(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    const auto value = interpreter->getFunctionStringVariableValue(variableId);

    interpreter->setStringIntermediate(value);
}

void op_function_variable_push(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    RuntimeValue value;
    interpreter->getFunctionVariableValue(variableId, value);

    interpreter->pushValue(value);
}

void op_function_variable_string_push(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    const auto value = interpreter->getFunctionStringVariableValue(variableId);

    interpreter->pushString(value);
}

void op_dynamic_variable_set(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    interpreter->getRuntime()->setDynamicVariable(interpreter, variableId, interpreter->getResult());
}

void op_dynamic_variable_string_set(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    interpreter->getRuntime()->setStringDynamicVariable(interpreter, variableId, interpreter->getResult());
}

void op_dynamic_variable_get_r(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    RuntimeValue value;

    interpreter->getRuntime()->getDynamicVariableValue(interpreter, variableId, value);

    interpreter->setResult(value);
}

void op_dynamic_variable_string_get_r(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    // Dynamic getters hand over an owned string, which is moved into the register.
    RuntimeValue value;

    interpreter->getRuntime()->getStringDynamicVariableValue(interpreter, variableId, value);

    interpreter->setResult(value);
}

void op_dynamic_variable_get_i(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    RuntimeValue value;

    interpreter->getRuntime()->getDynamicVariableValue(interpreter, variableId, value);

    interpreter->setIntermediate(value);
}

void op_dynamic_variable_string_get_i(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    // Dynamic getters hand over an owned string, which is moved into the register.
    RuntimeValue value;

    interpreter->getRuntime()->getStringDynamicVariableValue(interpreter, variableId, value);

    interpreter->setIntermediate(value);
}

void op_dynamic_variable_push(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    RuntimeValue value;

    interpreter->getRuntime()->getDynamicVariableValue(interpreter, variableId, value);

    interpreter->pushValue(value);
}

void op_dynamic_variable_string_push(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    // Dynamic getters hand over an owned string, which is moved onto the stack.
    RuntimeValue value;

    interpreter->getRuntime()->getStringDynamicVariableValue(interpreter, variableId, value);

    interpreter->pushValue(value);
}

void op_context_variable_set(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    interpreter->getRuntime()->setContextVariableValue(variableId, interpreter->getResult());
}

void op_context_variable_string_set(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    interpreter->getRuntime()->setStringContextVariableValue(variableId, interpreter->getResult().getStringValue());
}

void op_context_variable_get_r(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->setResult(value);
}

void op_context_variable_string_get_r(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    // The variable context shares its string, so the register receives a copy.
    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->setStringResult(value.getStringValue());
}

void op_context_variable_get_i(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->setIntermediate(value);
}

void op_context_variable_string_get_i(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    // The variable context shares its string, so the register receives a copy.
    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->setStringIntermediate(value.getStringValue());
}

void op_context_variable_push(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->pushValue(value);
}

void op_context_variable_string_push(VirtualMachine* interpreter) {
    const auto variableId = interpreter->nextId();

    // The variable context shares its string, so the stack receives a copy.
    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->pushString(value.getStringValue());
}

void op_boolean_load_false_r(VirtualMachine* interpreter) {
    interpreter->setBooleanResult(NOMAD_FALSE);
}

void op_boolean_load_false_i(VirtualMachine* interpreter) {
    interpreter->setBooleanIntermediate(NOMAD_FALSE);
}

void op_boolean_load_true_r(VirtualMachine* interpreter) {
    interpreter->setBooleanResult(NOMAD_TRUE);
}

void op_boolean_load_true_i(VirtualMachine* interpreter) {
    interpreter->setBooleanIntermediate(NOMAD_TRUE);
}

void op_boolean_push_false(VirtualMachine* interpreter) {
    interpreter->pushBoolean(false);
}

void op_boolean_push_true(VirtualMachine* interpreter) {
    interpreter->pushBoolean(true);
}

void op_boolean_not(VirtualMachine* interpreter) {
    const auto a = interpreter->getBooleanResult();

    const auto result = booleanNot(a);

    interpreter->setBooleanResult(result);
}

void op_boolean_and(VirtualMachine* interpreter) {
    auto a = interpreter->getBooleanResult();
    auto b = interpreter->getBooleanIntermediate();

    auto result = booleanAnd(a, b);

    interpreter->setBooleanResult(result);
}

void op_boolean_or(VirtualMachine* interpreter) {
    auto a = interpreter->getBooleanResult();
    auto b = interpreter->getBooleanIntermediate();

    auto result = booleanOr(a, b);

    interpreter->setBooleanResult(result);
}

void op_boolean_equal(VirtualMachine* interpreter) {
    auto a = interpreter->getBooleanResult();
    auto b = interpreter->getBooleanIntermediate();

    auto result = booleanEqualTo(a, b);

    interpreter->setBooleanResult(result);
}

void op_boolean_not_equal(VirtualMachine* interpreter) {
    auto a = interpreter->getBooleanResult();
    auto b = interpreter->getBooleanIntermediate();

    auto result = booleanNotEqualTo(a, b);

    interpreter->setBooleanResult(result);
}

void op_integer_load_zero_r(VirtualMachine* interpreter) {
    interpreter->setIntegerResult(0);
}

void op_integer_load_zero_i(VirtualMachine* interpreter) {
    interpreter->setIntegerIntermediate(0);
}

void op_integer_load_one_r(VirtualMachine* interpreter) {
    interpreter->setIntegerResult(1);
}

void op_integer_load_one_i(VirtualMachine* interpreter) {
    interpreter->setIntegerIntermediate(1);
}

void op_integer_load_r(VirtualMachine* interpreter) {
    const auto value = interpreter->nextInteger();

    interpreter->setIntegerResult(value);
}

void op_integer_load_i(VirtualMachine* interpreter) {
    const auto value = interpreter->nextInteger();

    interpreter->setIntegerIntermediate(value);
}

void op_integer_push_zero(VirtualMachine* interpreter) {
    interpreter->pushInteger(0);
}

void op_integer_push_one(VirtualMachine* interpreter) {
    interpreter->pushInteger(1);
}

void op_integer_push_n(VirtualMachine* interpreter) {
    const auto value = interpreter->nextInteger();

    interpreter->pushInteger(value);
}

void op_integer_absolute(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();

    auto result = integerAbsolute(a);

    interpreter->setIntegerResult(result);
}

void op_integer_negate(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();

    auto result = integerNegate(a);

    interpreter->setIntegerResult(result);
}

void op_integer_add(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();
    auto b = interpreter->getIntegerIntermediate();

    auto result = integerAdd(a, b);

    interpreter->setIntegerResult(result);
}

void op_integer_subtract(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();
    auto b = interpreter->getIntegerIntermediate();

    auto result = integerSubtract(a, b);

    interpreter->setIntegerResult(result);
}

void op_integer_multiply(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();
    auto b = interpreter->getIntegerIntermediate();

    auto result = integerMultiply(a, b);

    interpreter->setIntegerResult(result);
}

void op_integer_divide(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();
    auto b = interpreter->getIntegerIntermediate();

    auto result = integerDivide(a, b);

    interpreter->setIntegerResult(result);
}

void op_integer_modulo(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();
    auto b = interpreter->getIntegerIntermediate();

    auto result = integerModulo(a, b);

    interpreter->setIntegerResult(result);
}

void op_integer_and(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();
    auto b = interpreter->getIntegerIntermediate();

    auto result = integerAnd(a, b);

    interpreter->setIntegerResult(result);
}

void op_integer_or(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();
    auto b = interpreter->getIntegerIntermediate();

    auto result = integerOr(a, b);

    interpreter->setIntegerResult(result);
}

void op_integer_xor(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();
    auto b = interpreter->getIntegerIntermediate();

    auto result = integerXor(a, b);

    interpreter->setIntegerResult(result);
}

void op_integer_equal(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();
    auto b = interpreter->getIntegerIntermediate();

    auto result = integerEqualTo(a, b);

    interpreter->setBooleanResult(result);
}

void op_integer_not_equal(VirtualMachine* interpreter) {
    auto a = interpreter->getIntegerResult();
    auto b = interpreter->getIntegerIntermediate();

    auto result = integerNotEqualTo(a, b);

    interpreter->setBooleanResult(result);
}

void op_integer_less_than(VirtualMachine* interpreter) {
    const auto a = interpreter->getIntegerResult();
    const auto b = interpreter->getIntegerIntermediate();

    const auto result = integerLessThan(a, b);

    interpreter->setBooleanResult(result);
}

void op_integer_less_than_or_equal(VirtualMachine* interpreter) {
    const auto a = interpreter->getIntegerResult();
    const auto b = interpreter->getIntegerIntermediate();

    const auto result = integerLessThanOrEqual(a, b);

    interpreter->setBooleanResult(result);
}

void op_integer_greater_than(VirtualMachine* interpreter) {
    const auto a = interpreter->getIntegerResult();
    const auto b = interpreter->getIntegerIntermediate();

    const auto result = integerGreaterThan(a, b);

    interpreter->setBooleanResult(result);
}

void op_integer_greater_than_or_equal(VirtualMachine* interpreter) {
    const auto a = interpreter->getIntegerResult();
    const auto b = interpreter->getIntegerIntermediate();

    const auto result = integerGreaterThanOrEqual(a, b);

    interpreter->setBooleanResult(result);
}

void op_float_load_zero_r(VirtualMachine* interpreter) {
    interpreter->setFloatResult(0.0);
}

void op_float_load_zero_i(VirtualMachine* interpreter) {
    interpreter->setFloatIntermediate(0.0);
}

void op_float_load_one_r(VirtualMachine* interpreter) {
    interpreter->setFloatResult(1.0);
}

void op_float_load_one_i(VirtualMachine* interpreter) {
    interpreter->setFloatIntermediate(1.0);
}

void op_float_load_r(VirtualMachine* interpreter) {
    const auto value = interpreter->nextFloat();

    interpreter->setFloatResult(value);
}

void op_float_load_i(VirtualMachine* interpreter) {
    const auto value = interpreter->nextFloat();

    interpreter->setFloatIntermediate(value);
}

void op_float_push_zero(VirtualMachine* interpreter) {
    interpreter->pushFloat(0.0);
}

void op_float_push_one(VirtualMachine* interpreter) {
    interpreter->pushFloat(1.0);
}

void op_float_push_n(VirtualMachine* interpreter) {
    const auto value = interpreter->nextFloat();

    interpreter->pushFloat(value);
}

void op_float_absolute(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();

    const auto result = floatAbsolute(a);

    interpreter->setFloatResult(result);
}

void op_float_negate(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();

    const auto result = floatNegate(a);

    interpreter->setFloatResult(result);
}

void op_float_add(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();
    const auto b = interpreter->getFloatIntermediate();

    const auto result = floatAdd(a, b);

    interpreter->setFloatResult(result);
}

void op_float_subtract(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();
    const auto b = interpreter->getFloatIntermediate();

    const auto result = floatSubtract(a, b);

    interpreter->setFloatResult(result);
}

void op_float_multiply(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();
    const auto b = interpreter->getFloatIntermediate();

    const auto result = floatMultiply(a, b);

    interpreter->setFloatResult(result);
}

void op_float_divide(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();
    const auto b = interpreter->getFloatIntermediate();

    const auto result = floatDivide(a, b);

    interpreter->setFloatResult(result);
}

void op_float_modulo(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();
    const auto b = interpreter->getFloatIntermediate();

    const auto result = floatModulo(a, b);

    interpreter->setFloatResult(result);
}

void op_float_sin(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();

    const auto result = floatSin(a);

    interpreter->setFloatResult(result);
}

void op_float_cosine(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();

    const auto result = floatCosine(a);

    interpreter->setFloatResult(result);
}

void op_float_tangent(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();

    const auto result = floatTangent(a);

    interpreter->setFloatResult(result);
}

void op_float_equal(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();
    const auto b = interpreter->getFloatIntermediate();

    const auto result = floatEqualTo(a, b);

    interpreter->setBooleanResult(result);
}

void op_float_not_equal(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();
    const auto b = interpreter->getFloatIntermediate();

    const auto result = floatNotEqualTo(a, b);

    interpreter->setBooleanResult(result);
}

void op_float_less_than(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();
    const auto b = interpreter->getFloatIntermediate();

    const auto result = floatLessThan(a, b);

    interpreter->setBooleanResult(result);
}

void op_float_less_than_or_equal(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();
    const auto b = interpreter->getFloatIntermediate();

    const auto result = floatLessThanOrEqual(a, b);

    interpreter->setBooleanResult(result);
}

void op_float_greater_than(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();
    const auto b = interpreter->getFloatIntermediate();

    const auto result = floatGreaterThan(a, b);

    interpreter->setBooleanResult(result);
}

void op_float_greater_than_or_equal(VirtualMachine* interpreter) {
    const auto a = interpreter->getFloatResult();
    const auto b = interpreter->getFloatIntermediate();

    const auto result = floatGreaterThanOrEqual(a, b);

    interpreter->setBooleanResult(result);
}

void op_string_init_r(VirtualMachine* interpreter) {
    interpreter->initStringResult();
}

void op_string_init_i(VirtualMachine* interpreter) {
    interpreter->initStringIntermediate();
}

void op_string_free_r(VirtualMachine* interpreter) {
    interpreter->freeStringResult();
}

void op_string_free_i(VirtualMachine* interpreter) {
    interpreter->freeStringIntermediate();
}

void op_string_free_variable(VirtualMachine* interpreter) {
    auto string_index = interpreter->nextIndex();

    interpreter->freeStringVariable(string_index);
}

void op_string_free_stack(VirtualMachine* interpreter) {
    const auto index = interpreter->nextIndex();

    interpreter->freeStringStack(index);
}
void op_string_push_r(VirtualMachine* interpreter) {
    interpreter->pushStringResult();
}

void op_string_push_i(VirtualMachine* interpreter) {
    interpreter->pushStringIntermediate();
}

void op_string_push_n(VirtualMachine* interpreter) {
    const auto stringId = interpreter->nextId();

    const auto& string = interpreter->getRuntime()->getString(stringId);

    interpreter->pushString(string);
}

void op_string_pop_r(VirtualMachine* interpreter) {
    interpreter->popStringResult();
}

void op_string_pop_i(VirtualMachine* interpreter) {
    interpreter->popStringIntermediate();
}

void op_string_load_r(VirtualMachine* interpreter) {
    const auto string_id = interpreter->nextId();

    auto& string = interpreter->getRuntime()->getString(string_id);

    interpreter->setStringResult(string);
}

void op_string_load_i(VirtualMachine* interpreter) {
    auto stringId = interpreter->nextId();

    auto string = interpreter->getRuntime()->getString(stringId);

    interpreter->setStringIntermediate(string);
}

// String comparisons borrow both operands: the compiler loads `$stringref` pointers into `r` and `i` (or frees owned
// temporaries itself), so nothing is released here.
void op_string_equal(VirtualMachine* interpreter) {
    const auto result = stringEqualTo(interpreter->getResult().getStringValue(), interpreter->getIntermediate().getStringValue());

    interpreter->setBooleanResult(result);
}

void op_string_not_equal(VirtualMachine* interpreter) {
    const auto result = !stringEqualTo(interpreter->getResult().getStringValue(), interpreter->getIntermediate().getStringValue());

    interpreter->setBooleanResult(result);
}

void op_stringref_load_r(VirtualMachine* interpreter) {
    const auto stringId = interpreter->nextId();

    interpreter->setStringRefResult(interpreter->getRuntime()->getString(stringId).c_str());
}

void op_stringref_load_i(VirtualMachine* interpreter) {
    const auto stringId = interpreter->nextId();

    interpreter->setStringRefIntermediate(interpreter->getRuntime()->getString(stringId).c_str());
}

void op_stringref_push(VirtualMachine* interpreter) {
    const auto stringId = interpreter->nextId();

    RuntimeValue reference;
    reference.setStringRefValue(interpreter->getRuntime()->getString(stringId).c_str());

    interpreter->pushValue(reference);
}

void op_stringref_peek_r(VirtualMachine* interpreter) {
    const auto offset = interpreter->nextIndex();

    interpreter->setResult(interpreter->peekStack(offset));
}

void op_stringref_peek_i(VirtualMachine* interpreter) {
    const auto offset = interpreter->nextIndex();

    interpreter->setIntermediate(interpreter->peekStack(offset));
}

void op_format_string_execute(VirtualMachine* interpreter) {
    const auto formatStringId = interpreter->nextId();

    const auto formatString = interpreter->getRuntime()->getFormatString(formatStringId);

    const auto string = formatString->format(interpreter);

    interpreter->setStringResult(string);
}

void op_id_push_r(VirtualMachine* interpreter) {
    interpreter->pushResult();
}

void op_id_push_v(VirtualMachine* interpreter) {
    const auto id = interpreter->nextId();

    interpreter->pushId(id);
}

void op_id_load_r(VirtualMachine* interpreter) {
    const auto id = interpreter->nextId();

    interpreter->setIdResult(id);
}

void op_id_load_i(VirtualMachine* interpreter) {
    const auto id = interpreter->nextId();

    interpreter->setIdIntermediate(id);
}

void op_assert(VirtualMachine* interpreter) {
    const auto assertResult = interpreter->getBooleanResult();
    const auto assertMessageId = interpreter->getIdIntermediate();

    // Bitwise comparison
    if (!assertResult) {
        auto& assertionMessage = interpreter->getRuntime()->getString(assertMessageId);

        log::error(assertionMessage);

        interpreter->fault(assertionMessage);
    }
}

} // namespace nomad
