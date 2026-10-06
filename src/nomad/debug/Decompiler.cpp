// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/debug/Decompiler.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/script/Function.hpp>

#include <iomanip>
#include <sstream>

namespace nomad {

const NomadString DEFAULT_PREFIX = "  ";

struct DecompileState {
    Runtime* runtime;
    Function* function;
    size_t index;
    std::stringstream output;
    std::vector<OpCodeDefinition> opcodes;

    void start_line(const NomadString& text = "") {
        output << "|" << std::setw(4) << std::setfill('0') << (index-1) << "|  ";
        output << text;
    }

    void append(const NomadString& text) {
        output << text;
    }

    void end_line(const NomadString& text = "") {
        output << text << std::endl;
    }
};

void decompile_expression(DecompileState& state, const NomadString& prefix);
void decompile_string_expression(DecompileState& state, const NomadString& prefix);

void decompile_nativeFunction_parameters(DecompileState& state, const NomadString& prefix, const OpCodeDefinition& nativeFunction) {
    for (const auto& parameter : nativeFunction.parameters) {
        switch (parameter.type) {
            case NativeFunctionParameterType::Number:
                decompile_expression(state, prefix + DEFAULT_PREFIX);
                break;
            case NativeFunctionParameterType::String:
                decompile_string_expression(state, prefix + DEFAULT_PREFIX);
                break;
            case NativeFunctionParameterType::Variable: {
                const auto variable_type = state.function->get_op_code(state.index);
                state.index++;

                const auto variable_context = state.function->get_op_code(state.index);
                state.index++;

                state.start_line(prefix + " Variable reference: ");

                switch (variable_type) {
                    case VARIABLE_TYPE_CONSTANT:
                        state.append(state.runtime->get_constant_name(variable_context));
                        break;
                    case VARIABLE_TYPE_DYNAMIC:
                        state.append(state.runtime->get_dynamic_variable_name(variable_context));
                        break;
                    case VARIABLE_TYPE_FUNCTION:
                        state.append(state.function->get_variable_name(variable_context));
                        break;
                    default:
                        state.append(state.runtime->get_context_variable_name(variable_type, variable_context));
                        break;
                }
                state.end_line();

                break;
            }
            case NativeFunctionParameterType::VarArgs: {
                const auto var_count = state.function->get_op_code(state.index);
                state.index++;

                state.start_line(prefix + " VarArgs: " + std::to_string(var_count));
                state.end_line();

                for (auto i = 0; i < var_count; i++) {
                    decompile_expression(state, prefix + DEFAULT_PREFIX);
                }

                break;
            }
            case NativeFunctionParameterType::Callback: {
                const auto function_id = state.function->get_op_code(state.index);
                state.index++;

                const auto function = state.runtime->get_function(function_id);

                state.start_line(prefix + " Function reference: " + function->get_name() + "[" + std::to_string(function_id) + "]");
                state.end_line();
                break;
            }
            default:
                state.output << "Error: Unexpected parameter type: " << static_cast<int>(parameter.type)
                             << std::endl;
        }
    }
}

void decompile_expression(DecompileState& state, const NomadString& prefix) {
    auto operator_id = state.function->get_op_code(state.index);
    state.index++;

    if (operator_id < 0 || operator_id >= state.opcodes.size()) {
        state.output << "<<unknown operator>>";
        return;
    }

    auto& opcode = state.opcodes[operator_id];

    state.start_line(prefix + opcode.name + "[" + std::to_string(opcode.id) + "]");

    if (opcode.name == "op_load_zero") {
        state.end_line();
    } else if (opcode.name == "op_load_one") {
        state.end_line();
    } else if (opcode.name == "op_load_double") {
        state.output << " " << state.function->get_float(state.index);
        state.index += sizeof(NomadValue) / sizeof(OpCode);
        state.end_line();
    } else if (opcode.name == "op_load_constant") {
        auto constant_id = state.function->get_op_code(state.index);
        state.index++;
        auto constant_name = state.runtime->get_constant_name(constant_id);
        auto constant_value = state.runtime->get_constant_value(constant_id);

        state.output << " " << constant_name << " (" << constant_value << ")";
        state.end_line();
    } else if (opcode.name == "op_call_native_function") {
        auto nativeFunction_id = state.function->get_op_code(state.index);
        state.index++;

        auto& nativeFunction = state.opcodes[nativeFunction_id];

        decompile_nativeFunction_parameters(state, prefix + DEFAULT_PREFIX, nativeFunction);
    } else if (opcode.name == "op_call_function") {
        auto function_id = state.function->get_op_code(state.index);
        state.index++;
        auto function = state.runtime->get_function(function_id);
        state.output << " " << function->get_name();
        state.end_line();

        for (auto i = 0; i < function->get_parameter_count(); i++) {
            decompile_expression(state, prefix + DEFAULT_PREFIX);
        }
    } else if (opcode.name == "op_load_dynamic_variable") {
        auto variable_id = state.function->get_op_code(state.index);
        state.index++;
        auto variable_name = state.runtime->get_dynamic_variable_name(variable_id);
        state.output << " " << variable_name << "[" << variable_id << "]";
        state.end_line();
    } else if (opcode.name == "op_load_context_variable") {
        auto context_id = state.function->get_op_code(state.index);
        state.index++;
        auto variable_id = state.function->get_op_code(state.index);
        state.index++;
        auto variable_name = state.runtime->get_context_variable_name(context_id, variable_id);
        state.output << " " << variable_name << "[" << context_id << ", " << variable_id << "]";
        state.end_line();
    } else if (opcode.name == "op_load_function_variable") {
        auto variable_id = state.function->get_op_code(state.index);
        state.index++;
        auto variable_name = state.function->get_variable_name(variable_id);
        state.output << " " << variable_name << "[" << variable_id << "]";
        state.end_line();
    } else if (opcode.name == "op_load_string_id") {
        auto string_id = state.function->get_op_code(state.index);
        state.index++;
        auto string = state.runtime->get_string(string_id);
        state.output << " \"" << string << "\"[" << string_id << "]";
        state.end_line();
    } else if (opcode.name == "op_not") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_sin") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_cos") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_tan") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_unary_minus") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_unary_plus") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_add") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_sub") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_mul") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_div") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_mod") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_equal") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_not_equal") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_less_than") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_less_than_or_equal") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_greater_than") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_greater_than_or_equal") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_and") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else if (opcode.name == "op_or") {
        state.end_line();
        decompile_expression(state, prefix + DEFAULT_PREFIX);
        decompile_expression(state, prefix + DEFAULT_PREFIX);
    } else {
        state.output << "<<unknown expression: '" << opcode.name << "' [" << opcode.id << "]>>" << std::endl;
    }
}

void decompile_string_expression(DecompileState& state, const NomadString& prefix) {
    auto string_operator_id = state.function->get_op_code(state.index);
    state.index++;

    if (string_operator_id < 0 || string_operator_id >= state.opcodes.size()) {
        state.output << "<<unknown string operator>>";
        return;
    }

    auto& opcode = state.opcodes[string_operator_id];

    state.start_line(prefix);
    state.output << opcode.name << "[" << opcode.id << "]";

    if (opcode.name == "sop_load_string") {
        auto string_id = state.function->get_op_code(state.index);
        state.index++;
        auto string = state.runtime->get_string(string_id);
        state.append(" \"" + string + "\"[" + std::to_string(string_id) + "]");
        state.end_line();
    } else if (opcode.name == "sop_load_string_variable") {
        auto variable_type = state.function->get_op_code(state.index);
        state.index++;
        auto context_id = state.function->get_op_code(state.index);
        state.index++;
        auto variable_id = state.function->get_op_code(state.index);
        state.index++;

        NomadString variable_name;

        switch (variable_type) {
            case VARIABLE_TYPE_CONSTANT:
                variable_name = state.runtime->get_constant_name(variable_id);
                break;
            case VARIABLE_TYPE_DYNAMIC:
                variable_name = state.runtime->get_dynamic_variable_name(variable_id);
                break;
            case VARIABLE_TYPE_FUNCTION:
                variable_name = state.function->get_variable_name(variable_id);
                break;
            default:
                variable_name = state.runtime->get_context_variable_name(context_id, variable_id);
                break;
        }
        state.output << " " << variable_name;
        state.end_line();
    } else if (opcode.name == "sop_format_string") {
        auto format_string_id = state.function->get_op_code(state.index);
        state.index++;
        auto format_string = state.function->get_format_string(format_string_id);
        state.output << " $\"" << format_string.get_format_string() << " \"(" << format_string_id << ")";
        state.end_line();
    } else if (opcode.name == "sop_load_constant") {
        auto constant_id = state.function->get_op_code(state.index);
        auto constant_value = state.runtime->get_constant_value(constant_id);
        auto string_id = state.runtime->get_string(to_nomad_id(constant_value));
        state.index++;
        state.output << " [" << constant_id << "] = " << constant_value << " -> \"" << string_id << "\"";
        state.end_line();
    } else if (opcode.name == "sop_load_function_variable") {
        auto variable_id = state.function->get_op_code(state.index);
        state.index++;
        auto variable_name = state.function->get_variable_name(variable_id);
        state.output << " " << variable_name << "[" << variable_id << "]";
        state.end_line();
    } else if (opcode.name == "sop_load_dynamic_variable") {
        auto variable_id = state.function->get_op_code(state.index);
        state.index++;
        auto variable_name = state.runtime->get_dynamic_variable_name(variable_id);
        state.output << " " << variable_name << "[" << variable_id << "]";
        state.end_line();
    } else if (opcode.name == "sop_load_context_variable") {
        auto context_id = state.function->get_op_code(state.index);
        state.index++;
        auto variable_id = state.function->get_op_code(state.index);
        state.index++;
        auto variable_name = state.runtime->get_context_variable_name(context_id, variable_id);
        state.output << " " << variable_name << "[" << context_id << ", " << variable_id << "]";
        state.end_line();
    } else {
        state.output << "<<unknown function expression>>" << std::endl;
    }
}

void decompile_instruction(DecompileState& state, const OpCodeRegistration& opcode) {
    if (opcode.name == "i_nop") {
        // Nothing to add
        state.end_line();
    } else if (opcode.name == "i_return") {
        state.end_line();
        decompile_expression(state, DEFAULT_PREFIX);
    } else if (opcode.name == "i_return_zero") {
        state.end_line();
    } else if (opcode.name == "i_yield") {
        state.end_line();
        decompile_expression(state, DEFAULT_PREFIX);
    } else if (opcode.name == "i_call_native_function") {
        const auto nativeFunction_id = state.function->get_op_code(state.index);
        state.index++;

        const auto& nativeFunction = state.opcodes[nativeFunction_id];

        state.output << ": " << nativeFunction.name << "[" << nativeFunction.id << "]";
        state.end_line();

        decompile_nativeFunction_parameters(state, DEFAULT_PREFIX, nativeFunction);
    } else if (opcode.name == "i_call_function") {
        const auto function_id = state.function->get_op_code(state.index);
        state.index++;

        auto function = state.runtime->get_function(function_id);

        state.output << " " << function->get_name();
        state.end_line();

        for (auto i = 0; i < function->get_parameter_count(); i++) {
            decompile_expression(state, DEFAULT_PREFIX);
        }
    } else if (opcode.name == "i_jump") {
        const auto jump_index = state.function->get_op_code(state.index);
        state.index++;

        state.output << " " << jump_index;
        state.end_line();
    } else if (opcode.name == "i_if") {
        state.output << " [" << state.function->get_op_code(state.index) << "]";

        state.index++;

        state.end_line();
        decompile_expression(state, DEFAULT_PREFIX);
    } else if (opcode.name == "i_set_constant") {
        const auto constant_id = state.function->get_op_code(state.index);
        state.index++;

        const auto constant_name = state.runtime->get_constant_name(constant_id);

        state.output << " " << constant_name << "[" << constant_id << "]";
        state.end_line();

        decompile_expression(state, DEFAULT_PREFIX);

    } else if (opcode.name == "i_set_dynamic_variable") {
        const auto variable_id = state.function->get_op_code(state.index);
        state.index++;

        const auto variable_name = state.runtime->get_dynamic_variable_name(variable_id);

        state.output << " " << variable_name << "[" << variable_id << "]";
        state.end_line();

        decompile_expression(state, DEFAULT_PREFIX);
    } else if (opcode.name == "i_set_function_variable") {
        const auto variable_id = state.function->get_op_code(state.index);
        state.index++;

        const auto variable_name = state.function->get_variable_name(variable_id);

        state.output << " " << variable_name << "[" << variable_id << "]";
        state.end_line();

        decompile_expression(state, DEFAULT_PREFIX);
    } else if (opcode.name == "i_set_context_variable") {
        const auto context_id = state.function->get_op_code(state.index);
        state.index++;

        const auto variable_id = state.function->get_op_code(state.index);
        state.index++;

        const auto variable_name = state.runtime->get_context_variable_name(context_id, variable_id);

        state.output << " " << variable_name << "[" << context_id << ", " << variable_id << "]";
        state.end_line();

        decompile_expression(state, DEFAULT_PREFIX);
    } else if (opcode.name == "i_assert") {
        state.end_line();
        decompile_expression(state, DEFAULT_PREFIX);
    } else if (opcode.name == "i_string") {
        state.end_line();
        decompile_expression(state, DEFAULT_PREFIX);
    } else {
        state.output << "<<unknown instruction>>" << std::endl;
    }
}

//void decompile_nativeFunction(DecompileState& state, OpCodeRegistration& opcode) {
//
//    for (auto& parameter: opcode.parameters) {
//        switch (parameter.type) {
//            case NativeFunctionParameterType::Number:
//                decompile_expression(state, DEFAULT_PREFIX);
//                break;
//            case NativeFunctionParameterType::String:
//                decompile_string_expression(state, DEFAULT_PREFIX);
//                break;
//            case NativeFunctionParameterType::Variable:
//                state.output << "TODO: NativeFunctionParameterType::Variable" << std::endl;
//                break;
//            case NativeFunctionParameterType::VarArgs:
//                state.output << "TODO: NativeFunctionParameterType::VarArgs" << std::endl;
//                break;
//            case NativeFunctionParameterType::Function:
//                state.output << "TODO: NativeFunctionParameterType::Function" << std::endl;
//                break;
//            default:
//                state.output << "Error: Unexpected parameter type: " << static_cast<int>(parameter.type) << std::endl;
//        }
//    }
//}

NomadString decompile(Runtime* runtime, Function* function) {
    try {
        DecompileState state;

        state.runtime = runtime;
        state.function = function;
        state.index = 0;

        runtime->get_opcode_definitions(state.opcodes);

        while (state.index < function->get_code_size()) {
            auto opcode_id = function->get_op_code(state.index);
            state.index++;

            if (opcode_id >= state.opcodes.size()) {
                state.output << "Invalid opcode: " << opcode_id << std::endl;
                break; // Stop decompiling
            }

            auto& opcode = state.opcodes[opcode_id];

            state.start_line();
            state.output << opcode.name << "[" << opcode.id << "]";

            switch (opcode.type) {
                case OpCodeType::instruction:
                    decompile_instruction(state, opcode);
                    break;

                case OpCodeType::nativeFunction:
                    decompile_nativeFunction_parameters(state, DEFAULT_PREFIX, opcode);
                    break;

                default:
                    state.output << "<<unknown opcode type (" << opcode.id << ")>>" << std::endl;
                    break;
            }
        }

        return state.output.str();
    } catch (const std::exception& e) {
        return "Error: Failed to decompile function: " + NomadString(e.what());
    } catch (...) {
        return "Error: Failed to decompile function";
    }
}

} // nomad
