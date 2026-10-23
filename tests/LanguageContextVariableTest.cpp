// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/FormatString.hpp>
#include <nomad/script/Hub.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/VariableContext.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <type_traits>

using namespace nomad;
using namespace nomad::test;

static_assert(std::is_same_v<decltype(IdentifierDefinition{}.contextVariableId), ContextVariableId>);
static_assert(!std::is_convertible_v<NomadId, VariableContextId>);
static_assert(!std::is_convertible_v<VariableContextId, NomadId>);
static_assert(!std::is_convertible_v<NomadId, ContextVariableId>);
static_assert(!std::is_convertible_v<ContextVariableId, NomadId>);
static_assert(!std::is_invocable_v<decltype(&Runtime::getVariableContext), Runtime&, ContextVariableId>);
static_assert(!std::is_invocable_v<decltype(&Runtime::getContextVariableType), Runtime&, VariableContextId>);
static_assert(!std::is_invocable_v<decltype(&Runtime::getContextVariableType), Runtime&, DynamicVariableId>);
static_assert(!std::is_invocable_v<decltype(&Compiler::addLoadContextVariable),
    Compiler&, NomadId, const Type*, ExpressionTarget>);

BOOST_AUTO_TEST_SUITE(language_context_variables)

BOOST_AUTO_TEST_CASE(context_ids_support_lookup_iteration_and_catalog_growth)
{
    Runtime runtime;
    BOOST_TEST(VariableContextId{} == VariableContextId::INVALID);
    BOOST_TEST(runtime.getContextId("missing") == VariableContextId::INVALID);
    BOOST_TEST(runtime.getVariableContextIdByPrefix("missing.value") == VariableContextId::INVALID);
    BOOST_TEST(runtime.getVariableContext(VariableContextId::INVALID) == nullptr);
    BOOST_TEST(runtime.getVariableContext(VariableContextId{999999}) == nullptr);
    auto originalCount = NomadIndex{0};
    for (auto id = runtime.getFirstVariableContextId(); id.isValid(); id = runtime.getNextVariableContextId(id)) {
        ++originalCount;
    }

    auto context = std::make_unique<SimpleVariableContext>();
    const auto* original = context.get();
    const auto id = runtime.registerVariableContext("values", "values.", std::move(context));
    BOOST_REQUIRE(id.isValid());
    auto lastId = id;
    for (auto index = 0; index < 64; ++index) {
        const auto name = "additional" + std::to_string(index);
        lastId = runtime.registerVariableContext(name, name + ".", std::make_unique<SimpleVariableContext>());
        BOOST_REQUIRE(lastId.isValid());
    }
    BOOST_TEST(runtime.getContextId("values") == id);
    BOOST_TEST(runtime.getContextName(id) == "values");
    BOOST_TEST(runtime.getVariableContextIdByPrefix("values.answer") == id);
    BOOST_TEST(runtime.getVariableContext(id) == original);

    auto count = NomadIndex{0};
    auto visitedId = VariableContextId::INVALID;
    for (auto currentId = runtime.getFirstVariableContextId(); currentId.isValid();
         currentId = runtime.getNextVariableContextId(currentId)) {
        BOOST_REQUIRE(count < originalCount + 65);
        visitedId = currentId;
        ++count;
    }
    BOOST_TEST(count == originalCount + 65);
    BOOST_TEST(visitedId == lastId);
    BOOST_TEST(runtime.getNextVariableContextId(lastId) == VariableContextId::INVALID);
}

BOOST_AUTO_TEST_CASE(global_handles_preserve_context_and_local_identity_after_growth)
{
    Runtime runtime;
    BOOST_TEST(ContextVariableId{} == ContextVariableId::INVALID);
    BOOST_TEST(runtime.getContextVariableId("missing.value") == ContextVariableId::INVALID);
    BOOST_TEST(runtime.registerOrUpdateContextVariable("missing.value", runtime.getIntegerType()) ==
        ContextVariableId::INVALID);
    BOOST_TEST(runtime.registerContextVariable(VariableContextId::INVALID, "missing.value", runtime.getIntegerType()) ==
        ContextVariableId::INVALID);
    BOOST_TEST(runtime.getContextVariableType(ContextVariableId::INVALID) == nullptr);
    BOOST_TEST(runtime.getContextVariableType(ContextVariableId{999999}) == nullptr);

    const auto firstContextId = runtime.registerVariableContext(
        "first", "first.", std::make_unique<SimpleVariableContext>());
    const auto secondContextId = runtime.registerVariableContext(
        "second", "second.", std::make_unique<SimpleVariableContext>());
    const auto first = runtime.registerContextVariable(firstContextId, "first.value", runtime.getIntegerType());
    const auto second = runtime.registerContextVariable(secondContextId, "second.value", nullptr);
    BOOST_REQUIRE(first.isValid());
    BOOST_REQUIRE(second.isValid());
    BOOST_TEST(first != second);
    auto* firstContext = runtime.getVariableContext(firstContextId);
    auto* secondContext = runtime.getVariableContext(secondContextId);
    const auto firstLocalId = firstContext->getVariableId("first.value");
    const auto secondLocalId = secondContext->getVariableId("second.value");
    BOOST_TEST(firstLocalId == secondLocalId);
    BOOST_TEST(second.id != secondLocalId.id);
    BOOST_TEST(runtime.registerOrUpdateContextVariable("second.value", runtime.getIntegerType()) == second);
    BOOST_TEST(runtime.getContextVariableId("second.missing") == ContextVariableId::INVALID);

    runtime.setContextVariableValue(first, RuntimeValue{NomadInteger{17}});
    runtime.setContextVariableValue(second, RuntimeValue{NomadInteger{29}});
    runtime.setContextVariableRead(second, true);
    runtime.setContextVariableWritten(first, true);
    BOOST_TEST(!firstContext->isRead(firstLocalId));
    BOOST_TEST(secondContext->isRead(secondLocalId));
    BOOST_TEST(firstContext->isWritten(firstLocalId));
    BOOST_TEST(!secondContext->isWritten(secondLocalId));
    runtime.setContextVariableRead(second, false);
    runtime.setContextVariableWritten(first, false);
    BOOST_TEST(!secondContext->isRead(secondLocalId));
    BOOST_TEST(!firstContext->isWritten(firstLocalId));

    for (auto index = 0; index < 64; ++index) {
        const auto name = "second.additional" + std::to_string(index);
        BOOST_REQUIRE(runtime.registerContextVariable(secondContextId, name, runtime.getIntegerType()).isValid());
    }
    BOOST_TEST(runtime.getContextVariableId("first.value") == first);
    BOOST_TEST(runtime.getContextVariableId("second.value") == second);
    BOOST_TEST(runtime.registerContextVariable(secondContextId, "second.value", runtime.getIntegerType()) == second);
    BOOST_TEST(runtime.getContextVariableName(first) == "first.value");
    BOOST_TEST(runtime.getContextVariableName(second) == "second.value");
    BOOST_TEST(runtime.getContextVariableType(second) == runtime.getIntegerType());
    RuntimeValue firstValue;
    RuntimeValue secondValue;
    runtime.getContextVariableValue(first, firstValue);
    runtime.getContextVariableValue(second, secondValue);
    BOOST_TEST(firstValue.getIntegerValue() == 17);
    BOOST_TEST(secondValue.getIntegerValue() == 29);
}

BOOST_AUTO_TEST_CASE(hub_variable_registration_returns_global_handles_but_members_keep_local_ids)
{
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerHub("example").isValid());
    const auto id = runtime.registerHubVariable("example.answer");
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(runtime.getContextVariableId("example.answer") == id);
    const auto contextId = runtime.getVariableContextIdByPrefix("example.answer");
    BOOST_REQUIRE(contextId.isValid());
    BOOST_TEST(runtime.getContextId("example") == contextId);
    const auto* context = runtime.getVariableContext(contextId);
    const auto* member = runtime.getHub(runtime.getHubId("example"))->getMember("answer");
    BOOST_REQUIRE(member != nullptr);
    const auto* variable = std::get_if<VariableId>(member);
    BOOST_REQUIRE(variable != nullptr);
    BOOST_TEST(*variable == context->getVariableId("example.answer"));
    BOOST_TEST(runtime.registerOrUpdateContextVariable("example.answer", runtime.getIntegerType()) == id);
    runtime.setContextVariableValue(id, RuntimeValue{NomadInteger{42}});
    RuntimeValue value;
    runtime.getContextVariableValue(id, value);
    BOOST_TEST(value.getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(context_loads_encode_global_handles_for_scalar_and_object_targets)
{
    Runtime runtime;
    const auto contextId = runtime.registerVariableContext(
        "values", "values.", std::make_unique<SimpleVariableContext>());
    auto compiler = runtime.createCompiler();
    for (const auto* type : std::array<const Type*, 2>{runtime.getIntegerType(), runtime.getStringType()}) {
        const auto name = "values." + type->getTypeName();
        const auto id = runtime.registerContextVariable(contextId, name, type);
        BOOST_REQUIRE(id.isValid());
        const auto originalInstructionCount = runtime.getInstructions().size();
        const auto result = compiler->addLoadContextVariable(id, type, ExpressionTarget::Result);
        const auto intermediate = compiler->addLoadContextVariable(id, type, ExpressionTarget::Intermediate);
        const auto stack = compiler->addLoadContextVariable(id, type, ExpressionTarget::Stack);
        const auto& instructions = runtime.getInstructions();
        BOOST_REQUIRE(instructions.size() == originalInstructionCount + 6);
        for (const auto operand : {result, intermediate, stack}) {
            BOOST_TEST(instructions[operand].value.getIdValue() == id.id);
        }
        const auto resultOpCode = type->isString() ? OpCodes::op_context_variable_object_get_r :
            OpCodes::op_context_variable_get_r;
        const auto intermediateOpCode = type->isString() ? OpCodes::op_context_variable_object_get_i :
            OpCodes::op_context_variable_get_i;
        const auto stackOpCode = type->isString() ? OpCodes::op_context_variable_object_push :
            OpCodes::op_context_variable_push;
        BOOST_TEST(instructions[result - 1].fn == runtime.getInstructionFn(runtime.getInstructionId(resultOpCode)));
        BOOST_TEST(instructions[intermediate - 1].fn == runtime.getInstructionFn(runtime.getInstructionId(intermediateOpCode)));
        BOOST_TEST(instructions[stack - 1].fn == runtime.getInstructionFn(runtime.getInstructionId(stackOpCode)));
        IdentifierDefinition identifier{};
        compiler->getIdentifierDefinition(name, nullptr, identifier);
        BOOST_TEST((identifier.identifierType == IdentifierType::ContextVariable));
        BOOST_TEST(identifier.contextVariableId == id);
        BOOST_TEST(identifier.valueType == type);
    }
}

BOOST_AUTO_TEST_CASE(interpreter_and_formatting_resolve_global_handles_in_multiple_contexts)
{
    Runtime runtime;
    runtime.registerVariableContext("first", "first.", std::make_unique<SimpleVariableContext>());
    runtime.registerVariableContext("second", "second.", std::make_unique<SimpleVariableContext>());
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("first.value = 17", context));
    BOOST_REQUIRE(Interpreter{}.execute("second.value = 29", context));
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("second.value", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 29);
    BOOST_REQUIRE(Interpreter{}.execute("second.value = first.value + second.value", context));
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("$(\"{first.value}:{second.value}\")", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "17:46");
    BOOST_REQUIRE(Interpreter{}.execute("first.name = \"first\"", context));
    BOOST_REQUIRE(Interpreter{}.execute("second.name = \"second\"", context));
    BOOST_REQUIRE_MESSAGE(Interpreter{}.execute("second.name = $\"{second.name}!\"", context),
        (context.getError() != nullptr ? *context.getError() : "Interpreter assignment failed"));
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("second.name", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "second!");

    auto compiler = runtime.createCompiler();
    FormatString format("{first.value}:{second.value}:{second.name}", CallableId::INVALID);
    BOOST_REQUIRE(!compileFormatString(compiler.get(), nullptr, format.getFormatString(), &format).has_value());
    BOOST_TEST(format.format(machine.get()) == "17:46:second!");
}

BOOST_AUTO_TEST_CASE(compiled_context_reads_and_writes_preserve_scalar_and_string_values)
{
    const auto liveStringsBefore = RuntimeValue::getLiveStringCount();
    {
        LanguageTestFixture fixture;
        auto& runtime = fixture.getRuntime();
        runtime.registerVariableContext("first", "first.", std::make_unique<SimpleVariableContext>());
        runtime.registerVariableContext("second", "second.", std::make_unique<SimpleVariableContext>());
        fixture.addFunction("main",
            "first.value = 17\nsecond.value = 29\n"
            "first.name = \"first\"\nsecond.name = \"second\"\n"
            "second.value = first.value + second.value\nsecond.name = $\"{second.name}!\"\n"
            "return $\"{first.value}:{second.value}:{first.name}:{second.name}\"");
        const auto compiled = fixture.compile();
        BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.text == "17:46:first:second!");
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == liveStringsBefore);
}

BOOST_AUTO_TEST_SUITE_END()
