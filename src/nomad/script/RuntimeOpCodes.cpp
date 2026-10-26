// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Runtime.hpp>

namespace nomad {

void Runtime::registerDefaultInstructions() {
    // OpCode definitions
    registerInstruction(OpCodes::op_nop, op_nop, "No operation", {});
    registerInstruction(OpCodes::op_stop, op_stop, "Stop the execution of the interpreter", {});
    registerInstruction(OpCodes::op_yield, op_yield, "Yield the execution of the interpreter", {});
    registerInstruction(OpCodes::op_return, op_return, "Return from the current function. Does not free variables from the stack", {});
    registerInstruction(
        OpCodes::op_return_n, op_return_n, "Return from the current function. Frees `n` entry from the stack",
        {Operand::Index}
    );

    // Flow control
    registerInstruction(OpCodes::op_jump, op_jump, "Jump to the given index (address)", {Operand::Index});
    registerInstruction(
        OpCodes::op_jump_if, op_jump_if, "Jump to the given index (address) if the `r` register is true",
        {Operand::Index}
    );
    registerInstruction(
        OpCodes::op_jump_if_false, op_jump_if_false, "Jump to the given index (address) if the `r` register is false",
        {Operand::Index}
    );

    // Register push and pop
    registerInstruction(OpCodes::op_push_r, op_push_r, "Push the `r` register value to the stack", {});
    registerInstruction(OpCodes::op_push_i, op_push_i, "Push the `i` register value to the stack", {});
    registerInstruction(OpCodes::op_pop_r, op_pop_r, "Pop the stack value to the `r` register", {});
    registerInstruction(OpCodes::op_pop_i, op_pop_i, "Pop the stack value to the `i` register", {});
    registerInstruction(OpCodes::op_pop_1, op_pop_1, "Pop 1 value from the stack", {});
    registerInstruction(OpCodes::op_pop_2, op_pop_2, "Pop 2 values from the stack", {});
    registerInstruction(OpCodes::op_pop_3, op_pop_3, "Pop 3 values from the stack", {});
    registerInstruction(OpCodes::op_pop_n, op_pop_n, "Pop `n` values from the stack", {Operand::Index});
    registerInstruction(OpCodes::op_copy_r_to_i, op_copy_r_to_i, "Copy the value of the `result` register into the `intermediate` register", {});
    registerInstruction(OpCodes::op_copy_i_to_r, op_copy_i_to_r, "Copy the value of the `intermediate` register into the `result` register", {});
    registerInstruction(OpCodes::op_object_copy_r_to_i, op_object_copy_r_to_i, "Copy the object in the `result` register into the `intermediate` register", {});
    registerInstruction(OpCodes::op_object_move_r_to_i, op_object_move_r_to_i, "Move the object in the `result` register into the `intermediate` register", {});
    registerInstruction(OpCodes::op_object_move_i_to_r, op_object_move_i_to_r, "Move the object in the `intermediate` register into the `result` register", {});
    registerInstruction(OpCodes::op_object_move_r_to_stack, op_object_move_r_to_stack, "Move the object in the `result` register onto the stack", {});

    // Function and nativeFunction calls
    registerInstruction(
        OpCodes::op_call_function, op_call_function,
        "Allocate `n` variables on the stack and call the function at the given index (address)",
        {Operand::Function}
    );
    registerInstruction(OpCodes::op_call_native_function, op_call_native_function, "Call the given native function id", {Operand::NativeFunction});
    registerInstruction(OpCodes::op_call_method, op_call_method, "Call a script method id with the current receiver", {Operand::Id});
    registerInstruction(OpCodes::op_native_property_get, op_native_property_get, "Read a native or stored property", {Operand::Id});
    registerInstruction(OpCodes::op_native_property_set, op_native_property_set, "Write a native or stored property", {Operand::Id});
    registerInstruction(OpCodes::op_native_index_get, op_native_index_get, "Read an indexer", {Operand::Id});
    registerInstruction(OpCodes::op_native_index_set, op_native_index_set, "Write an indexer", {Operand::Id});
    registerInstruction(OpCodes::op_native_method_call, op_native_method_call, "Call an native method with a saved stack receiver", {Operand::Id, Operand::Index});
    registerInstruction(OpCodes::op_parameter_load_r, op_parameter_load_r, "Load the `n`th parameter into result", {Operand::Id});
    registerInstruction(OpCodes::op_parameter_load_i, op_parameter_load_i, "Load the `n`th parameter into intermediate", {Operand::Id});
    registerInstruction(OpCodes::op_parameter_push, op_parameter_push, "Push the `n`th parameter onto the stack", {Operand::Id});
    registerInstruction(OpCodes::op_object_parameter_load_r, op_object_parameter_load_r, "Load the `n`th object parameter into result", {Operand::Id});
    registerInstruction(OpCodes::op_object_parameter_load_i, op_object_parameter_load_i, "Load the `n`th object parameter into intermediate", {Operand::Id});
    registerInstruction(OpCodes::op_object_parameter_push, op_object_parameter_push, "Push the `n`th object parameter onto the stack", {Operand::Id});

    // Variable operations
    registerInstruction(
        OpCodes::op_function_variable_set, op_function_variable_set,
        "Set the `id` function variable from the `r` register value", {Operand::FunctionVariable}
    );
    registerInstruction(OpCodes::op_function_variable_get_r, op_function_variable_get_r, "Load the `id` function variable value to the `result` register", {Operand::FunctionVariable});
    registerInstruction(OpCodes::op_function_variable_object_get_r, op_function_variable_object_get_r, "Load the `id` object function variable value to the `result` register", {Operand::FunctionVariable});
    registerInstruction(OpCodes::op_function_variable_get_i, op_function_variable_get_i, "Load the `id` function variable value to the `intermediate` register", {Operand::FunctionVariable});
    registerInstruction(OpCodes::op_function_variable_object_get_i, op_function_variable_object_get_i, "Load the `id` object function variable value to the `intermediate` register", {Operand::FunctionVariable});
    registerInstruction(OpCodes::op_function_variable_push, op_function_variable_push, "Push the `id` function variable value to the stack", {Operand::FunctionVariable});
    registerInstruction(OpCodes::op_function_variable_object_push, op_function_variable_object_push, "Push the `id` object function variable to the stack", {Operand::FunctionVariable});
    registerInstruction(OpCodes::op_constant_get_r, op_constant_get_r, "Load the constant into the result register", {Operand::Id});
    registerInstruction(OpCodes::op_constant_get_i, op_constant_get_i, "Load the constant into the intermediate register", {Operand::Id});
    registerInstruction(OpCodes::op_constant_push, op_constant_push, "Push the constant onto the stack", {Operand::Id});
    registerInstruction(OpCodes::op_dynamic_variable_set, op_dynamic_variable_set, "Set the dynamic variable `id` from the `result` register value", {Operand::DynamicVariable});
    registerInstruction(OpCodes::op_dynamic_variable_get_r, op_dynamic_variable_get_r, "Load the dynamic variable `id` value to the `r` register", {Operand::DynamicVariable});
    registerInstruction(OpCodes::op_dynamic_variable_get_i, op_dynamic_variable_get_i, "Load the dynamic variable `id` value to the `intermediate` register", {Operand::DynamicVariable});
    registerInstruction(OpCodes::op_dynamic_variable_push, op_dynamic_variable_push, "Push the dynamic variable `id` value to the stackr", {Operand::DynamicVariable});
    registerInstruction(OpCodes::op_context_variable_set, op_context_variable_set, "Set the context variable `id` from the `result` register value", {Operand::ContextVariableId});
    registerInstruction(OpCodes::op_context_variable_get_r, op_context_variable_get_r, "Load the context variable `id` value to the `result` register", {Operand::ContextVariableId});
    registerInstruction(OpCodes::op_context_variable_object_get_r, op_context_variable_object_get_r, "Load the object context variable `id` value to the `result` register", {Operand::ContextVariableId});
    registerInstruction(OpCodes::op_context_variable_get_i, op_context_variable_get_i, "Load the context variable `id` value to the `intermediate` register", {Operand::ContextVariableId});
    registerInstruction(OpCodes::op_context_variable_object_get_i, op_context_variable_object_get_i, "Load the object context variable `id` value to the `intermediate` register", {Operand::ContextVariableId});
    registerInstruction(OpCodes::op_context_variable_push, op_context_variable_push, "Push the context variable `id` value to the stack", {Operand::ContextVariableId});
    registerInstruction(OpCodes::op_context_variable_object_push, op_context_variable_object_push, "Push the object context variable `id` value to the stack", {Operand::ContextVariableId});

    // Boolean operators
    registerInstruction(
        OpCodes::op_boolean_load_false_r, op_boolean_load_false_r, "Load the boolean false value to the `r` register",
        {}
    );
    registerInstruction(
        OpCodes::op_boolean_load_false_i, op_boolean_load_false_i, "Load the boolean false value to the `i` register",
        {}
    );
    registerInstruction(
        OpCodes::op_boolean_load_true_r, op_boolean_load_true_r, "Load the boolean true value to the `r` register", {}
    );
    registerInstruction(
        OpCodes::op_boolean_load_true_i, op_boolean_load_true_i, "Load the boolean true value to the `i` register", {}
    );
    registerInstruction(OpCodes::op_boolean_push_false, op_boolean_push_false, "Push boolean false value to the stack", {});
    registerInstruction(OpCodes::op_boolean_push_true, op_boolean_push_true, "Push boolean true value to the stack", {});
    registerInstruction(OpCodes::op_boolean_not, op_boolean_not, "Negate the `r` register boolean value", {});
    registerInstruction(
        OpCodes::op_boolean_and, op_boolean_and, "Logical AND the `r` and `i` register boolean values", {}
    );
    registerInstruction(
        OpCodes::op_boolean_or, op_boolean_or, "Logical OR the `r` and `i` register boolean values", {}
    );
    registerInstruction(
        OpCodes::op_boolean_equal, op_boolean_equal, "Test if the `r` and `i` register boolean values are equal", {}
    );
    registerInstruction(
        OpCodes::op_boolean_not_equal, op_boolean_not_equal,
        "Test if the `r` and `i` register boolean values are not equal", {}
    );


    // Integer operators
    registerInstruction(
        OpCodes::op_integer_load_zero_r, op_integer_load_zero_r, "Load the integer zero value to the `r` register", {}
    );
    registerInstruction(
        OpCodes::op_integer_load_zero_i, op_integer_load_zero_i, "Load the integer zero value to the `i` register", {}
    );
    registerInstruction(
        OpCodes::op_integer_load_one_r, op_integer_load_one_r, "Load the integer one value to the `r` register", {}
    );
    registerInstruction(
        OpCodes::op_integer_load_one_i, op_integer_load_one_i, "Load the integer one value to the `i` register", {}
    );
    registerInstruction(
        OpCodes::op_integer_load_r, op_integer_load_r, "Load the integer value to the `r` register",
        {Operand::Integer}
    );
    registerInstruction(
        OpCodes::op_integer_load_i, op_integer_load_i, "Load the integer value to the `i` register",
        {Operand::Integer}
    );
    registerInstruction(OpCodes::op_integer_push_zero, op_integer_push_zero, "Push integer zero value to the stack", {});
    registerInstruction(OpCodes::op_integer_push_one, op_integer_push_one, "Push integer one value to the stack", {});
    registerInstruction(OpCodes::op_integer_push_n, op_integer_push_n, "Push integer value to the stack", {Operand::Integer});
    registerInstruction(
        OpCodes::op_integer_absolute, op_integer_absolute, "Get the absolute value of the `r` register", {}
    );
    registerInstruction(OpCodes::op_integer_negate, op_integer_negate, "Negate the `r` register integer value", {});
    registerInstruction(OpCodes::op_integer_add, op_integer_add, "Add the `r` and `i` register integer values", {});
    registerInstruction(
        OpCodes::op_integer_subtract, op_integer_subtract, "Subtract the `r` and `i` register integer values", {}
    );
    registerInstruction(
        OpCodes::op_integer_multiply, op_integer_multiply, "Multiply the `r` and `i` register integer values", {}
    );
    registerInstruction(
        OpCodes::op_integer_divide, op_integer_divide, "Divide the `r` and `i` register integer values", {}
    );
    registerInstruction(
        OpCodes::op_integer_modulo, op_integer_modulo, "Modulo the `r` and `i` register integer values", {}
    );
    registerInstruction(
        OpCodes::op_integer_and, op_integer_and, "Bitwise AND the `r` and `i` register integer values", {}
    );
    registerInstruction(
        OpCodes::op_integer_or, op_integer_or, "Bitwise OR the `r` and `i` register integer values", {}
    );
    registerInstruction(
        OpCodes::op_integer_xor, op_integer_xor, "Bitwise XOR the `r` and `i` register integer values", {}
    );
    registerInstruction(
        OpCodes::op_integer_equal, op_integer_equal, "Test if the `r` and `i` register integer values are equal", {}
    );
    registerInstruction(
        OpCodes::op_integer_not_equal, op_integer_not_equal,
        "Test if the `r` and `i` register integer values are not equal", {}
    );
    registerInstruction(
        OpCodes::op_integer_less_than, op_integer_less_than,
        "Test if the `r` register integer value is less than the `i` register integer value", {}
    );
    registerInstruction(
        OpCodes::op_integer_less_than_or_equal, op_integer_less_than_or_equal,
        "Test if the `r` register integer value is less than or equal to the `i` register integer value", {}
    );
    registerInstruction(
        OpCodes::op_integer_greater_than, op_integer_greater_than,
        "Test if the `r` register integer value is greater than the `i` register integer value", {}
    );
    registerInstruction(
        OpCodes::op_integer_greater_than_or_equal, op_integer_greater_than_or_equal,
        "Test if the `r` register integer value is greater than or equal to the `i` register integer value", {}
    );


    // Float operators
    registerInstruction(
        OpCodes::op_float_load_zero_r, op_float_load_zero_r, "Load the float zero value to the `r` register", {}
    );
    registerInstruction(
        OpCodes::op_float_load_zero_i, op_float_load_zero_i, "Load the float zero value to the `i` register", {}
    );
    registerInstruction(
        OpCodes::op_float_load_one_r, op_float_load_one_r, "Load the float one value to the `r` register", {}
    );
    registerInstruction(
        OpCodes::op_float_load_one_i, op_float_load_one_i, "Load the float one value to the `i` register", {}
    );
    registerInstruction(
        OpCodes::op_float_load_r, op_float_load_r, "Load the float value to the `r` register", {Operand::Float}
    );
    registerInstruction(
        OpCodes::op_float_load_i, op_float_load_i, "Load the float value to the `i` register", {Operand::Float}
    );
    registerInstruction(OpCodes::op_float_push_zero, op_float_push_zero, "Push float zero value to the stack", {});
    registerInstruction(OpCodes::op_float_push_one, op_float_push_one, "Push float one value to the stack", {});
    registerInstruction(OpCodes::op_float_push_n, op_float_push_n, "Push the float value to the stack", {Operand::Float});
    registerInstruction(
        OpCodes::op_float_absolute, op_float_absolute, "Get the absolute value of the `r` register", {}
    );
    registerInstruction(OpCodes::op_float_negate, op_float_negate, "Negate the `r` register float value", {});
    registerInstruction(OpCodes::op_float_add, op_float_add, "Add the `r` and `i` register float values", {});
    registerInstruction(
        OpCodes::op_float_subtract, op_float_subtract, "Subtract the `r` and `i` register float values", {}
    );
    registerInstruction(
        OpCodes::op_float_multiply, op_float_multiply, "Multiply the `r` and `i` register float values", {}
    );
    registerInstruction(OpCodes::op_float_divide, op_float_divide, "Divide the `r` and `i` register float values", {});
    registerInstruction(OpCodes::op_float_modulo, op_float_modulo, "Modulo the `r` and `i` register float values", {});
    registerInstruction(OpCodes::op_float_sin, op_float_sin, "Get the sine value of the `r` register", {});
    registerInstruction(OpCodes::op_float_cosine, op_float_cosine, "Get the cosine value of the `r` register", {});
    registerInstruction(OpCodes::op_float_tangent, op_float_tangent, "Get the tangent value of the `r` register", {});
    registerInstruction(
        OpCodes::op_float_equal, op_float_equal, "Test if the `r` and `i` register float values are equal", {}
    );
    registerInstruction(
        OpCodes::op_float_not_equal, op_float_not_equal, "Test if the `r` and `i` register float values are not equal",
        {}
    );
    registerInstruction(
        OpCodes::op_float_less_than, op_float_less_than,
        "Test if the `r` register float value is less than the `i` register float value", {}
    );
    registerInstruction(
        OpCodes::op_float_less_than_or_equal, op_float_less_than_or_equal, "op_float_less_than_or_equal", {}
    );
    registerInstruction(
        OpCodes::op_float_greater_than, op_float_greater_than,
        "Test if the `r` register float value is greater than the `i` register float value", {}
    );
    registerInstruction(
        OpCodes::op_float_greater_than_or_equal, op_float_greater_than_or_equal,
        "Test if the `r` register float value is greater than or equal to the `i` register float value", {}
    );

    registerInstruction(OpCodes::op_object_init_r, op_object_init_r, "Clear the object result register", {});
    registerInstruction(OpCodes::op_object_new, op_object_new, "Invoke a registered object constructor", {Operand::Id});
    registerInstruction(OpCodes::op_object_return, op_object_return, "Complete construction, moving the instance into result and releasing the constructor frame", {});
    registerInstruction(OpCodes::op_object_instantiate, op_object_instantiate, "Allocate a script instance and bind this", {Operand::Id});
    registerInstruction(OpCodes::op_object_init_i, op_object_init_i, "Clear the object intermediate register", {});

    // String operators
    registerInstruction(
        OpCodes::op_string_init_r, op_string_init_r, "Init an empty string in the `r` register", {}
    );
    registerInstruction(
        OpCodes::op_string_init_i, op_string_init_i, "Init an empty string in the `i` register", {}
    );
    registerInstruction(OpCodes::op_string_push_n, op_string_push_n, "Push the string value to the stack", {Operand::String});
    registerInstruction(
        OpCodes::op_string_load_r, op_string_load_r, "Load the string value to the `r` register", {Operand::String}
    );
    registerInstruction(
        OpCodes::op_string_load_i, op_string_load_i, "Load the string value to the `i` register", {Operand::String}
    );
    registerInstruction(
        OpCodes::op_string_equal, op_string_equal,
        "Test if the `r` and `i` register string values are equal. Operands are not freed", {}
    );
    registerInstruction(
        OpCodes::op_string_not_equal, op_string_not_equal,
        "Test if the `r` and `i` register string values are not equal. Operands are not freed", {}
    );

    // Object operators
    registerInstruction(OpCodes::op_object_free_r, op_object_free_r, "Release the object in the `r` register", {});
    registerInstruction(OpCodes::op_object_free_i, op_object_free_i, "Release the object in the `i` register", {});
    registerInstruction(
        OpCodes::op_object_free_variable, op_object_free_variable, "Release the object function variable", {Operand::Index}
    );
    registerInstruction(
        OpCodes::op_object_free_stack, op_object_free_stack, "Release the object at stack offset (0 = top)", {Operand::Index}
    );
    registerInstruction(
        OpCodes::op_object_push_r, op_object_push_r, "Push the `r` register object to the stack", {}
    );
    registerInstruction(
        OpCodes::op_object_push_i, op_object_push_i, "Move the `i` register object to the stack. Pair with `op_object_pop_i`", {}
    );
    registerInstruction(OpCodes::op_object_pop_r, op_object_pop_r, "Pop the object to the `r` register", {});
    registerInstruction(OpCodes::op_object_pop_i, op_object_pop_i, "Pop the object to the `i` register", {});

    // Format string operators
    registerInstruction(
        OpCodes::op_format_string_execute, op_format_string_execute, "Execute the format string in the `r` register",
        {Operand::FormatString}
    );
    registerInstruction(OpCodes::op_dynamic_format_execute, op_dynamic_format_execute,
        "Interpret a string template in the current script frame", {Operand::Index, Operand::Index});

    // ID operators
    registerInstruction(OpCodes::op_id_push_r, op_id_push_r, "Push the result value to the stack", {});
    registerInstruction(OpCodes::op_id_push_v, op_id_push_v, "Push the `id` value to the stack", {Operand::Id});
    registerInstruction(OpCodes::op_id_load_r, op_id_load_r, "Load the `id` value to the `r` register", {});
    registerInstruction(OpCodes::op_id_load_i, op_id_load_i, "Load the `id` value to the `i` register", {});

    // Debug
    registerInstruction(
        OpCodes::op_assert, op_assert, "Assert the boolean value in the `r` register value is true", {}
    );
}

} // namespace nomad
