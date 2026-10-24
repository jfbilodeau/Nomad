// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/FormatString.hpp>
#include <nomad/script/Hub.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <type_traits>

using namespace nomad;
using namespace nomad::test;

static_assert(std::is_same_v<decltype(IdentifierDefinition{}.constantId), ConstantId>);
static_assert(!std::is_convertible_v<NomadId, ConstantId>);
static_assert(!std::is_convertible_v<ConstantId, NomadId>);
static_assert(!std::is_convertible_v<CallableId, ConstantId>);
static_assert(!std::is_convertible_v<ConstantId, CallableId>);
static_assert(!std::is_convertible_v<HubId, ConstantId>);
static_assert(!std::is_convertible_v<ConstantId, HubId>);
static_assert(!std::is_invocable_v<decltype(&Runtime::getConstantType), Runtime&, CallableId>);
static_assert(!std::is_invocable_v<decltype(&Compiler::addLoadConstant), Compiler&, NomadId, ExpressionTarget>);

BOOST_AUTO_TEST_SUITE(language_constants)

BOOST_AUTO_TEST_CASE(constant_ids_support_registration_lookup_iteration_and_catalog_growth)
{
    Runtime runtime;
    BOOST_TEST(runtime.getConstantId("missing") == ConstantId::INVALID);
    auto originalCount = NomadIndex{0};
    for (auto id = runtime.getFirstConstantId(); id.isValid(); id = runtime.getNextConstantId(id)) {
        ++originalCount;
    }

    BOOST_REQUIRE(runtime.registerHub("example").isValid());
    const auto id = runtime.registerConstant("example.answer", RuntimeValue{NomadInteger{42}}, runtime.getIntegerType());
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(runtime.getConstantId("example.answer") == id);
    BOOST_TEST(runtime.getConstantName(id) == "example.answer");
    BOOST_TEST(runtime.getConstantType(id) == runtime.getIntegerType());
    BOOST_TEST(runtime.registerConstant("example.answer", RuntimeValue{true}, runtime.getBooleanType()) ==
        ConstantId::INVALID);
    BOOST_TEST(runtime.registerConstant("example", RuntimeValue{NomadInteger{1}}, runtime.getIntegerType()) ==
        ConstantId::INVALID);

    auto lastId = id;
    for (auto index = 0; index < 64; ++index) {
        lastId = runtime.registerConstant("additional" + std::to_string(index),
            RuntimeValue{NomadInteger{index}}, runtime.getIntegerType());
        BOOST_REQUIRE(lastId.isValid());
    }
    BOOST_REQUIRE(runtime.registerHub("example.child").isValid());
    BOOST_TEST(runtime.getConstantId("example.answer") == id);
    BOOST_TEST(runtime.getConstantName(id) == "example.answer");
    BOOST_TEST(runtime.getConstantType(id) == runtime.getIntegerType());
    RuntimeValue value;
    runtime.getConstantValue(id, value);
    BOOST_TEST(value.getIntegerValue() == 42);
    const auto* member = runtime.getHub(runtime.getHubId("example"))->getMember("answer");
    BOOST_REQUIRE(member != nullptr);
    const auto* constant = std::get_if<ConstantId>(member);
    BOOST_REQUIRE(constant != nullptr);
    BOOST_TEST(*constant == id);

    auto count = NomadIndex{0};
    auto visitedId = ConstantId::INVALID;
    for (auto currentId = runtime.getFirstConstantId(); currentId.isValid();
         currentId = runtime.getNextConstantId(currentId)) {
        BOOST_REQUIRE(count < originalCount + 65);
        visitedId = currentId;
        ++count;
    }
    BOOST_TEST(count == originalCount + 65);
    BOOST_TEST(visitedId == lastId);
    BOOST_TEST(runtime.getNextConstantId(lastId) == ConstantId::INVALID);
}

BOOST_AUTO_TEST_CASE(constant_loads_encode_typed_ids_for_all_expression_targets)
{
    Runtime runtime;
    const auto id = runtime.registerConstant("answer", RuntimeValue{NomadInteger{42}}, runtime.getIntegerType());
    BOOST_REQUIRE(id.isValid());
    auto compiler = runtime.createCompiler();
    const auto originalInstructionCount = runtime.getInstructions().size();
    const auto resultOperand = compiler->addLoadConstant(id, ExpressionTarget::Result);
    const auto intermediateOperand = compiler->addLoadConstant(id, ExpressionTarget::Intermediate);
    const auto stackOperand = compiler->addLoadConstant(id, ExpressionTarget::Stack);
    const auto& instructions = runtime.getInstructions();
    BOOST_REQUIRE(instructions.size() == originalInstructionCount + 6);
    BOOST_TEST(instructions[resultOperand - 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_constant_get_r)));
    BOOST_TEST(instructions[intermediateOperand - 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_constant_get_i)));
    BOOST_TEST(instructions[stackOperand - 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_constant_push)));
    for (const auto operand : {resultOperand, intermediateOperand, stackOperand}) {
        BOOST_TEST(instructions[operand].value.getIdValue() == id.id);
    }

    IdentifierDefinition identifier{};
    compiler->getIdentifierDefinition("answer", nullptr, identifier);
    BOOST_TEST((identifier.identifierType == IdentifierType::Constant));
    BOOST_TEST(identifier.constantId == id);
    BOOST_TEST(identifier.valueType == runtime.getIntegerType());

    FormatString format("answer={answer}", CallableId::INVALID);
    BOOST_TEST(!compileFormatString(compiler.get(), nullptr, format.getFormatString(), &format).has_value());
    auto machine = runtime.createVirtualMachine();
    BOOST_TEST(format.format(machine.get()) == "answer=42");
}

BOOST_AUTO_TEST_CASE(constants_remain_readable_in_scripts_interpreter_and_format_strings)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    BOOST_REQUIRE(runtime.registerConstant("answer", RuntimeValue{NomadInteger{42}},
        runtime.getIntegerType()).isValid());
    fixture.addFunction("main", "const offset = 2\nreturn $\"{answer}:{answer + offset}\"");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.text == "42:44");

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("answer", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 42);
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("$(\"{answer}\")", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "42");
}

BOOST_AUTO_TEST_SUITE_END()
