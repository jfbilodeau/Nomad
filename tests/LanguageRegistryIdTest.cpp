// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/log/Logger.hpp>
#include <nomad/log/MemorySink.hpp>

#include <nomad/script/FormatString.hpp>
#include <nomad/script/Hub.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/scope_exit.hpp>
#include <boost/test/unit_test.hpp>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#include <windows.h>
#endif

#include <type_traits>

using namespace nomad;
using namespace nomad::test;

static_assert(std::is_same_v<decltype(IdentifierDefinition{}.dynamicVariableId), DynamicVariableId>);
static_assert(!std::is_convertible_v<NomadId, StringId>);
static_assert(!std::is_convertible_v<StringId, NomadId>);
static_assert(!std::is_convertible_v<NomadId, FormatStringId>);
static_assert(!std::is_convertible_v<FormatStringId, NomadId>);
static_assert(!std::is_convertible_v<NomadId, DynamicVariableId>);
static_assert(!std::is_convertible_v<DynamicVariableId, NomadId>);
static_assert(!std::is_invocable_v<decltype(&Runtime::getString), Runtime&, FormatStringId>);
static_assert(!std::is_invocable_v<decltype(&Runtime::getFormatString), Runtime&, StringId>);
static_assert(!std::is_invocable_v<decltype(&Runtime::getDynamicVariableType), Runtime&, ConstantId>);
static_assert(!std::is_invocable_v<decltype(&Compiler::addLoadDynamicVariable),
    Compiler&, NomadId, const Type*, ExpressionTarget>);

namespace {

// Keep bounds-check regressions non-interactive under the Debug CRT.
class BoundsCheckReportingFixture {
#if defined(_MSC_VER) && defined(_DEBUG)
public:
    BoundsCheckReportingFixture():
        m_previousErrorMode(GetErrorMode()),
        m_previousReportMode(_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE)),
        m_previousReportFile(_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR))
    {
        SetErrorMode(m_previousErrorMode | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    }

    ~BoundsCheckReportingFixture() {
        _CrtSetReportFile(_CRT_ASSERT, m_previousReportFile);
        _CrtSetReportMode(_CRT_ASSERT, m_previousReportMode);
        SetErrorMode(m_previousErrorMode);
    }

private:
    const UINT m_previousErrorMode;
    const int m_previousReportMode;
    const _HFILE m_previousReportFile;
#endif
};

} // namespace

BOOST_AUTO_TEST_SUITE(language_registry_ids)

BOOST_FIXTURE_TEST_CASE(
    string_lookup_failures_return_null_without_logging_or_changing_interned_objects,
    BoundsCheckReportingFixture
) {
    Runtime runtime;
    auto& logger = log::getLogger();
    logger.flush();
    const auto previousLevel = logger.getLogLevel();
    const auto sinkId = logger.createMemorySink();
    BOOST_SCOPE_EXIT_ALL(&) {
        logger.removeSink(sinkId);
        logger.setLogLevel(previousLevel);
    };

    logger.setLogLevel(LogLevel::Error);
    auto* sink = static_cast<MemorySink*>(logger.getSink(sinkId));
    BOOST_REQUIRE(sink != nullptr);
    for (const auto id : {StringId::INVALID, StringId{-2}, StringId{NOMAD_ID_MAX}}) {
        BOOST_TEST(runtime.getString(id) == nullptr);
    }

    const auto id = runtime.registerString("checked string");
    const auto* original = runtime.getString(id);
    BOOST_REQUIRE(original != nullptr);
    const auto references = original->getReferenceCount();
    const auto unregistered = StringId{id.id + 1};
    BOOST_TEST(runtime.getString(unregistered) == nullptr);
    BOOST_TEST(runtime.getStringByName("missing string") == nullptr);
    BOOST_TEST(runtime.getStringId("missing string") == StringId::INVALID);
    BOOST_TEST(runtime.registerString("checked string") == id);
    BOOST_TEST(runtime.getString(id) == original);
    BOOST_TEST(runtime.getStringByName("checked string") == original);
    BOOST_TEST(original->getReferenceCount() == references);
    const auto emptyId = runtime.registerString("");
    const auto* empty = runtime.getString(emptyId);
    BOOST_REQUIRE(empty != nullptr);
    BOOST_TEST(empty->getView().empty());
    BOOST_TEST(runtime.getStringByName("") == empty);
    logger.flush();
    BOOST_TEST(sink->getEntries().empty());
}

BOOST_FIXTURE_TEST_CASE(
    instruction_function_lookup_rejects_invalid_ids_without_changing_registered_callbacks,
    BoundsCheckReportingFixture
) {
    Runtime runtime;
    const auto id = runtime.registerInstruction(
        "registry.testLookup",
        [](VirtualMachine*) {},
        NomadDoc(""),
        {}
    );
    BOOST_REQUIRE(id.isValid());
    const auto originalFunction = runtime.getInstructionFn(id);
    BOOST_REQUIRE(originalFunction != nullptr);
    for (const auto invalidId : {
        OpCodeId::INVALID,
        OpCodeId{-2},
        OpCodeId{NOMAD_ID_MAX},
        OpCodeId{id.id + 1}
    }) {
        BOOST_TEST(runtime.getInstructionFn(invalidId) == nullptr);
    }

    BOOST_TEST(runtime.getInstructionFn(id) == originalFunction);
    BOOST_TEST(runtime.getInstructionId("registry.testLookup") == id);
    auto compiler = runtime.createCompiler();
    const auto instructionCount = compiler->getOpCodeSize();
    BOOST_CHECK_THROW(compiler->addOpCode(OpCodeId::INVALID), NomadBug);
    BOOST_TEST(compiler->getOpCodeSize() == instructionCount);
}

BOOST_AUTO_TEST_CASE(string_ids_deduplicate_and_keep_borrowed_objects_stable)
{
    Runtime runtime;
    BOOST_TEST(StringId{} == StringId::INVALID);
    BOOST_TEST(runtime.getStringId("missing") == StringId::INVALID);
    const auto id = runtime.registerString("original");
    BOOST_REQUIRE(id.isValid());
    const auto* original = runtime.getString(id);
    BOOST_REQUIRE(original != nullptr);
    const auto references = original->getReferenceCount();
    BOOST_TEST(runtime.registerString("original") == id);
    BOOST_TEST(original->getReferenceCount() == references);

    for (auto index = 0; index < 64; ++index) {
        BOOST_REQUIRE(runtime.registerString("additional" + std::to_string(index)).isValid());
    }
    BOOST_TEST(runtime.getStringId("original") == id);
    BOOST_TEST(runtime.getString(id) == original);
    BOOST_TEST(runtime.getStringByName("original") == original);
    BOOST_TEST(original->getString() == "original");
    BOOST_TEST(original->getReferenceCount() == references);
}

BOOST_AUTO_TEST_CASE(format_string_ids_preserve_text_and_callable_identity)
{
    Runtime runtime;
    const auto firstOwner = runtime.registerNativeFunction("first", [](VirtualMachine*) {},
        {}, runtime.getVoidType(), NomadDoc("First owner"));
    const auto secondOwner = runtime.registerNativeFunction("second", [](VirtualMachine*) {},
        {}, runtime.getVoidType(), NomadDoc("Second owner"));
    BOOST_REQUIRE(firstOwner.isValid());
    BOOST_REQUIRE(secondOwner.isValid());
    BOOST_TEST(FormatStringId{} == FormatStringId::INVALID);
    BOOST_TEST(runtime.getFormatStringId("missing", firstOwner) == FormatStringId::INVALID);
    BOOST_TEST(runtime.getFormatString(FormatStringId::INVALID) == nullptr);
    BOOST_TEST(runtime.getFormatString(FormatStringId{999999}) == nullptr);

    const auto first = runtime.registerFormatString("value", firstOwner);
    const auto second = runtime.registerFormatString("value", secondOwner);
    BOOST_REQUIRE(first.isValid());
    BOOST_REQUIRE(second.isValid());
    BOOST_TEST(first != second);
    const auto* original = runtime.getFormatString(first);
    BOOST_REQUIRE(original != nullptr);
    BOOST_TEST(original->getFunctionId() == firstOwner);
    BOOST_TEST(runtime.registerFormatString("value", firstOwner) == first);

    for (auto index = 0; index < 64; ++index) {
        BOOST_REQUIRE(runtime.registerFormatString("additional" + std::to_string(index), firstOwner).isValid());
    }
    BOOST_TEST(runtime.getFormatStringId("value", firstOwner) == first);
    BOOST_TEST(runtime.getFormatStringId("value", secondOwner) == second);
    BOOST_TEST(runtime.getFormatString(first) == original);
    BOOST_TEST(original->getFormatString() == "value");
}

BOOST_AUTO_TEST_CASE(string_and_dynamic_loads_keep_raw_bytecode_operands)
{
    Runtime runtime;
    const auto dynamicId = runtime.registerDynamicVariable("answer", {},
        [](VirtualMachine*, RuntimeValue& value) { value.setIntegerValue(42); },
        runtime.getIntegerType(), NomadDoc("Answer"));
    BOOST_REQUIRE(dynamicId.isValid());
    auto compiler = runtime.createCompiler();
    const auto originalInstructionCount = runtime.getInstructions().size();
    const auto stringResult = compiler->addLoadStringValue("literal", ExpressionTarget::Result);
    const auto stringIntermediate = compiler->addLoadStringValue("literal", ExpressionTarget::Intermediate);
    const auto stringStack = compiler->addLoadStringValue("literal", ExpressionTarget::Stack);
    const auto dynamicResult = compiler->addLoadDynamicVariable(dynamicId, runtime.getIntegerType(), ExpressionTarget::Result);
    const auto dynamicIntermediate = compiler->addLoadDynamicVariable(dynamicId, runtime.getIntegerType(), ExpressionTarget::Intermediate);
    const auto dynamicStack = compiler->addLoadDynamicVariable(dynamicId, runtime.getIntegerType(), ExpressionTarget::Stack);
    const auto& instructions = runtime.getInstructions();
    BOOST_REQUIRE(instructions.size() == originalInstructionCount + 12);
    const auto stringId = runtime.getStringId("literal");
    BOOST_REQUIRE(stringId.isValid());
    for (const auto operand : {stringResult, stringIntermediate, stringStack}) {
        BOOST_TEST(instructions[operand].value.getIdValue() == stringId.id);
    }
    for (const auto operand : {dynamicResult, dynamicIntermediate, dynamicStack}) {
        BOOST_TEST(instructions[operand].value.getIdValue() == dynamicId.id);
    }
    BOOST_TEST(instructions[stringResult - 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_string_load_r)));
    BOOST_TEST(instructions[stringIntermediate - 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_string_load_i)));
    BOOST_TEST(instructions[stringStack - 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_string_push_n)));
    BOOST_TEST(instructions[dynamicResult - 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_dynamic_variable_get_r)));
    BOOST_TEST(instructions[dynamicIntermediate - 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_dynamic_variable_get_i)));
    BOOST_TEST(instructions[dynamicStack - 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_dynamic_variable_push)));
}

BOOST_AUTO_TEST_CASE(dynamic_variable_ids_support_lookup_iteration_growth_and_format_segments)
{
    Runtime runtime;
    BOOST_TEST(DynamicVariableId{} == DynamicVariableId::INVALID);
    BOOST_TEST(runtime.getDynamicVariableId("missing") == DynamicVariableId::INVALID);
    auto originalCount = NomadIndex{0};
    for (auto id = runtime.getFirstDynamicVariableId(); id.isValid(); id = runtime.getNextDynamicVariableId(id)) {
        ++originalCount;
    }
    BOOST_REQUIRE(runtime.registerHub("registry", HubMode::Opened).isValid());
    NomadInteger answer = 42;
    const auto id = runtime.registerDynamicVariable("registry.answer",
        [&answer](VirtualMachine*, const RuntimeValue& value) { answer = value.getIntegerValue(); },
        [&answer](VirtualMachine*, RuntimeValue& value) { value.setIntegerValue(answer); },
        runtime.getIntegerType(), NomadDoc("Answer"));
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(runtime.canGetDynamicVariable(id));
    BOOST_TEST(runtime.canSetDynamicVariable(id));
    BOOST_TEST(runtime.registerDynamicVariable("registry", {}, {},
        runtime.getIntegerType(), NomadDoc("Conflict")) == DynamicVariableId::INVALID);
    auto lastId = id;
    for (auto index = 0; index < 64; ++index) {
        lastId = runtime.registerDynamicVariable("additional" + std::to_string(index), {},
            [](VirtualMachine*, RuntimeValue& value) { value.setIntegerValue(0); },
            runtime.getIntegerType(), NomadDoc("Additional variable"));
        BOOST_REQUIRE(lastId.isValid());
    }
    BOOST_TEST(runtime.getDynamicVariableId("registry.answer") == id);
    BOOST_TEST(runtime.getDynamicVariableName(id) == "registry.answer");
    BOOST_TEST(runtime.getDynamicVariableType(id) == runtime.getIntegerType());
    BOOST_TEST(!runtime.canSetDynamicVariable(lastId));
    auto count = NomadIndex{0};
    auto visitedId = DynamicVariableId::INVALID;
    for (auto currentId = runtime.getFirstDynamicVariableId(); currentId.isValid();
         currentId = runtime.getNextDynamicVariableId(currentId)) {
        BOOST_REQUIRE(count < originalCount + 65);
        visitedId = currentId;
        ++count;
    }
    BOOST_TEST(count == originalCount + 65);
    BOOST_TEST(visitedId == lastId);
    BOOST_TEST(runtime.getNextDynamicVariableId(lastId) == DynamicVariableId::INVALID);
    const auto* member = runtime.getHub(runtime.getHubId("registry"))->getMember("answer");
    BOOST_REQUIRE(member != nullptr);
    const auto* dynamicVariable = std::get_if<DynamicVariableId>(member);
    BOOST_REQUIRE(dynamicVariable != nullptr);
    BOOST_TEST(*dynamicVariable == id);

    auto compiler = runtime.createCompiler();
    IdentifierDefinition identifier{};
    compiler->getIdentifierDefinition("registry.answer", nullptr, identifier);
    BOOST_TEST((identifier.identifierType == IdentifierType::DynamicVariable));
    BOOST_TEST(identifier.dynamicVariableId == id);
    BOOST_TEST(identifier.valueType == runtime.getIntegerType());
    FormatString format("answer={registry.answer}", CallableId::INVALID);
    BOOST_REQUIRE(!compileFormatString(compiler.get(), nullptr, format.getFormatString(), &format).has_value());
    auto machine = runtime.createVirtualMachine();
    BOOST_TEST(format.format(machine.get()) == "answer=42");
    runtime.setDynamicVariable(machine.get(), id, RuntimeValue{NomadInteger{7}});
    BOOST_TEST(format.format(machine.get()) == "answer=7");
}

BOOST_AUTO_TEST_CASE(dynamic_ids_remain_live_in_scripts_interpreter_and_format_strings)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    NomadInteger answer = 42;
    BOOST_REQUIRE(runtime.registerDynamicVariable("answer",
        [&answer](VirtualMachine*, const RuntimeValue& value) { answer = value.getIntegerValue(); },
        [&answer](VirtualMachine*, RuntimeValue& value) { value.setIntegerValue(answer); },
        runtime.getIntegerType(), NomadDoc("Answer")).isValid());
    fixture.addFunction("main", "answer = answer + 1\nassert answer == 43\nreturn $\"value={answer}\"");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.text == "value=43");
    BOOST_TEST(outcome.leakedStrings == 0);
    BOOST_TEST(answer == 43);
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);
    BOOST_REQUIRE(context.getInterpreter().execute("answer = 7", context));
    BOOST_TEST(answer == 7);
    BOOST_REQUIRE(context.getInterpreter().execute("$(\"value={answer}\")", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "value=7");
}

BOOST_AUTO_TEST_SUITE_END()
