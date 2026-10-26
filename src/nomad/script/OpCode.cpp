// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/OpCode.hpp>

#include <nomad/system/Boolean.hpp>
#include <nomad/system/Float.hpp>
#include <nomad/system/Integer.hpp>
#include <nomad/script/FormatString.hpp>
#include <nomad/script/InterpreterContext.hpp>
#include <nomad/script/NomadStringObject.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <cassert>

namespace nomad {

namespace {

void callNativeIndexer(VirtualMachine* machine, const bool write) {
    const auto indexerId = IndexerId{machine->nextId()};
    const auto info = machine->getRuntime()->getNativeIndexerInfo(indexerId);
    if (!info) {
        machine->fault("Invalid native indexer");
    }

    const auto argumentCount = info->indexTypes.size() + (write ? 1 : 0);
    const auto receiver = machine->peekStack(argumentCount);
    const auto releaseArguments = [&]() {
        for (const auto* type : info->indexTypes) {
            auto value = machine->popValue();
            type->freeValue(value);
        }
        if (write) {
            auto value = machine->popValue();
            info->valueType->freeValue(value);
        }
        auto savedReceiver = machine->popValue();
        info->objectType->freeValue(savedReceiver);
    };

    try {
        machine->callNativeIndexer(indexerId, receiver, write);
    } catch (...) {
        releaseArguments();
        if (!write) {
            info->valueType->freeValue(machine->getResult());
        }
        throw;
    }
    releaseArguments();
}

} // namespace

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

void op_copy_i_to_r(VirtualMachine* interpreter) {
    interpreter->setResult(interpreter->getIntermediate());
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

void op_object_copy_r_to_i(VirtualMachine* interpreter) {
    interpreter->getIntermediate().copyObjectValue(interpreter->getResult());
}

void op_object_move_r_to_i(VirtualMachine* interpreter) {
    interpreter->moveObjectResultToIntermediate();
}

void op_object_move_i_to_r(VirtualMachine* interpreter) {
    interpreter->pushObjectIntermediate();
    interpreter->popObjectResult();
}

void op_object_move_r_to_stack(VirtualMachine* interpreter) {
    interpreter->moveObjectResultToStack();
}

void op_call_function(VirtualMachine* interpreter) {
    interpreter->callFunction(interpreter->nextIndex());
}

void op_call_method(VirtualMachine* interpreter) {
    interpreter->callMethod(interpreter->nextIndex());
}

void op_function_enter(VirtualMachine* interpreter) {
    interpreter->enterFunction(CallableId{interpreter->nextId()});
}

void op_push_this(VirtualMachine* interpreter) {
    interpreter->pushThis();
}

void op_pop_this(VirtualMachine* interpreter) {
    interpreter->popThis();
}

void op_move_this_to_r(VirtualMachine* interpreter) {
    interpreter->moveThisToResult();
}

void op_call_native_function(VirtualMachine* interpreter) {
    const auto nativeFunctionId = CallableId{interpreter->nextId()};

    try {
        interpreter->callNativeFunction(nativeFunctionId);
    } catch (...) {
        if (const auto* constructor = interpreter->getRuntime()->getNativeConstructor(nativeFunctionId)) {
            const auto& parameters = constructor->getParameters();
            for (auto index = NomadIndex{0}; index < parameters.size(); ++index) {
                auto argument = interpreter->peekStack(index);
                parameters[index].type->freeValue(argument);
            }
        }
        throw;
    }
}

void op_native_property_get(VirtualMachine* interpreter) {
    const auto memberId = PropertyId{interpreter->nextId()};
    RuntimeValue result;
    interpreter->getProperty(memberId, interpreter->getIntermediate(), result);
    interpreter->setResult(result);
}

void op_native_property_set(VirtualMachine* interpreter) {
    const auto memberId = PropertyId{interpreter->nextId()};
    interpreter->setProperty(memberId, interpreter->getIntermediate(), interpreter->getResult());
}

void op_native_index_get(VirtualMachine* interpreter) {
    callNativeIndexer(interpreter, false);
}

void op_native_index_set(VirtualMachine* interpreter) {
    callNativeIndexer(interpreter, true);
}

void op_native_method_call(VirtualMachine* interpreter) {
    const auto memberId = CallableId{interpreter->nextId()};
    const auto receiverOffset = interpreter->nextIndex();
    interpreter->setIntermediate(interpreter->peekStack(receiverOffset));
    interpreter->callNativeMethod(memberId, interpreter->getIntermediate());
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

void op_object_parameter_load_r(VirtualMachine* interpreter) {
    const auto parameterId = interpreter->nextId();

    interpreter->getResult().copyObjectValue(interpreter->getParameter(parameterId));
}

void op_object_parameter_load_i(VirtualMachine* interpreter) {
    const auto parameterId = interpreter->nextId();

    interpreter->getIntermediate().copyObjectValue(interpreter->getParameter(parameterId));
}

void op_object_parameter_push(VirtualMachine* interpreter) {
    const auto parameterId = interpreter->nextId();

    interpreter->pushObjectValue(interpreter->getParameter(parameterId));
}

void op_function_variable_set(VirtualMachine* interpreter) {
    const auto variableId = VariableId{interpreter->nextId()};

    interpreter->setFunctionVariableValue(variableId, interpreter->getResult());
}


void op_function_variable_get_r(VirtualMachine* interpreter) {
    const auto variableId = VariableId{interpreter->nextId()};

    RuntimeValue value;
    interpreter->getFunctionVariableValue(variableId, value);

    interpreter->setResult(value);
}

void op_function_variable_object_get_r(VirtualMachine* interpreter) {
    const auto variableId = VariableId{interpreter->nextId()};

    RuntimeValue value;
    interpreter->getFunctionVariableValue(variableId, value);

    interpreter->getResult().copyObjectValue(value);
}

void op_function_variable_get_i(VirtualMachine* interpreter) {
    const auto variableId = VariableId{interpreter->nextId()};

    RuntimeValue value;
    interpreter->getFunctionVariableValue(variableId, value);

    interpreter->setIntermediate(value);
}

void op_function_variable_object_get_i(VirtualMachine* interpreter) {
    const auto variableId = VariableId{interpreter->nextId()};

    RuntimeValue value;
    interpreter->getFunctionVariableValue(variableId, value);

    interpreter->getIntermediate().copyObjectValue(value);
}

void op_function_variable_push(VirtualMachine* interpreter) {
    const auto variableId = VariableId{interpreter->nextId()};

    RuntimeValue value;
    interpreter->getFunctionVariableValue(variableId, value);

    interpreter->pushValue(value);
}

void op_function_variable_object_push(VirtualMachine* interpreter) {
    const auto variableId = VariableId{interpreter->nextId()};

    RuntimeValue value;
    interpreter->getFunctionVariableValue(variableId, value);

    interpreter->pushObjectValue(value);
}

void op_constant_get_r(VirtualMachine* machine) {
    const auto id = ConstantId{machine->nextId()};
    RuntimeValue value;
    machine->getRuntime()->getConstantValue(id, value);
    RuntimeValue copy;
    machine->getRuntime()->getConstantType(id)->copyValue(value, copy);
    machine->setResult(copy);
}

void op_constant_get_i(VirtualMachine* machine) {
    const auto id = ConstantId{machine->nextId()};
    RuntimeValue value;
    machine->getRuntime()->getConstantValue(id, value);
    RuntimeValue copy;
    machine->getRuntime()->getConstantType(id)->copyValue(value, copy);
    machine->setIntermediate(copy);
}

void op_constant_push(VirtualMachine* machine) {
    const auto id = ConstantId{machine->nextId()};
    RuntimeValue value;
    machine->getRuntime()->getConstantValue(id, value);
    RuntimeValue copy;
    machine->getRuntime()->getConstantType(id)->copyValue(value, copy);
    machine->pushValue(copy);
}

void op_dynamic_variable_set(VirtualMachine* interpreter) {
    const auto variableId = DynamicVariableId{interpreter->nextId()};

    interpreter->getRuntime()->setDynamicVariable(interpreter, variableId, interpreter->getResult());
}

void op_dynamic_variable_get_r(VirtualMachine* interpreter) {
    const auto variableId = DynamicVariableId{interpreter->nextId()};

    RuntimeValue value;

    interpreter->getRuntime()->getDynamicVariableValue(interpreter, variableId, value);

    interpreter->setResult(value);
}

void op_dynamic_variable_get_i(VirtualMachine* interpreter) {
    const auto variableId = DynamicVariableId{interpreter->nextId()};

    RuntimeValue value;

    interpreter->getRuntime()->getDynamicVariableValue(interpreter, variableId, value);

    interpreter->setIntermediate(value);
}

void op_dynamic_variable_push(VirtualMachine* interpreter) {
    const auto variableId = DynamicVariableId{interpreter->nextId()};

    RuntimeValue value;

    interpreter->getRuntime()->getDynamicVariableValue(interpreter, variableId, value);

    interpreter->pushValue(value);
}

void op_context_variable_set(VirtualMachine* interpreter) {
    const auto variableId = ContextVariableId{interpreter->nextId()};

    interpreter->getRuntime()->setContextVariableValue(variableId, interpreter->getResult());
}

void op_context_variable_get_r(VirtualMachine* interpreter) {
    const auto variableId = ContextVariableId{interpreter->nextId()};

    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->setResult(value);
}

void op_context_variable_object_get_r(VirtualMachine* interpreter) {
    const auto variableId = ContextVariableId{interpreter->nextId()};

    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->getResult().copyObjectValue(value);
}

void op_context_variable_get_i(VirtualMachine* interpreter) {
    const auto variableId = ContextVariableId{interpreter->nextId()};

    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->setIntermediate(value);
}

void op_context_variable_object_get_i(VirtualMachine* interpreter) {
    const auto variableId = ContextVariableId{interpreter->nextId()};

    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->getIntermediate().copyObjectValue(value);
}

void op_context_variable_push(VirtualMachine* interpreter) {
    const auto variableId = ContextVariableId{interpreter->nextId()};

    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->pushValue(value);
}

void op_context_variable_object_push(VirtualMachine* interpreter) {
    const auto variableId = ContextVariableId{interpreter->nextId()};

    RuntimeValue value;

    interpreter->getRuntime()->getContextVariableValue(variableId, value);

    interpreter->pushObjectValue(value);
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

void op_object_init_r(VirtualMachine* interpreter) {
    interpreter->getResult().setObjectValue(nullptr);
}

void op_object_new(VirtualMachine* interpreter) {
    interpreter->createScriptObject(TypeId{interpreter->nextId()});
}

void op_object_init_i(VirtualMachine* interpreter) {
    interpreter->getIntermediate().setObjectValue(nullptr);
}

void op_string_init_i(VirtualMachine* interpreter) {
    interpreter->initStringIntermediate();
}

void op_object_free_r(VirtualMachine* interpreter) {
    interpreter->freeObjectResult();
}

void op_object_free_i(VirtualMachine* interpreter) {
    interpreter->freeObjectIntermediate();
}

void op_object_free_variable(VirtualMachine* interpreter) {
    const auto variableId = VariableId{toNomadId(interpreter->nextIndex())};

    interpreter->freeObjectVariable(variableId);
}

void op_object_free_stack(VirtualMachine* interpreter) {
    const auto index = interpreter->nextIndex();

    interpreter->freeObjectStack(index);
}

void op_object_push_r(VirtualMachine* interpreter) {
    interpreter->pushObjectResult();
}

void op_object_push_i(VirtualMachine* interpreter) {
    interpreter->pushObjectIntermediate();
}

void op_string_push_n(VirtualMachine* interpreter) {
    const auto stringId = StringId{interpreter->nextId()};

    auto* string = interpreter->getRuntime()->getString(stringId);
    string->addRef();

    interpreter->pushString(string);
}

void op_object_pop_r(VirtualMachine* interpreter) {
    interpreter->popObjectResult();
}

void op_object_pop_i(VirtualMachine* interpreter) {
    interpreter->popObjectIntermediate();
}

void op_string_load_r(VirtualMachine* interpreter) {
    const auto stringId = StringId{interpreter->nextId()};

    auto* string = interpreter->getRuntime()->getString(stringId);
    string->addRef();

    interpreter->setStringResult(string);
}

void op_string_load_i(VirtualMachine* interpreter) {
    const auto stringId = StringId{interpreter->nextId()};

    auto* string = interpreter->getRuntime()->getString(stringId);
    string->addRef();

    interpreter->setStringIntermediate(string);
}

// String comparisons release both operands.
void op_string_equal(VirtualMachine* interpreter) {
    const auto result = interpreter->getStringResult()->getView() == interpreter->getStringIntermediate()->getView();

    interpreter->freeObjectResult();
    interpreter->freeObjectIntermediate();
    interpreter->setBooleanResult(result);
}

void op_string_not_equal(VirtualMachine* interpreter) {
    const auto result = interpreter->getStringResult()->getView() != interpreter->getStringIntermediate()->getView();

    interpreter->freeObjectResult();
    interpreter->freeObjectIntermediate();
    interpreter->setBooleanResult(result);
}

void op_format_string_execute(VirtualMachine* interpreter) {
    const auto formatStringId = FormatStringId{interpreter->nextId()};

    const auto formatString = interpreter->getRuntime()->getFormatString(formatStringId);

    const auto string = formatString->format(interpreter);

    interpreter->setStringResult(string);
}

void op_dynamic_format_execute(VirtualMachine* machine) {
    const auto line = machine->nextIndex();
    const auto column = machine->nextIndex();
    auto* function = machine->getCurrentFunction();

    if (function == nullptr) {
        machine->fault("Dynamic formatting requires an active script function");
    }

    auto* formatText = machine->getStringResult();
    machine->setResult(RuntimeValue{});
    InterpreterContext context(*machine, *function);

    const auto result = formatDynamicString(formatText->getText(), context, function->getPath(), line, column);

    formatText->releaseRef();

    machine->setStringResult(result.text);
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
    const auto assertMessageId = StringId{interpreter->getIdIntermediate()};

    // Bitwise comparison
    if (!assertResult) {
        const auto* assertionMessage = interpreter->getRuntime()->getString(assertMessageId);

        log::error(assertionMessage->getString());

        interpreter->fault(assertionMessage->getString());
    }
}

} // namespace nomad
