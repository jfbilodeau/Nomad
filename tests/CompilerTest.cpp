// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/script/ClassType.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>

#include <array>
#include <limits>
#include <type_traits>
#include <vector>

using namespace nomad;

static_assert(std::is_abstract_v<OpCodeEmitter>);
static_assert(std::has_virtual_destructor_v<OpCodeEmitter>);
static_assert(std::is_base_of_v<OpCodeEmitter, Compiler>);
static_assert(static_cast<int>(ExpressionTarget::Result) == 0);
static_assert(static_cast<int>(ExpressionTarget::Intermediate) == 1);
static_assert(static_cast<int>(ExpressionTarget::Stack) == 2);

BOOST_AUTO_TEST_CASE(type_load_emission_preserves_every_target_and_operand) {
    Runtime runtime;
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    const std::array<const Type*, 2> types{runtime.getIntegerType(), runtime.getStringType()};
    const std::array<ExpressionTarget, 3> targets{
        ExpressionTarget::Result, ExpressionTarget::Intermediate, ExpressionTarget::Stack
    };
    const std::array<NomadString, 12> valueNames{
        OpCodes::op_function_variable_get_r, OpCodes::op_function_variable_get_i, OpCodes::op_function_variable_push,
        OpCodes::op_parameter_load_r, OpCodes::op_parameter_load_i, OpCodes::op_parameter_push,
        OpCodes::op_dynamic_variable_get_r, OpCodes::op_dynamic_variable_get_i, OpCodes::op_dynamic_variable_push,
        OpCodes::op_context_variable_get_r, OpCodes::op_context_variable_get_i, OpCodes::op_context_variable_push
    };
    const std::array<NomadString, 12> objectNames{
        OpCodes::op_function_variable_object_get_r,
        OpCodes::op_function_variable_object_get_i,
        OpCodes::op_function_variable_object_push,
        OpCodes::op_object_parameter_load_r,
        OpCodes::op_object_parameter_load_i,
        OpCodes::op_object_parameter_push,
        OpCodes::op_dynamic_variable_get_r,
        OpCodes::op_dynamic_variable_get_i,
        OpCodes::op_dynamic_variable_push,
        OpCodes::op_context_variable_object_get_r,
        OpCodes::op_context_variable_object_get_i,
        OpCodes::op_context_variable_object_push
    };
    const std::array<NomadId, 4> operands{17, 19, 23, 29};
    for (const auto* type : types) {
        const auto& names = type->needsFree() ? objectNames : valueNames;
        for (auto targetIndex = NomadIndex{0}; targetIndex < targets.size(); ++targetIndex) {
            instructions.clear();
            BOOST_TEST(type->emitLoadFunctionVariable(compiler, operands[0], targets[targetIndex]) == 1U);
            BOOST_TEST(type->emitLoadParameter(compiler, operands[1], targets[targetIndex]) == 3U);
            BOOST_TEST(type->emitLoadDynamicVariable(compiler, operands[2], targets[targetIndex]) == 5U);
            BOOST_TEST(type->emitLoadContextVariable(compiler, operands[3], targets[targetIndex]) == 7U);
            BOOST_REQUIRE(instructions.size() == 8U);
            for (auto index = NomadIndex{0}; index < operands.size(); ++index) {
                BOOST_TEST(runtime.getInstructionId(instructions[index * 2].fn).id ==
                    runtime.getInstructionId(names[index * 3 + targetIndex]).id);
                BOOST_TEST(instructions[index * 2 + 1].value.getIdValue() == operands[index]);
            }
        }
    }
    instructions.clear();
    BOOST_CHECK_THROW(
        runtime.getIntegerType()->emitLoadParameter(compiler, 1, static_cast<ExpressionTarget>(99)),
        NomadBug
    );
    BOOST_TEST(instructions.empty());
}

BOOST_AUTO_TEST_CASE(type_register_emission_preserves_copy_and_move_ownership_instructions) {
    Runtime runtime;
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    const std::array<const Type*, 2> types{runtime.getIntegerType(), runtime.getStringType()};
    const std::array<NomadString, 7> valueNames{
        OpCodes::op_push_r, OpCodes::op_copy_r_to_i, OpCodes::op_copy_i_to_r,
        OpCodes::op_push_r, OpCodes::op_push_i, OpCodes::op_pop_r, OpCodes::op_pop_i
    };
    const std::array<NomadString, 7> objectNames{
        OpCodes::op_object_push_r, OpCodes::op_object_move_r_to_i, OpCodes::op_object_move_i_to_r,
        OpCodes::op_object_move_r_to_stack, OpCodes::op_object_push_i,
        OpCodes::op_object_pop_r, OpCodes::op_object_pop_i
    };
    for (const auto* type : types) {
        instructions.clear();
        BOOST_TEST(compiler.addPushResult(type) == 0U);
        compiler.addMoveResult(type, ExpressionTarget::Intermediate);
        compiler.addMoveIntermediateToResult(type);
        compiler.addMoveResult(type, ExpressionTarget::Stack);
        BOOST_TEST(compiler.addPushIntermediate(type) == 4U);
        BOOST_TEST(compiler.addPopResult(type) == 5U);
        BOOST_TEST(compiler.addPopIntermediate(type) == 6U);
        BOOST_REQUIRE(instructions.size() == 7U);
        const auto& names = type->needsFree() ? objectNames : valueNames;
        for (auto index = NomadIndex{0}; index < names.size(); ++index) {
            BOOST_TEST(runtime.getInstructionId(instructions[index].fn).id ==
                runtime.getInstructionId(names[index]).id);
        }
        compiler.addMoveResult(type, ExpressionTarget::Result);
        BOOST_TEST(instructions.size() == 7U);
        BOOST_CHECK_THROW(compiler.addMoveResult(type, static_cast<ExpressionTarget>(99)), NomadBug);
        BOOST_TEST(instructions.size() == 7U);
    }
}

BOOST_AUTO_TEST_CASE(type_store_emission_preserves_existing_instruction_and_id_encoding) {
    Runtime runtime;
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    const std::array<const Type*, 2> types{runtime.getIntegerType(), runtime.getStringType()};
    const std::array<NomadString, 4> names{
        OpCodes::op_function_variable_set, OpCodes::op_dynamic_variable_set,
        OpCodes::op_context_variable_set, OpCodes::op_parameter_set
    };
    const std::array<NomadId, 4> operands{17, 19, 23, 29};
    for (const auto* type : types) {
        instructions.clear();
        BOOST_TEST(type->emitStoreFunctionVariable(compiler, operands[0]) == 1U);
        BOOST_TEST(type->emitStoreDynamicVariable(compiler, operands[1]) == 3U);
        BOOST_TEST(type->emitStoreContextVariable(compiler, operands[2]) == 5U);
        BOOST_TEST(type->emitStoreParameter(compiler, operands[3]) == 7U);
        BOOST_REQUIRE(instructions.size() == 8U);
        for (auto index = NomadIndex{0}; index < operands.size(); ++index) {
            BOOST_TEST(runtime.getInstructionId(instructions[index * 2].fn).id ==
                runtime.getInstructionId(names[index]).id);
            BOOST_TEST(instructions[index * 2 + 1].value.getIdValue() == operands[index]);
        }
    }
}

BOOST_AUTO_TEST_CASE(type_initialization_emission_preserves_empty_and_reference_defaults) {
    Runtime runtime;
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    runtime.getIntegerType()->emitInitResult(compiler);
    runtime.getIntegerType()->emitInitIntermediate(compiler);
    runtime.getVoidType()->emitInitResult(compiler);
    runtime.getVoidType()->emitInitIntermediate(compiler);
    BOOST_TEST(instructions.empty());
    BOOST_TEST(!runtime.getIntegerType()->needsFree());
    BOOST_TEST(!runtime.getVoidType()->needsFree());
    ClassType classType(&runtime, TypeId::INVALID, "Default", ClassMode::Closed);
    classType.emitInitResult(compiler);
    classType.emitInitIntermediate(compiler);
    runtime.getStringType()->emitInitResult(compiler);
    runtime.getStringType()->emitInitIntermediate(compiler);
    BOOST_REQUIRE(instructions.size() == 4U);
    const std::array<NomadString, 4> names{
        OpCodes::op_object_init_r, OpCodes::op_object_init_i,
        OpCodes::op_string_init_r, OpCodes::op_string_init_i
    };
    for (auto index = NomadIndex{0}; index < names.size(); ++index) {
        BOOST_TEST(runtime.getInstructionId(instructions[index].fn).id == runtime.getInstructionId(names[index]).id);
    }
}

BOOST_AUTO_TEST_CASE(constant_loads_use_the_shared_script_target_dispatch) {
    Runtime runtime;
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    const std::array<ExpressionTarget, 3> targets{
        ExpressionTarget::Result, ExpressionTarget::Intermediate, ExpressionTarget::Stack
    };
    const std::array<NomadString, 3> names{
        OpCodes::op_constant_get_r, OpCodes::op_constant_get_i, OpCodes::op_constant_push
    };
    for (auto index = NomadIndex{0}; index < targets.size(); ++index) {
        instructions.clear();
        BOOST_TEST(compiler.addLoadConstant(ConstantId{17}, targets[index]) == 1U);
        BOOST_REQUIRE(instructions.size() == 2U);
        BOOST_TEST(runtime.getInstructionId(instructions[0].fn).id == runtime.getInstructionId(names[index]).id);
        BOOST_TEST(instructions[1].value.getIdValue() == 17);
    }
}

BOOST_AUTO_TEST_CASE(compiler_uses_type_overrides_for_load_copy_move_and_parameter_store) {
    class EmissionType final : public ClassType {
    public:
        using ClassType::ClassType;

        NomadIndex emitLoadFunctionVariable(
            OpCodeEmitter& emitter,
            NomadId,
            ExpressionTarget
        ) const override {
            const auto instruction = getRuntime()->getInstructionFn(
                getRuntime()->getInstructionId(OpCodes::op_nop)
            );
            return emitter.addOpCode(instruction);
        }
        NomadIndex emitCopyResultToStack(OpCodeEmitter& emitter) const override {
            const auto instruction = getRuntime()->getInstructionFn(
                getRuntime()->getInstructionId(OpCodes::op_nop)
            );
            return emitter.addOpCode(instruction);
        }
        void emitMoveResultToIntermediate(OpCodeEmitter& emitter) const override {
            const auto instruction = getRuntime()->getInstructionFn(
                getRuntime()->getInstructionId(OpCodes::op_nop)
            );
            emitter.addOpCode(instruction);
        }
        NomadIndex emitStoreParameter(OpCodeEmitter& emitter, NomadId) const override {
            const auto instruction = getRuntime()->getInstructionFn(
                getRuntime()->getInstructionId(OpCodes::op_nop)
            );
            return emitter.addOpCode(instruction);
        }
    };
    Runtime runtime;
    EmissionType type(&runtime, TypeId::INVALID, "Emission", ClassMode::Closed);
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    BOOST_TEST(compiler.addLoadFunctionVariable(VariableId{1}, &type, ExpressionTarget::Result) == 0U);
    BOOST_TEST(compiler.addPushResult(&type) == 1U);
    compiler.addMoveResult(&type, ExpressionTarget::Intermediate);
    BOOST_REQUIRE(instructions.size() == 3U);
    for (const auto& instruction : instructions) {
        BOOST_TEST(runtime.getInstructionId(instruction.fn).id == runtime.getInstructionId(OpCodes::op_nop).id);
    }
    instructions.clear();
    compiler.addStoreParameter(5, &type);
    BOOST_REQUIRE(instructions.size() == 3U);
    BOOST_TEST(runtime.getInstructionId(instructions[0].fn).id ==
        runtime.getInstructionId(OpCodes::op_object_free_parameter).id);
    BOOST_TEST(instructions[1].value.getIdValue() == 5);
    BOOST_TEST(runtime.getInstructionId(instructions[2].fn).id == runtime.getInstructionId(OpCodes::op_nop).id);
}

BOOST_AUTO_TEST_CASE(opcode_emitter_writes_directly_through_compiler) {
    Runtime runtime;
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    OpCodeEmitter& emitter = compiler;
    const auto instruction = runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_nop));
    BOOST_REQUIRE(instruction != nullptr);
    BOOST_TEST(emitter.addOpCode(instruction) == 0U);
    BOOST_TEST(emitter.addId(NomadId{23}) == 1U);
    BOOST_TEST(emitter.addIndex(std::numeric_limits<NomadIndex>::max()) == 2U);
    BOOST_REQUIRE(instructions.size() == 3U);
    BOOST_TEST(instructions[0].fn == instruction);
    BOOST_TEST(instructions[1].value.getIdValue() == 23);
    BOOST_TEST(instructions[2].value.getIndexValue() == std::numeric_limits<NomadIndex>::max());
    BOOST_CHECK_THROW(emitter.addOpCode(nullptr), NomadBug);
    const auto unregistered = +[](VirtualMachine*) {};
    BOOST_CHECK_THROW(emitter.addOpCode(unregistered), NomadBug);
    BOOST_TEST(instructions.size() == 3U);
}

BOOST_AUTO_TEST_CASE(value_and_void_types_emit_no_cleanup_instructions) {
    Runtime runtime;
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    OpCodeEmitter& emitter = compiler;
    for (const auto* type : {runtime.getIntegerType(), runtime.getFloatType(), runtime.getBooleanType(),
        runtime.getVoidType(), runtime.getCallbackType({}, runtime.getVoidType())}) {
        type->emitFreeResult(emitter);
        type->emitFreeIntermediate(emitter);
        type->emitFreeStackValue(emitter, 5);
        type->emitFreeFunctionVariable(emitter, 7);
        type->emitFreeParameter(emitter, 9);
    }
    BOOST_TEST(instructions.empty());
}

BOOST_AUTO_TEST_CASE(class_types_emit_existing_cleanup_instructions_and_operands) {
    Runtime runtime;
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    OpCodeEmitter& emitter = compiler;
    ClassType classType(&runtime, TypeId::INVALID, "Cleanup", ClassMode::Closed);
    const std::array<const Type*, 2> types{runtime.getStringType(), &classType};
    for (const auto* type : types) {
        instructions.clear();
        type->emitFreeResult(emitter);
        type->emitFreeIntermediate(emitter);
        type->emitFreeStackValue(emitter, std::numeric_limits<NomadIndex>::max());
        type->emitFreeFunctionVariable(emitter, 11);
        type->emitFreeParameter(emitter, 13);
        BOOST_REQUIRE(instructions.size() == 8U);
        const std::array<NomadIndex, 5> positions{0, 1, 2, 4, 6};
        const std::array<NomadString, 5> names{
            OpCodes::op_object_free_r,
            OpCodes::op_object_free_i,
            OpCodes::op_object_free_stack,
            OpCodes::op_object_free_variable,
            OpCodes::op_object_free_parameter
        };
        for (auto index = NomadIndex{0}; index < positions.size(); ++index) {
            BOOST_TEST(runtime.getInstructionId(instructions[positions[index]].fn).id ==
                runtime.getInstructionId(names[index]).id);
        }
        BOOST_TEST(instructions[3].value.getIndexValue() == std::numeric_limits<NomadIndex>::max());
        BOOST_TEST(instructions[5].value.getIndexValue() == 11U);
        BOOST_TEST(instructions[7].value.getIdValue() == 13);
    }
}

BOOST_AUTO_TEST_CASE(parameter_cleanup_is_delegated_to_the_type) {
    class ParameterEmissionType final : public ClassType {
    public:
        using ClassType::ClassType;

        void emitFreeParameter(OpCodeEmitter& emitter, NomadId) const override {
            const auto instruction = getRuntime()->getInstructionFn(
                getRuntime()->getInstructionId(OpCodes::op_nop)
            );
            emitter.addOpCode(instruction);
        }
    };
    Runtime runtime;
    ParameterEmissionType type(&runtime, TypeId::INVALID, "ParameterEmission", ClassMode::Closed);
    BOOST_TEST(type.needsFree());
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    compiler.addStoreParameter(5, &type);
    BOOST_REQUIRE(instructions.size() == 3U);
    BOOST_TEST(runtime.getInstructionId(instructions[0].fn).id == runtime.getInstructionId(OpCodes::op_nop).id);
    BOOST_TEST(runtime.getInstructionId(instructions[1].fn).id ==
        runtime.getInstructionId(OpCodes::op_parameter_set).id);
    BOOST_TEST(instructions[2].value.getIdValue() == 5);
}

BOOST_AUTO_TEST_CASE(script_registration_reports_collisions_immediately)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"answer", {"first.nomad", "return 1"}}}, {}});
    BOOST_REQUIRE(runtime.getFunctionId("answer").isValid());
    compiler->registerSources(&context, {{{"answer", {"second.nomad", "return 2"}}}, {}});

    BOOST_REQUIRE(context.getErrorCount() == 1U);
    BOOST_TEST(context.getFirstError()->sourceName == "second.nomad");
    BOOST_TEST(context.getFirstError()->message.find("already used") != NomadString::npos);
    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_TEST(context.getErrorCount() == 1U);
}

BOOST_AUTO_TEST_CASE(source_registration_reports_loading_diagnostics_immediately)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const SourceBatch batch{
        {{"broken", {"untitled:broken", "return missing"}}},
        {{DiagnosticSeverity::Warning, "Loading warning", "loader", 1, 2},
         {DiagnosticSeverity::Error, "Loading error", "loader", 3, 4}}
    };

    compiler->registerSources(&context, batch);

    BOOST_TEST(context.getWarningCount() == 1U);
    BOOST_TEST(context.getErrorCount() == 1U);
    BOOST_REQUIRE(context.getDiagnostics().size() == 2U);
    BOOST_TEST(context.getDiagnostics()[0].message == "Loading warning");
    BOOST_TEST(context.getDiagnostics()[1].message == "Loading error");
    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_TEST(context.getDiagnostics().size() == 2U);
}

BOOST_AUTO_TEST_CASE(compilation_does_not_ignore_preexisting_context_errors)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"broken", {"broken.nomad", "return missing"}}}, {}});
    context.reportError("Existing error");

    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE(context.getDiagnostics().size() == 1U);
    BOOST_TEST(context.getFirstError()->message == "Existing error");
}

BOOST_AUTO_TEST_CASE(script_registration_and_compilation_require_matching_contexts)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    auto otherCompiler = runtime.createCompiler();
    CompilerContext otherContext(otherCompiler.get());
    const SourceBatch batch;

    BOOST_CHECK_THROW(compiler->registerSources(nullptr, batch), NomadBug);
    BOOST_CHECK_THROW(compiler->registerSources(&otherContext, batch), NomadBug);
    BOOST_CHECK_THROW((void)compiler->createFunction(nullptr, NOMAD_INVALID_ID, "test"), NomadBug);
    BOOST_CHECK_THROW((void)compiler->createFunction(&otherContext, NOMAD_INVALID_ID, "test"), NomadBug);
    CompilerContext context(compiler.get());
    BOOST_CHECK_THROW((void)compiler->createFunction(&context, NOMAD_INVALID_ID, "test"), NomadBug);
    BOOST_CHECK_THROW((void)compiler->getScript(NOMAD_INVALID_ID), NomadBug);
    BOOST_CHECK_THROW((void)compiler->getScriptId(CallableId::INVALID), NomadBug);
    BOOST_CHECK_THROW(compiler->compileFunctions(nullptr), NomadBug);
    BOOST_CHECK_THROW(compiler->compileFunctions(&otherContext), NomadBug);
}

BOOST_AUTO_TEST_CASE(statement_parsers_register_without_interpreter_metadata)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();

    compiler->registerParseStatementFn("console.test", StatementAvailability::Compiler, nullptr);

    BOOST_TEST(compiler->isStatement("console.test"));
}

BOOST_AUTO_TEST_CASE(registered_source_batches_are_owned_by_the_compiler)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    {
        SourceBatch batch{{{"answer", {"untitled:answer", "return 42"}}}, {}};
        compiler->registerSources(&context, batch);
        batch.sources.front().source.text = "return missing";
    }
    BOOST_REQUIRE(compiler->compileFunctions(&context));

    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("answer"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(context_variables_compile_and_execute_with_global_ids)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"contextVariable", {"contextVariable.nomad",
        "global.value = 17\nreturn global.value"}}}, {}});
    const auto functionId = runtime.getFunctionId("contextVariable");
    BOOST_REQUIRE(compiler->compileFunctions(&context));

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 17);
}

BOOST_AUTO_TEST_CASE(context_variables_in_format_strings_use_global_ids)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"contextFormatString", {"contextFormatString.nomad",
        "global.value = 17\nreturn $\"value {global.value}\""}}}, {}});
    const auto functionId = runtime.getFunctionId("contextFormatString");
    BOOST_REQUIRE(compiler->compileFunctions(&context));

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getStringValue() == "value 17");

    runtime.getStringType()->freeValue(&result);
}

BOOST_AUTO_TEST_CASE(functions_keep_their_script_identity_across_registration_and_sorting)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const auto path = std::filesystem::path{"not-on-disk"} / "z.nomad";
    const NomadString text =
        "fun declared value:int\n"
        "    return value + 1\n"
        "end\n"
        "return $\"{declared 41}\"";
    {
        SourceBatch batch{{{"z", {"editor:z", text, path}}}, {}};
        compiler->registerSources(&context, batch);
        batch.sources.front().source.filePath.reset();
    }
    const auto scriptId = compiler->getScriptId(runtime.getFunctionId("z"));
    compiler->registerSources(&context, {{{"a", {"untitled:a", "return 1"}}}, {}});
    BOOST_REQUIRE(compiler->compileFunctions(&context));

    const auto& script = compiler->getScript(scriptId);
    BOOST_TEST(script.scriptName == "z");
    BOOST_TEST(script.source.sourceName == "editor:z");
    BOOST_TEST(script.source.text == text);
    BOOST_REQUIRE(script.source.filePath.has_value());
    BOOST_TEST(*script.source.filePath == path);
    BOOST_TEST(compiler->getScriptId(runtime.getFunctionId("declared")) == scriptId);
    BOOST_TEST(compiler->getScriptId(runtime.getFunctionId("a")) != scriptId);
    BOOST_TEST(!compiler->getScript(compiler->getScriptId(runtime.getFunctionId("a"))).source.filePath.has_value());

    std::vector<ScriptFunction*> functions;
    runtime.getFunctions(functions);
    auto generatedCount = NomadIndex{0};
    for (const auto* function : functions) {
        if (function->getParentId() == runtime.getFunctionId("z")) {
            ++generatedCount;
            BOOST_TEST(compiler->getScriptId(function->getId()) == scriptId);
            BOOST_TEST(function->getPath() == "editor:z");
            BOOST_TEST(function->getSourceCode() == text);
        }
    }
    BOOST_TEST(generatedCount == 1U);
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("z"), {}, result);
    BOOST_TEST(result.getStringValue() == "42");
    runtime.getStringType()->freeValue(&result);
}

BOOST_AUTO_TEST_CASE(script_lookup_rejects_invalid_and_out_of_range_ids)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"answer", {"answer.nomad", "return 42"}}}, {}});
    const auto scriptId = compiler->getScriptId(runtime.getFunctionId("answer"));
    BOOST_TEST(isValidId(scriptId));
    BOOST_TEST(compiler->getScript(scriptId).scriptName == "answer");

    for (const auto invalidId : {NOMAD_INVALID_ID, NomadId{-2}, NomadId{1}, NOMAD_ID_MAX}) {
        BOOST_CHECK_THROW((void)compiler->getScript(invalidId), NomadBug);
        BOOST_CHECK_THROW((void)compiler->createFunction(&context, invalidId, "invalid"), NomadBug);
    }
    BOOST_TEST(runtime.getFunctionId("invalid").isInvalid());
    BOOST_TEST(!context.hasError());
}

BOOST_AUTO_TEST_CASE(function_creation_reports_collisions_against_its_own_script)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{
        {"first", {"untitled:first", "return 1"}},
        {"second", {"untitled:second", "return 2"}}
    }, {}});
    const auto scriptId = compiler->getScriptId(runtime.getFunctionId("second"));
    const auto functionId = compiler->createFunction(&context, scriptId, "first");

    BOOST_TEST(functionId.isInvalid());
    BOOST_REQUIRE(context.getErrorCount() == 1U);
    BOOST_TEST(context.getFirstError()->sourceName == "untitled:second");
    BOOST_TEST(context.getFirstError()->message.find("untitled:first") != NomadString::npos);
    BOOST_TEST(!compiler->compileFunctions(&context));
}
