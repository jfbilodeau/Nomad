// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

namespace nomad {

class VirtualMachine;

using InstructionFn = void (*)(VirtualMachine*);

namespace OpCodes {
    // OpCode definition below
}

#define DECLARE_OP_CODE(name) \
    void name(VirtualMachine* interpreter); \
    namespace OpCodes { \
        const NomadString name = #name; \
    }

// OpCode definitions
DECLARE_OP_CODE(op_nop)
DECLARE_OP_CODE(op_stop)
DECLARE_OP_CODE(op_yield)
DECLARE_OP_CODE(op_return)
DECLARE_OP_CODE(op_return_n)

// Flow control
DECLARE_OP_CODE(op_jump)
DECLARE_OP_CODE(op_jump_if)
DECLARE_OP_CODE(op_jump_if_false)

// Register stack operations
DECLARE_OP_CODE(op_push_r)
DECLARE_OP_CODE(op_push_i)
DECLARE_OP_CODE(op_pop_r)
DECLARE_OP_CODE(op_pop_i)
DECLARE_OP_CODE(op_pop_1)
DECLARE_OP_CODE(op_pop_2)
DECLARE_OP_CODE(op_pop_3)
DECLARE_OP_CODE(op_pop_n)
DECLARE_OP_CODE(op_copy_r_to_i)
DECLARE_OP_CODE(op_copy_string_r_to_i)
DECLARE_OP_CODE(op_string_move_r_to_i)
DECLARE_OP_CODE(op_string_move_r_to_stack)

// Function and nativeFunction calls
DECLARE_OP_CODE(op_call_function)
DECLARE_OP_CODE(op_call_native_function)
DECLARE_OP_CODE(op_parameter_load_r)
DECLARE_OP_CODE(op_parameter_load_i)
DECLARE_OP_CODE(op_parameter_push)
DECLARE_OP_CODE(op_string_parameter_load_r)
DECLARE_OP_CODE(op_string_parameter_load_i)
DECLARE_OP_CODE(op_string_parameter_push)

// Variable operations
DECLARE_OP_CODE(op_function_variable_set)
DECLARE_OP_CODE(op_function_variable_get_r)
DECLARE_OP_CODE(op_function_variable_string_get_r)
DECLARE_OP_CODE(op_function_variable_get_i)
DECLARE_OP_CODE(op_function_variable_string_get_i)
DECLARE_OP_CODE(op_function_variable_push)
DECLARE_OP_CODE(op_function_variable_string_push)
DECLARE_OP_CODE(op_dynamic_variable_set)
DECLARE_OP_CODE(op_dynamic_variable_string_set)
DECLARE_OP_CODE(op_dynamic_variable_get_r)
DECLARE_OP_CODE(op_dynamic_variable_string_get_r)
DECLARE_OP_CODE(op_dynamic_variable_get_i)
DECLARE_OP_CODE(op_dynamic_variable_string_get_i)
DECLARE_OP_CODE(op_dynamic_variable_push)
DECLARE_OP_CODE(op_dynamic_variable_string_push)
DECLARE_OP_CODE(op_context_variable_set)
DECLARE_OP_CODE(op_context_variable_string_set)
DECLARE_OP_CODE(op_context_variable_get_r)
DECLARE_OP_CODE(op_context_variable_string_get_r)
DECLARE_OP_CODE(op_context_variable_get_i)
DECLARE_OP_CODE(op_context_variable_string_get_i)
DECLARE_OP_CODE(op_context_variable_push)
DECLARE_OP_CODE(op_context_variable_string_push)

// Boolean operators
DECLARE_OP_CODE(op_boolean_load_false_r)
DECLARE_OP_CODE(op_boolean_load_false_i)
DECLARE_OP_CODE(op_boolean_load_true_r)
DECLARE_OP_CODE(op_boolean_load_true_i)
DECLARE_OP_CODE(op_boolean_push_false)
DECLARE_OP_CODE(op_boolean_push_true)
DECLARE_OP_CODE(op_boolean_not)
DECLARE_OP_CODE(op_boolean_and)
DECLARE_OP_CODE(op_boolean_or)
DECLARE_OP_CODE(op_boolean_equal)
DECLARE_OP_CODE(op_boolean_not_equal)

// Integer operators
DECLARE_OP_CODE(op_integer_load_zero_r)
DECLARE_OP_CODE(op_integer_load_zero_i)
DECLARE_OP_CODE(op_integer_load_one_r)
DECLARE_OP_CODE(op_integer_load_one_i)
DECLARE_OP_CODE(op_integer_load_r)
DECLARE_OP_CODE(op_integer_load_i)
DECLARE_OP_CODE(op_integer_push_zero)
DECLARE_OP_CODE(op_integer_push_one)
DECLARE_OP_CODE(op_integer_push_n)
DECLARE_OP_CODE(op_integer_absolute)
DECLARE_OP_CODE(op_integer_negate)
DECLARE_OP_CODE(op_integer_add)
DECLARE_OP_CODE(op_integer_subtract)
DECLARE_OP_CODE(op_integer_multiply)
DECLARE_OP_CODE(op_integer_divide)
DECLARE_OP_CODE(op_integer_modulo)
DECLARE_OP_CODE(op_integer_and)
DECLARE_OP_CODE(op_integer_or)
DECLARE_OP_CODE(op_integer_xor)
DECLARE_OP_CODE(op_integer_equal)
DECLARE_OP_CODE(op_integer_not_equal)
DECLARE_OP_CODE(op_integer_less_than)
DECLARE_OP_CODE(op_integer_less_than_or_equal)
DECLARE_OP_CODE(op_integer_greater_than)
DECLARE_OP_CODE(op_integer_greater_than_or_equal)

// Float operators
DECLARE_OP_CODE(op_float_load_zero_r)
DECLARE_OP_CODE(op_float_load_zero_i)
DECLARE_OP_CODE(op_float_load_one_r)
DECLARE_OP_CODE(op_float_load_one_i)
DECLARE_OP_CODE(op_float_load_r)
DECLARE_OP_CODE(op_float_load_i)
DECLARE_OP_CODE(op_float_push_zero)
DECLARE_OP_CODE(op_float_push_one)
DECLARE_OP_CODE(op_float_push_n)
DECLARE_OP_CODE(op_float_absolute)
DECLARE_OP_CODE(op_float_negate)
DECLARE_OP_CODE(op_float_add)
DECLARE_OP_CODE(op_float_subtract)
DECLARE_OP_CODE(op_float_multiply)
DECLARE_OP_CODE(op_float_divide)
DECLARE_OP_CODE(op_float_modulo)
DECLARE_OP_CODE(op_float_sin)
DECLARE_OP_CODE(op_float_cosine)
DECLARE_OP_CODE(op_float_tangent)
DECLARE_OP_CODE(op_float_equal)
DECLARE_OP_CODE(op_float_not_equal)
DECLARE_OP_CODE(op_float_less_than)
DECLARE_OP_CODE(op_float_less_than_or_equal)
DECLARE_OP_CODE(op_float_greater_than)
DECLARE_OP_CODE(op_float_greater_than_or_equal)

// String operators
DECLARE_OP_CODE(op_string_init_r)
DECLARE_OP_CODE(op_string_init_i)
DECLARE_OP_CODE(op_string_free_r)
DECLARE_OP_CODE(op_string_free_i)
DECLARE_OP_CODE(op_string_free_variable)
DECLARE_OP_CODE(op_string_free_stack)
DECLARE_OP_CODE(op_string_push_r)
DECLARE_OP_CODE(op_string_push_i)
DECLARE_OP_CODE(op_string_push_n)
DECLARE_OP_CODE(op_string_pop_r)
DECLARE_OP_CODE(op_string_pop_i)
DECLARE_OP_CODE(op_string_load_r)
DECLARE_OP_CODE(op_string_load_i)
DECLARE_OP_CODE(op_string_equal)
DECLARE_OP_CODE(op_string_not_equal)

// String reference (`$stringref`) operators. References are borrowed pointers and are never freed.
DECLARE_OP_CODE(op_stringref_load_r)
DECLARE_OP_CODE(op_stringref_load_i)
DECLARE_OP_CODE(op_stringref_push)
DECLARE_OP_CODE(op_stringref_peek_r)
DECLARE_OP_CODE(op_stringref_peek_i)

// Format string operators
DECLARE_OP_CODE(op_format_string_execute)

// ID operators
DECLARE_OP_CODE(op_id_push_r)
DECLARE_OP_CODE(op_id_push_v)
DECLARE_OP_CODE(op_id_load_r)
DECLARE_OP_CODE(op_id_load_i)

// Debug
DECLARE_OP_CODE(op_assert)

} // namespace nomad
