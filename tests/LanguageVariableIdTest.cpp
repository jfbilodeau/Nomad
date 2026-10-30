// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/game/EntityVariableContext.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/Variable.hpp>
#include <nomad/script/VariableContext.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <limits>
#include <type_traits>

using namespace nomad;
using namespace nomad::test;

static_assert(std::is_same_v<decltype(IdentifierDefinition{}.variableId), VariableId>);
static_assert(std::is_same_v<decltype(IdentifierDefinition{}.parameterId), NomadId>);
static_assert(std::is_same_v<decltype(Capture{}.variableId), VariableId>);
static_assert(std::is_same_v<decltype(Capture{}.parameterId), NomadId>);
static_assert(!std::is_convertible_v<NomadId, VariableId>);
static_assert(!std::is_convertible_v<VariableId, NomadId>);
static_assert(!std::is_convertible_v<ContextVariableId, VariableId>);
static_assert(!std::is_convertible_v<VariableContextId, VariableId>);
static_assert(!std::is_invocable_v<decltype(&VariableMap::getVariableType), VariableMap&, NomadId>);
static_assert(!std::is_invocable_v<decltype(&VariableMap::getVariableOffset), const VariableMap&, NomadId>);
static_assert(!std::is_invocable_v<decltype(&Compiler::addLoadFunctionVariable),
    Compiler&, ContextVariableId, const Type*, ExpressionTarget>);
static_assert(!std::is_invocable_v<decltype(&ScriptFunction::flagVariableAsCaptured),
    ScriptFunction&, NomadId, const NomadString&, const Type*>);
static_assert(!std::is_invocable_v<decltype(&ScriptFunction::flagParameterAsCaptured),
    ScriptFunction&, VariableId, const NomadString&, const Type*>);

namespace {

class VariableLayoutTestType final : public ClassType {
public:
    VariableLayoutTestType(Runtime* runtime, const NomadIndex size):
        ClassType(runtime, TypeId::INVALID, "VariableLayoutTest", ClassMode::Closed),
        m_size(size)
    {}

    [[nodiscard]] NomadIndex getSize() const override {
        return m_size;
    }

private:
    const NomadIndex m_size;
};

} // namespace

BOOST_AUTO_TEST_SUITE(language_variable_ids)

BOOST_AUTO_TEST_CASE(variable_layout_separates_logical_ids_from_physical_slots)
{
    Runtime runtime;
    const VariableLayoutTestType emptyType(&runtime, 0);
    const VariableLayoutTestType pointType(&runtime, 3);
    const VariableLayoutTestType pairType(&runtime, 2);
    VariableMap variables;
    BOOST_TEST(variables.getStorageSize() == 0U);

    const auto first = variables.registerVariable("first", &emptyType);
    const auto point = variables.registerVariable("point", &pointType);
    const auto text = variables.registerVariable("text", runtime.getStringType());
    const auto lastEmpty = variables.registerVariable("lastEmpty", &emptyType);
    const auto pair = variables.registerVariable("pair", &pairType);

    BOOST_TEST(variables.getVariableCount() == 5U);
    BOOST_TEST(first == VariableId{0});
    BOOST_TEST(point == VariableId{1});
    BOOST_TEST(text == VariableId{2});
    BOOST_TEST(lastEmpty == VariableId{3});
    BOOST_TEST(pair == VariableId{4});
    BOOST_TEST(variables.getVariableOffset(first) == 0U);
    BOOST_TEST(variables.getVariableOffset(point) == 0U);
    BOOST_TEST(variables.getVariableOffset(text) == 3U);
    BOOST_TEST(variables.getVariableOffset(lastEmpty) == 4U);
    BOOST_TEST(variables.getVariableOffset(pair) == 4U);
    BOOST_TEST(variables.getStorageSize() == 6U);
}

BOOST_AUTO_TEST_CASE(variable_layout_queries_reflect_current_type_metadata)
{
    Runtime runtime;
    const VariableLayoutTestType emptyType(&runtime, 0);
    const VariableLayoutTestType pointType(&runtime, 3);
    VariableMap variables;
    const auto inferred = variables.registerVariable("inferred", nullptr);
    const auto following = variables.registerVariable("following", runtime.getIntegerType());

    BOOST_TEST(variables.getVariableOffset(inferred) == 0U);
    BOOST_CHECK_THROW((void)variables.getVariableOffset(following), NomadException);
    BOOST_CHECK_THROW((void)variables.getStorageSize(), NomadException);

    BOOST_TEST(variables.registerVariable("inferred", &pointType) == inferred);
    BOOST_TEST(variables.getVariableOffset(following) == 3U);
    BOOST_TEST(variables.getStorageSize() == 4U);

    variables.setVariableType(inferred, &emptyType);
    BOOST_TEST(variables.getVariableOffset(following) == 0U);
    BOOST_TEST(variables.getStorageSize() == 1U);
    const auto appended = variables.registerVariable("appended", &pointType);
    BOOST_TEST(variables.getVariableOffset(appended) == 1U);
    BOOST_TEST(variables.getStorageSize() == 4U);
    BOOST_TEST(variables.getVariableCount() == 3U);
    BOOST_TEST(variables.getVariableId("inferred") == inferred);
    BOOST_TEST(variables.getVariableId("following") == following);
}

BOOST_AUTO_TEST_CASE(variable_layout_reports_deferred_sizes_until_the_type_is_resolved)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    VariableMap variables;
    const auto deferred = variables.registerVariable("deferred", compiler->deferType({"Point"}));
    const auto following = variables.registerVariable("following", runtime.getIntegerType());

    BOOST_CHECK_THROW((void)variables.getVariableOffset(following), NomadException);
    BOOST_CHECK_THROW((void)variables.getStorageSize(), NomadException);
    variables.setVariableType(deferred, runtime.getFloatType());
    BOOST_TEST(variables.getVariableOffset(following) == 1U);
    BOOST_TEST(variables.getStorageSize() == 2U);
}

BOOST_AUTO_TEST_CASE(variable_layout_checks_overflow_without_rejecting_representable_sizes)
{
    Runtime runtime;
    const auto maximumSize = std::numeric_limits<NomadIndex>::max();
    const VariableLayoutTestType maximumType(&runtime, maximumSize);
    const VariableLayoutTestType emptyType(&runtime, 0);
    VariableMap variables;
    const auto maximum = variables.registerVariable("maximum", &maximumType);
    const auto empty = variables.registerVariable("empty", &emptyType);

    BOOST_TEST(variables.getVariableOffset(maximum) == 0U);
    BOOST_TEST(variables.getVariableOffset(empty) == maximumSize);
    BOOST_TEST(variables.getStorageSize() == maximumSize);

    const auto overflow = variables.registerVariable("overflow", runtime.getIntegerType());
    BOOST_TEST(variables.getVariableOffset(overflow) == maximumSize);
    BOOST_CHECK_THROW((void)variables.getStorageSize(), NomadException);
    const auto following = variables.registerVariable("following", runtime.getIntegerType());
    BOOST_CHECK_THROW((void)variables.getVariableOffset(following), NomadException);
}

BOOST_AUTO_TEST_CASE(variable_layout_rejects_invalid_variable_ids)
{
    Runtime runtime;
    VariableMap variables;
    BOOST_CHECK_THROW((void)variables.getVariableOffset(VariableId{0}), NomadException);
    const auto first = variables.registerVariable("first", runtime.getIntegerType());
    BOOST_TEST(variables.getVariableOffset(first) == 0U);
    for (const auto id : {VariableId::INVALID, VariableId{-2}, VariableId{1}, VariableId{NOMAD_ID_MAX}}) {
        BOOST_CHECK_THROW((void)variables.getVariableOffset(id), NomadException);
    }
}

BOOST_AUTO_TEST_CASE(variable_map_ids_are_owner_local_and_stable)
{
    Runtime runtime;
    VariableMap variables;
    VariableMap otherVariables;
    BOOST_TEST(VariableId{} == VariableId::INVALID);
    BOOST_TEST(variables.getVariableId("missing") == VariableId::INVALID);
    const auto id = variables.registerVariable("value", nullptr);
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(variables.registerVariable("value", runtime.getIntegerType()) == id);
    BOOST_TEST(variables.registerVariable("value", runtime.getStringType()) == id);
    BOOST_TEST(variables.getVariableType(id) == runtime.getIntegerType());
    BOOST_TEST(otherVariables.registerVariable("different", runtime.getFloatType()) == id);
    BOOST_TEST(otherVariables.getVariableType(id) == runtime.getFloatType());
    for (auto index = 0; index < 64; ++index) {
        BOOST_REQUIRE(variables.registerVariable("additional" + std::to_string(index), runtime.getIntegerType()).isValid());
    }
    BOOST_TEST(variables.getVariableCount() == 65U);
    BOOST_TEST(variables.getStorageSize() == 65U);
    BOOST_TEST(variables.getVariableId("value") == id);
    BOOST_TEST(variables.getVariableOffset(id) == 0U);
    BOOST_TEST(variables.getVariableName(id) == "value");
    BOOST_TEST(!variables.isRead(id));
    BOOST_TEST(!variables.isWritten(id));
    variables.setRead(id, true);
    variables.setWritten(id, true);
    BOOST_TEST(variables.isRead(id));
    BOOST_TEST(variables.isWritten(id));

    VariableList values(&variables);
    values.setVariableValue(id, RuntimeValue{NomadInteger{42}});
    RuntimeValue value;
    values.getVariableValue(id, value);
    BOOST_TEST(value.getIntegerValue() == 42);
    BOOST_TEST(values.getVariableId("value") == id);
    BOOST_TEST(values.getVariableType(id) == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(context_iteration_and_entity_mirrors_use_typed_local_ids)
{
    Runtime runtime;
    SimpleVariableContext context;
    BOOST_TEST(context.getFirstVariableId() == VariableId::INVALID);
    const auto first = context.registerVariable("first", runtime.getIntegerType());
    const auto second = context.registerVariable("second", runtime.getStringType());
    BOOST_TEST(context.getFirstVariableId() == first);
    BOOST_TEST(context.getNextVariableId(first) == second);
    BOOST_TEST(context.getNextVariableId(second) == VariableId::INVALID);

    ThisEntityVariableContext thisContext(nullptr);
    OtherEntityVariableContext otherContext(&thisContext);
    const auto health = thisContext.registerVariable("this.health", runtime.getIntegerType());
    const auto mana = otherContext.registerVariable("other.mana", runtime.getFloatType());
    BOOST_REQUIRE(health.isValid());
    BOOST_REQUIRE(mana.isValid());
    BOOST_TEST(health != mana);
    BOOST_TEST(otherContext.getVariableId("other.health") == health);
    BOOST_TEST(thisContext.getVariableId("this.mana") == mana);
    BOOST_TEST(thisContext.getVariableCount() == 2U);
    BOOST_TEST(otherContext.getVariableCount() == 2U);
    BOOST_TEST(thisContext.getFirstVariableId() == health);
    BOOST_TEST(otherContext.getNextVariableId(health) == mana);
    BOOST_TEST(otherContext.getNextVariableId(mana) == VariableId::INVALID);
    thisContext.setWritten(health, true);
    otherContext.setRead(mana, true);
    BOOST_TEST(otherContext.isWritten(health));
    BOOST_TEST(thisContext.isRead(mana));
}

BOOST_AUTO_TEST_CASE(capture_sources_keep_variable_ids_separate_from_parameter_positions)
{
    Runtime runtime;
    ScriptFunction function(CallableId{}, "nested", "nested.nomad", "");
    const auto variableCapture = function.flagVariableAsCaptured(VariableId{2}, "local", runtime.getIntegerType());
    const auto parameterCapture = function.flagParameterAsCaptured(NomadId{2}, "argument", runtime.getStringType());
    BOOST_TEST(variableCapture == NomadId{0});
    BOOST_TEST(parameterCapture == NomadId{1});
    BOOST_REQUIRE(function.getCaptureCount() == 2U);
    const auto& captures = function.getCaptures();
    BOOST_TEST((captures[0].source == CaptureSource::Variable));
    BOOST_TEST(captures[0].variableId == VariableId{2});
    BOOST_TEST((captures[1].source == CaptureSource::Parameter));
    BOOST_TEST(captures[1].parameterId == NomadId{2});
    BOOST_TEST(function.isCaptured(VariableId{2}));
    BOOST_TEST(function.isCaptured(NomadId{2}));
    BOOST_TEST(!function.isCaptured(VariableId{3}));
    BOOST_TEST(!function.isCaptured(NomadId{3}));
    BOOST_TEST(function.flagVariableAsCaptured(VariableId{2}, "local", runtime.getIntegerType()) == variableCapture);
    BOOST_TEST(function.flagParameterAsCaptured(NomadId{2}, "argument", runtime.getStringType()) == parameterCapture);
    BOOST_TEST(function.getCaptureCount() == 2U);
    BOOST_CHECK_THROW(function.flagVariableAsCaptured(VariableId{3}, "local", runtime.getIntegerType()), NomadBug);
    BOOST_CHECK_THROW(function.flagParameterAsCaptured(NomadId{2}, "local", runtime.getIntegerType()), NomadBug);
}

BOOST_AUTO_TEST_CASE(local_loads_keep_existing_scalar_and_object_bytecode)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    ScriptFunction function(CallableId{}, "owner", "owner.nomad", "");
    for (const auto* type : std::array<const Type*, 2>{runtime.getIntegerType(), runtime.getStringType()}) {
        const auto name = "local" + type->getTypeName();
        const auto id = function.registerVariable(name, type);
        const auto originalCount = runtime.getInstructions().size();
        const auto result = compiler->addLoadFunctionVariable(id, type, ExpressionTarget::Result);
        const auto intermediate = compiler->addLoadFunctionVariable(id, type, ExpressionTarget::Intermediate);
        const auto stack = compiler->addLoadFunctionVariable(id, type, ExpressionTarget::Stack);
        const auto& instructions = runtime.getInstructions();
        BOOST_REQUIRE(instructions.size() == originalCount + 6);
        for (const auto operand : {result, intermediate, stack}) {
            BOOST_TEST(instructions[operand].value.getIdValue() == id.id);
        }
        const auto resultOpCode = type->isString() ? OpCodes::op_function_variable_object_get_r :
            OpCodes::op_function_variable_get_r;
        const auto intermediateOpCode = type->isString() ? OpCodes::op_function_variable_object_get_i :
            OpCodes::op_function_variable_get_i;
        const auto stackOpCode = type->isString() ? OpCodes::op_function_variable_object_push :
            OpCodes::op_function_variable_push;
        BOOST_TEST(instructions[result - 1].fn == runtime.getInstructionFn(runtime.getInstructionId(resultOpCode)));
        BOOST_TEST(instructions[intermediate - 1].fn == runtime.getInstructionFn(runtime.getInstructionId(intermediateOpCode)));
        BOOST_TEST(instructions[stack - 1].fn == runtime.getInstructionFn(runtime.getInstructionId(stackOpCode)));
        IdentifierDefinition definition{};
        compiler->getIdentifierDefinition(name, &function, definition);
        BOOST_TEST((definition.identifierType == IdentifierType::FunctionVariable));
        BOOST_TEST(definition.variableId == id);
    }
}

BOOST_AUTO_TEST_CASE(interpreter_console_ids_preserve_lookup_and_reassignment)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("value = 7", context));
    const auto id = context.getFunction()->getVariableId("value");
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(context.getVariableTypeById(id) == runtime.getIntegerType());
    BOOST_REQUIRE(context.getVariableValueById(id) != nullptr);
    BOOST_TEST(context.getVariableValueById(id)->getIntegerValue() == 7);
    BOOST_REQUIRE(Interpreter{}.execute("value = \"changed\"", context));
    BOOST_TEST(context.getFunction()->getVariableId("value") == id);
    BOOST_TEST(context.getVariableTypeById(id) == runtime.getStringType());
    BOOST_TEST(context.getVariableValueById(id)->getStringValue() == "changed");
    BOOST_TEST(context.getVariableTypeById(VariableId::INVALID) == nullptr);
    BOOST_TEST(context.getVariableValueById(VariableId::INVALID) == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
