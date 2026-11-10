// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/script/NomadObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VariableContext.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/SystemFunctions.hpp>

#include <array>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace nomad;

static_assert(std::is_same_v<decltype(std::declval<const InterpreterContext&>().getInterpreter()), const Interpreter&>);
static_assert(!std::is_constructible_v<InterpreterContext, VirtualMachine&>);
static_assert(std::is_constructible_v<InterpreterContext, const Interpreter&>);
static_assert(!std::is_constructible_v<InterpreterContext, const Interpreter&, VirtualMachine&>);
static_assert(!std::is_constructible_v<InterpreterContext, Interpreter&&>);
static_assert(!std::is_constructible_v<InterpreterContext, const Interpreter&&>);
static_assert(!std::is_constructible_v<
    InterpreterContext, Interpreter&&, const ScriptCallableBase&>);
static_assert(!std::is_constructible_v<
    InterpreterContext, const Interpreter&&, const ScriptCallableBase&>);
static_assert(!std::is_default_constructible_v<Interpreter>);
static_assert(std::is_constructible_v<Interpreter, VirtualMachine&>);
static_assert(!std::is_constructible_v<Interpreter, VirtualMachine&&>);
static_assert(std::is_same_v<decltype(std::declval<const Interpreter&>().getVirtualMachine()), VirtualMachine&>);

namespace {

void evaluateFrameExpressions(VirtualMachine* machine) {
    auto* function = machine->getCurrentFunction();
    BOOST_REQUIRE(function != nullptr);
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator, *function);
    BOOST_TEST(&contextEvaluator.getVirtualMachine() == machine);
    BOOST_TEST(&context.getInterpreter() == &contextEvaluator);
    BOOST_TEST(context.getVirtualMachine() == machine);
    BOOST_TEST(context.getFunction() != function);
    BOOST_TEST(context.getFunction()->getParameterCount() == function->getParameterCount());
    BOOST_TEST(context.isFrameBacked());
    const auto savedResult = machine->getResult();
    const auto savedIntermediate = machine->getIntermediate();
    const auto functionsBefore = [&] {
        std::vector<ScriptFunction*> functions;
        machine->getRuntime()->getFunctions(functions);
        return functions.size();
    }();
    const auto variableCount = function->getVariableCount();
    const auto parameterCount = function->getParameterCount();
    const auto instructionCount = machine->getRuntime()->getInstructions().size();
    const auto captures = function->getCaptureCount();

    const std::vector<std::pair<NomadString, NomadString>> cases{
        {"value + local", "42"},
        {"text.toUpper", "HELLO"},
        {"answer + value", "43"},
        {"global.score + value", "45"},
        {"test.native text", "hello"},
        {"test.parameter", "40"},
        {"false && test.fail", "false"},
        {"$\"{1+2}\"", "3"},
        {"$(\"value {value} local {local} captured {captured}\")", "value 40 local 2 captured 6"},
    };
    for (const auto& [source, expected] : cases) {
        const auto succeeded = context.getInterpreter().execute(source, context);
        const auto diagnostic = context.getError() ? *context.getError() : source;
        BOOST_REQUIRE_MESSAGE(succeeded, diagnostic);
        BOOST_REQUIRE(context.getResult() != nullptr);
        BOOST_TEST(context.getResult()->getType()->toString(context.getResult()->getStorage().data()) == expected);
    }
    BOOST_REQUIRE(context.getInterpreter().execute("$\"{value}\"", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "40");
    BOOST_REQUIRE(context.getInterpreter().execute("$\"{value + local}\"", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "42");
    for (const auto* source : {"", "test.void"}) {
        BOOST_REQUIRE(context.getInterpreter().execute(source, context));
        BOOST_REQUIRE(context.getResult() != nullptr);
        BOOST_TEST(context.getResult()->getType()->isVoid());
    }
    for (const auto* source : {"missing + 1", "1 +", "1\n2", "uncaptured"}) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!context.getInterpreter().execute(source, context));
            BOOST_TEST(context.hasError());
            BOOST_TEST(context.getResult() == nullptr);
        }
    }
    BOOST_TEST(!context.getInterpreter().execute("test.fail", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "frame failure");
    BOOST_TEST(context.getResult() == nullptr);
    BOOST_REQUIRE(context.getInterpreter().execute("local = 7", context));
    BOOST_TEST(context.getVariableValue("local")->getIntegerValue() == 7);
    BOOST_TEST(!context.getInterpreter().execute("local = true", context));
    BOOST_TEST(context.getVariableValue("local")->getIntegerValue() == 7);
    BOOST_REQUIRE(context.getInterpreter().execute("value = 1", context));
    BOOST_REQUIRE(context.getInterpreter().execute("text = \"changed\"", context));
    BOOST_TEST(context.getVariableValue("value")->getIntegerValue() == 1);
    BOOST_TEST(machine->getParameter(0).getIntegerValue() == 1);
    BOOST_REQUIRE(context.getInterpreter().execute("created = local + value", context));
    BOOST_REQUIRE(context.getInterpreter().execute("created", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 8);
    BOOST_REQUIRE(context.getInterpreter().execute("created = \"forty-seven\"", context));
    BOOST_REQUIRE(context.getInterpreter().execute("created", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "forty-seven");
    BOOST_TEST(!context.getInterpreter().execute("unknown.name = 1", context));
    BOOST_TEST(context.getVariableValue("unknown.name") == nullptr);
    BOOST_REQUIRE(context.getInterpreter().execute("localText = \"first\"", context));
    BOOST_REQUIRE(context.getInterpreter().execute("localText = localText.toUpper", context));
    BOOST_TEST(context.getVariableValue("localText")->getStringValue() == "FIRST");
    BOOST_TEST(!context.getInterpreter().execute("localText = 1", context));
    context.setVariable("value", machine->getRuntime()->getIntegerType(), RuntimeValue{NomadInteger{1}});
    BOOST_CHECK_THROW(context.setVariable("local", machine->getRuntime()->getBooleanType(),
        RuntimeValue{true}), std::invalid_argument);
    BOOST_TEST(machine->getResult().getIntegerValue() == savedResult.getIntegerValue());
    BOOST_TEST(machine->getIntermediate().getIntegerValue() == savedIntermediate.getIntegerValue());
    BOOST_TEST(function->getVariableCount() == variableCount);
    BOOST_TEST(function->getParameterCount() == parameterCount);
    BOOST_TEST(function->getCaptureCount() == captures);
    BOOST_TEST(machine->getRuntime()->getInstructions().size() == instructionCount);
    std::vector<ScriptFunction*> functions;
    machine->getRuntime()->getFunctions(functions);
    BOOST_TEST(functions.size() == functionsBefore);
    BOOST_TEST(context.getInterpreter().execute("value + 2", context));
}

} // namespace

BOOST_AUTO_TEST_CASE(interpreter_contexts_borrow_the_supplied_instance_and_keep_independent_state) {
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    {
        InterpreterContext first(interpreter);
        InterpreterContext second(interpreter);
        BOOST_TEST(&first.getInterpreter() == &interpreter);
        BOOST_TEST(&second.getInterpreter() == &interpreter);
        BOOST_REQUIRE(first.getInterpreter().execute("value = 17", first));
        BOOST_REQUIRE(second.getInterpreter().execute("value = 31", second));
        BOOST_REQUIRE(first.getInterpreter().execute("value + 1", first));
        BOOST_TEST(first.getResult()->getValue().getIntegerValue() == 18);
        BOOST_TEST(second.getResult()->getValue().getIntegerValue() == 31);
        BOOST_TEST(first.getVariableValue("value")->getIntegerValue() == 17);
        BOOST_TEST(second.getVariableValue("value")->getIntegerValue() == 31);
    }
    InterpreterContext subsequent(interpreter);
    BOOST_TEST(&subsequent.getInterpreter() == &interpreter);
    BOOST_TEST(subsequent.getVariableType("value") == nullptr);
    BOOST_REQUIRE(interpreter.execute("value = 47", subsequent));
    BOOST_TEST(subsequent.getResult()->getValue().getIntegerValue() == 47);
}

BOOST_AUTO_TEST_CASE(interpreters_and_contexts_use_their_bound_virtual_machine) {
    Runtime runtime;
    auto firstMachine = runtime.createVirtualMachine();
    auto secondMachine = runtime.createVirtualMachine();
    const Interpreter firstInterpreter(*firstMachine);
    const Interpreter secondInterpreter(*secondMachine);
    InterpreterContext firstContext(firstInterpreter);
    InterpreterContext secondContext(secondInterpreter);
    BOOST_TEST(&firstInterpreter.getVirtualMachine() == firstMachine.get());
    BOOST_TEST(&secondInterpreter.getVirtualMachine() == secondMachine.get());
    BOOST_TEST(firstContext.getVirtualMachine() == firstMachine.get());
    BOOST_TEST(secondContext.getVirtualMachine() == secondMachine.get());
    BOOST_TEST(firstContext.getRuntime() == &runtime);
    BOOST_TEST(secondContext.getRuntime() == &runtime);
    VirtualMachine* calledMachine = nullptr;
    const auto functionId = runtime.registerNativeFunction("recordBoundVm", [&](VirtualMachine* active) {
        calledMachine = active;
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(functionId.isValid());
    static_cast<void>(firstInterpreter.runFunction(functionId, {}));
    BOOST_TEST(calledMachine == firstMachine.get());
    static_cast<void>(secondInterpreter.runFunction(functionId, {}));
    BOOST_TEST(calledMachine == secondMachine.get());
    BOOST_REQUIRE(firstInterpreter.execute("recordBoundVm", firstContext));
    BOOST_TEST(calledMachine == firstMachine.get());
    BOOST_REQUIRE(secondInterpreter.execute("recordBoundVm", secondContext));
    BOOST_TEST(calledMachine == secondMachine.get());
}

BOOST_AUTO_TEST_CASE(interpreter_typed_values_own_complete_nested_storage_and_keep_moves_stable) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("ValueEmpty");
    const auto leafId = runtime.registerStruct("ValueLeaf");
    const auto outerId = runtime.registerStruct("ValueOuter");
    const auto ownerId = runtime.registerClass("ValueOwner", ClassMode::Closed);
    const auto* empty = runtime.getTypeByName("ValueEmpty");
    const auto* leaf = runtime.getTypeByName("ValueLeaf");
    const auto* outer = runtime.getTypeByName("ValueOuter");
    BOOST_REQUIRE(runtime.registerProperty(leafId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(leafId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(leafId, "owner", runtime.getTypeByName("ValueOwner")).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "prefix", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "leaf", leaf).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "suffix", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "empty", empty).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(outerId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(ownerId);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        std::array<RuntimeValue, 5> source{};
        source[0].setStringValue("prefix");
        source[1].setIntegerValue(7);
        source[2].setStringValue("nested");
        source[3].setObjectValue(new NomadObject);
        source[4].setStringValue("suffix");
        {
            InterpreterValue value(outer, source);
            BOOST_REQUIRE_EQUAL(value.getStorage().size(), 5U);
            BOOST_TEST(value.getStorage().data() != source.data());
            BOOST_TEST(source[2].getObjectValue()->getReferenceCount() == 2U);
            BOOST_TEST(source[3].getObjectValue()->getReferenceCount() == 2U);
            InterpreterValue copied(value);
            BOOST_TEST(copied.getStorage().data() != value.getStorage().data());
            BOOST_TEST(source[2].getObjectValue()->getReferenceCount() == 3U);
            source[1].setIntegerValue(99);
            BOOST_TEST(copied.getStorage()[1].getIntegerValue() == 7);
            const auto* address = copied.getStorage().data();
            InterpreterValue moved(std::move(copied));
            BOOST_TEST(moved.getStorage().data() == address);
            BOOST_TEST(copied.getType() == nullptr);
            BOOST_TEST(copied.getStorage().empty());
            InterpreterValue assigned(runtime.getIntegerType(), RuntimeValue{NomadInteger{1}});
            assigned = moved;
            const auto* alias = &assigned;
            assigned = *alias;
            BOOST_TEST(assigned.getStorage()[4].getStringValue() == "suffix");
            InterpreterValue moveAssigned(runtime.getBooleanType(), RuntimeValue{true});
            moveAssigned = std::move(assigned);
            BOOST_TEST(assigned.getType() == nullptr);
            BOOST_TEST(moveAssigned.getStorage()[3].getObjectValue() == source[3].getObjectValue());
            auto* moveAlias = &moveAssigned;
            const auto* moveAddress = moveAssigned.getStorage().data();
            moveAssigned = std::move(*moveAlias);
            BOOST_TEST(moveAssigned.getStorage().data() == moveAddress);
            std::vector<InterpreterValue> relocated;
            relocated.push_back(std::move(moveAssigned));
            for (auto count = 0; count < 32; ++count) {
                relocated.emplace_back(runtime.getIntegerType(), RuntimeValue{NomadInteger{count}});
            }
            BOOST_TEST(relocated.front().getStorage().data() == moveAddress);
            BOOST_CHECK_THROW(static_cast<void>(value.getValue()), std::invalid_argument);
            BOOST_CHECK_THROW((InterpreterValue{outer, source[0]}), std::invalid_argument);
            BOOST_CHECK_THROW((InterpreterValue{outer, std::span<const RuntimeValue>{source}.first(4)}),
                std::invalid_argument);
            BOOST_CHECK_THROW((InterpreterValue{runtime.getIntegerType(), source}), std::invalid_argument);
            BOOST_CHECK_THROW((InterpreterValue{nullptr, std::span<const RuntimeValue>{}}), std::invalid_argument);
            InterpreterValue emptyValue(empty, std::span<const RuntimeValue>{});
            InterpreterValue emptyCopy(emptyValue);
            BOOST_TEST(emptyCopy.getType() == empty);
            BOOST_TEST(emptyCopy.getStorage().empty());
            BOOST_TEST(emptyCopy.getStorage().data() == nullptr);
            BOOST_TEST(!emptyCopy.getType()->isVoid());
            BOOST_CHECK_THROW(static_cast<void>(emptyCopy.getValue()), std::invalid_argument);
        }
        BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(source[2].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(source[3].getObjectValue()->getReferenceCount() == 1U);
        outer->freeValue(source.data());
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(interpreter_executes_whole_struct_reads_and_assignments_from_host_storage) {
    Runtime runtime;
    const auto pairId = runtime.registerStruct("ConsolePair");
    const auto emptyId = runtime.registerStruct("ConsoleEmpty");
    const auto* pair = runtime.getTypeByName("ConsolePair");
    const auto* empty = runtime.getTypeByName("ConsoleEmpty");
    BOOST_REQUIRE(runtime.registerProperty(pairId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "text", runtime.getStringType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(pairId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(emptyId);
    auto machine = runtime.createVirtualMachine();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        {
            const Interpreter contextEvaluator(*machine);
            InterpreterContext context(contextEvaluator);
            const Interpreter independentEvaluator(*machine);
            InterpreterContext independent(independentEvaluator);
            std::array<RuntimeValue, 2> source{};
            source[0].setIntegerValue(17);
            source[1].setStringValue("host value");
            context.setVariable("source", pair, source);
            pair->freeValue(source.data());
            const auto* root = context.getVariableStorage("source").data();
            context.setVariable("empty", empty, std::span<const RuntimeValue>{});
            BOOST_REQUIRE(context.getInterpreter().execute("source", context));
            BOOST_REQUIRE(context.getResult() != nullptr);
            BOOST_TEST(context.getResult()->getType() == pair);
            BOOST_TEST(context.getResult()->getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(context.getResult()->getStorage()[1].getStringValue() == "host value");
            context.setResult(*context.getResult());
            context.setResult(pair, context.getResult()->getStorage());
            const InterpreterValue snapshot(*context.getResult());
            BOOST_REQUIRE(context.getInterpreter().execute("copy = source\nsource = source\nemptyCopy = empty", context));
            BOOST_TEST(context.getVariableStorage("copy").data() != root);
            BOOST_TEST(context.getVariableStorage("copy")[0].getIntegerValue() == 17);
            BOOST_TEST(context.getVariableType("emptyCopy") == empty);
            BOOST_REQUIRE(context.getInterpreter().execute("emptyCopy", context));
            BOOST_TEST(context.getResult()->getType() == empty);
            BOOST_TEST(context.getResult()->getStorage().empty());
            BOOST_TEST(context.getResult()->getStorage().data() == nullptr);
            BOOST_CHECK_THROW(context.setVariable("empty", empty, RuntimeValue{}), std::invalid_argument);
            BOOST_CHECK_THROW(context.setVariable("copy", pair, std::span<const RuntimeValue>{}),
                std::invalid_argument);
            BOOST_TEST(context.getVariableStorage("copy")[1].getStringValue() == "host value");
            BOOST_CHECK_THROW(context.setVariable("copy", pair, RuntimeValue{}), std::invalid_argument);
            BOOST_CHECK_THROW(static_cast<void>(context.getVariableValue("copy")), std::invalid_argument);
            BOOST_CHECK_THROW(static_cast<void>(context.getVariableValueById(
                context.getCallable()->getVariableId("copy"))),
                std::invalid_argument);
            context.setVariable("copy", pair, context.getVariableStorage("copy"));
            BOOST_REQUIRE(context.getInterpreter().execute("source = 31", context));
            BOOST_TEST(context.getVariableValue("source")->getIntegerValue() == 31);
            BOOST_TEST(snapshot.getStorage()[1].getStringValue() == "host value");
            BOOST_TEST(context.getVariableStorage("copy")[0].getIntegerValue() == 17);
            BOOST_TEST(independent.getVariableType("copy") == nullptr);
            BOOST_TEST(independent.getVariableStorage("copy").empty());
            const auto variables = context.listVariables();
            BOOST_REQUIRE_EQUAL(variables.size(), 4U);
            BOOST_TEST(variables[0].name == "copy");
            BOOST_TEST(variables[0].value == "ConsolePair");
            BOOST_TEST(variables[1].value == "ConsoleEmpty");
            context.clearVariableValue("copy");
            BOOST_TEST(context.getVariableType("copy") == nullptr);
            BOOST_TEST(context.getVariableStorage("copy").empty());
            BOOST_TEST(!context.getInterpreter().execute("copy", context));
            BOOST_TEST(context.hasError());
            BOOST_TEST(context.getResult() == nullptr);
            context.setResult(pair, snapshot.getStorage());
            BOOST_CHECK_THROW(context.setResult(pair, std::span<const RuntimeValue>{}), std::invalid_argument);
            BOOST_TEST(context.getResult()->getType() == pair);
            context.setVoidResult();
            BOOST_TEST(context.getResult()->getStorage().empty());
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(frame_interpreter_copies_and_replaces_aggregate_parameters_and_locals_by_physical_offset) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto visits = 0;
    const auto inspectFrameId = runtime.registerNativeFunction(
        "inspectAggregateFrame",
        [&](VirtualMachine* machine) {
            ++visits;
            const auto* callable = machine->getCurrentCallable();
            BOOST_REQUIRE(callable != nullptr);
            const auto variableCount = callable->getVariableCount();
            const auto parameterCount = callable->getParameterCount();
            const auto instructionsBefore = runtime.getInstructions().size();
            const auto savedResult = machine->getResult();
            const auto savedIntermediate = machine->getIntermediate();
            const Interpreter contextEvaluator(*machine);
            InterpreterContext context(contextEvaluator, *callable);
            const auto* outer = runtime.getTypeByName("FrameOuter");
            const auto* empty = runtime.getTypeByName("FrameEmpty");
            const auto parameterId = callable->getParameterId("value");
            const auto tailId = callable->getParameterId("tail");
            BOOST_REQUIRE(parameterId != NOMAD_INVALID_ID);
            BOOST_REQUIRE(tailId != NOMAD_INVALID_ID);
            BOOST_TEST(callable->getParameterOffset(tailId) == 5U);
            const auto input = context.getVariableStorage("value");
            BOOST_REQUIRE_EQUAL(input.size(), 5U);
            BOOST_TEST(input[0].getIntegerValue() == 7);
            BOOST_TEST(input[1].getStringValue() == "nested");
            BOOST_TEST(input[3].getStringValue() == "outer");
            BOOST_TEST(context.getVariableValue("tail")->getIntegerValue() == 23);
            BOOST_TEST(context.getParameterStorage(tailId)[0].getIntegerValue() == 23);
            BOOST_TEST(context.getVariableType("empty") == empty);
            BOOST_TEST(context.getVariableStorage("empty").empty());
            BOOST_TEST(context.getVariableStorage("emptyLocal").empty());
            BOOST_REQUIRE(context.getInterpreter().execute("value", context));
            const InterpreterValue snapshot(*context.getResult());
            BOOST_TEST(snapshot.getStorage().data() != input.data());
            BOOST_REQUIRE(context.getInterpreter().execute("saved = value\nlocal = value\nvalue = local\nvalue = value\n"
                "tail = tail + 1\nemptyLocal = empty\nempty = emptyLocal", context));
            BOOST_TEST(context.getVariableStorage("saved").data() != input.data());
            BOOST_TEST(context.getVariableStorage("local")[1].getStringValue() == "nested");
            BOOST_REQUIRE(context.getInterpreter().execute("empty", context));
            BOOST_TEST(context.getResult()->getType() == empty);
            BOOST_TEST(context.getResult()->getStorage().empty());
            std::array<RuntimeValue, 5> replacement{};
            outer->copyValue(snapshot.getStorage().data(), replacement.data());
            replacement[0].setIntegerValue(41);
            context.setParameter(parameterId, replacement);
            context.setVariable("local", outer, replacement);
            outer->freeValue(replacement.data());
            BOOST_TEST(context.getVariableStorage("value")[0].getIntegerValue() == 41);
            BOOST_TEST(context.getVariableStorage("local")[0].getIntegerValue() == 41);
            BOOST_TEST(context.getVariableStorage("saved")[0].getIntegerValue() == 7);
            BOOST_TEST(snapshot.getStorage()[0].getIntegerValue() == 7);
            context.setParameter(parameterId, context.getParameterStorage(parameterId));
            context.setVariable("local", outer, context.getVariableStorage("local"));
            BOOST_CHECK_THROW(context.setParameter(parameterId, std::span<const RuntimeValue>{}),
                std::invalid_argument);
            BOOST_CHECK_THROW(context.setParameter(parameterId, RuntimeValue{}), std::invalid_argument);
            BOOST_TEST(!context.getInterpreter().execute("value = 1", context));
            BOOST_TEST(!context.getInterpreter().execute("local = 1", context));
            BOOST_TEST(context.getVariableStorage("local")[0].getIntegerValue() == 41);
            context.setVariable("tail", runtime.getIntegerType(), RuntimeValue{NomadInteger{29}});
            BOOST_REQUIRE(context.getInterpreter().execute("tail", context));
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 29);
            BOOST_TEST(machine->getResult().getIntegerValue() == savedResult.getIntegerValue());
            BOOST_TEST(machine->getIntermediate().getIntegerValue() == savedIntermediate.getIntegerValue());
            BOOST_TEST(callable->getVariableCount() == variableCount);
            BOOST_TEST(callable->getParameterCount() == parameterCount);
            BOOST_TEST(runtime.getInstructions().size() == instructionsBefore);
        },
        {},
        runtime.getVoidType(),
        ""
    );
    BOOST_REQUIRE(inspectFrameId.isValid());
    fixture.addFunction("main",
        "class FrameOwner\nnumber = 11\nend\n"
        "struct FrameEmpty\nend\n"
        "struct FrameLeaf\nnumber = 7\ntext = \"nested\"\nowner = new FrameOwner\nend\n"
        "struct FrameOuter\nleaf = new FrameLeaf\ntext = \"outer\"\nlast = 19\nend\n"
        "fun inspect value:FrameOuter empty:FrameEmpty tail:int\n"
        "prefix = \"prefix\"\nemptyLocal = new FrameEmpty\nlocal = new FrameOuter\n"
        "inspectAggregateFrame\n"
        "assert value.leaf.number == 41\nassert local.leaf.number == 41\n"
        "assert value.leaf.owner.number == 11\nassert value.last == 19\n"
        "assert local.text == \"outer\"\nassert prefix == \"prefix\"\n"
        "return tail\nend\n"
        "source = new FrameOuter\nresult = inspect source (new FrameEmpty) 23\n"
        "assert source.leaf.number == 7\nreturn result");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 29);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(visits == 3);
}

BOOST_AUTO_TEST_CASE(interpreter_executes_conditional_loops)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);
    const auto succeeded = context.getInterpreter().execute(
        "value = 0\n"
        "total = 0\n"
        "while value < 6\n"
        "    value = value + 1\n"
        "    if value == 2 continue\n"
        "    if value == 5 break\n"
        "    total = total + value\n"
        "end\n"
        "do\n"
        "    total = total + 1\n"
        "    continue\n"
        "end until true", context);
    const auto diagnostic = context.getError() ? *context.getError() : "Loop execution failed";
    BOOST_REQUIRE_MESSAGE(succeeded, diagnostic);
    BOOST_REQUIRE(context.getInterpreter().execute("total", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 9);
    BOOST_TEST(!context.getInterpreter().execute("break", context));
}

BOOST_AUTO_TEST_CASE(interpreter_detects_type_changes_across_loop_iterations)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);
    BOOST_TEST(!context.getInterpreter().execute(
        "value = 1\n"
        "while value > 0\n"
        "    value = \"changed\"\n"
        "end", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->find("changed type") != NomadString::npos);
    BOOST_REQUIRE(context.getInterpreter().execute("value = 2", context));
    BOOST_REQUIRE(context.getInterpreter().execute("value + 1", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 3);
}

BOOST_AUTO_TEST_CASE(frame_interpreter_requires_an_active_script_function)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    ScriptFunction function(CallableId::INVALID, "inactive", "inactive.nomad", "");
    const Interpreter interpreter(*machine);
    BOOST_CHECK_THROW((InterpreterContext{interpreter, function}), NomadBug);
}

BOOST_AUTO_TEST_CASE(interpreter_uses_named_source_text_and_compiled_source_batches)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const SourceBatch batch{{{"answer", {"untitled:answer", "return 42"}}}, {}};
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, batch);
    BOOST_REQUIRE(compiler->compileFunctions(&compilation));

    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    BOOST_REQUIRE(interpreter.execute(SourceText{"console:input", "value = answer"}, context));
    BOOST_REQUIRE(interpreter.execute(SourceText{"console:input", "value + 1"}, context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 43);

    for (const auto* text : {"missing", "\"unterminated", "1 +", "1\n2"}) {
        const SourceText source{"console:input", text};
        BOOST_TEST(!interpreter.execute(source, context));
        BOOST_REQUIRE(!context.getDiagnostics().empty());
        for (const auto& diagnostic : context.getDiagnostics()) {
            BOOST_TEST(diagnostic.sourceName == source.sourceName);
        }
    }
}

BOOST_AUTO_TEST_CASE(interpreter_reports_source_execution_failures_as_diagnostics)
{
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto failingFunction = runtime.registerNativeFunction("source.failValue", [](VirtualMachine*) {
        throw std::runtime_error("source value failure");
    }, {}, runtime.getIntegerType(), "");
    BOOST_REQUIRE(failingFunction.isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("source.failVoid", [](VirtualMachine*) {
        throw NomadException("source statement failure");
    }, {}, runtime.getVoidType(), "").isValid());
    fixture.addFunction("failingSourceScript", "return source.failValue");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    BOOST_REQUIRE(interpreter.execute("value = 17", context));
    machine->pushInteger(1234);
    machine->setIntegerResult(41);
    machine->setIntegerIntermediate(43);
    const std::array<std::pair<NomadString, NomadString>, 6> cases{{
        {"source.failValue", "source value failure"},
        {"failingSourceScript", "source value failure"},
        {"value = source.failValue", "source value failure"},
        {"source.failVoid", "source statement failure"},
        {"if true\nsource.failVoid\nend", "source statement failure"},
        {"$(\"{source.failValue}\")", "source value failure"}
    }};
    for (const auto& [text, message] : cases) {
        BOOST_TEST_CONTEXT(text) {
            BOOST_TEST(!interpreter.execute(SourceText{"console:failure", text}, context));
            BOOST_REQUIRE_EQUAL(context.getDiagnostics().size(), 1U);
            const auto& diagnostic = context.getDiagnostics()[0];
            BOOST_CHECK(diagnostic.severity == DiagnosticSeverity::Error);
            BOOST_TEST(diagnostic.sourceName == "console:failure");
            BOOST_TEST(diagnostic.message == message);
            BOOST_REQUIRE(context.getError() != nullptr);
            BOOST_TEST(*context.getError() == message);
            BOOST_TEST(context.getResult() == nullptr);
            BOOST_TEST(context.getVariableValue("value")->getIntegerValue() == 17);
            BOOST_TEST(machine->getIntegerResult() == 41);
            BOOST_TEST(machine->getIntegerIntermediate() == 43);
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            BOOST_REQUIRE(interpreter.execute("value + 1", context));
            BOOST_TEST(context.getDiagnostics().empty());
            BOOST_REQUIRE(context.getResult() != nullptr);
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 18);
        }
    }
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runFunction(failingFunction, {})), std::runtime_error);
    machine->pop1();
}

BOOST_AUTO_TEST_CASE(frame_interpreter_reads_live_parameters_locals_and_calls_on_active_vm)
{
    Runtime runtime;
    runtime.registerNativeFunction("test.native",
        [](VirtualMachine* machine) { machine->setStringResult(machine->getStringParameter(0)->getString()); },
        {defParameter("text", runtime.getStringType(), NomadParamDoc("Text"))},
        runtime.getStringType(), NomadDoc("Echo"));
    runtime.registerNativeFunction("test.fail",
        [](VirtualMachine* machine) { machine->fault("frame failure"); },
        {}, runtime.getBooleanType(), NomadDoc("Fail"));
    runtime.registerNativeFunction("test.void",
        [](VirtualMachine*) {}, {}, runtime.getVoidType(), NomadDoc("No value"));
    runtime.registerDynamicVariable("test.parameter",
        [](VirtualMachine*, const RuntimeValue&) {},
        [](VirtualMachine* machine, RuntimeValue& value) { value = machine->getParameter(0); },
        runtime.getIntegerType(), NomadDoc("Current frame parameter"));

    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"answer", {"answer.nomad", "return 3"}}}, {}});
    BOOST_REQUIRE(compiler->compileFunctions(&compilation));
    const auto parentId = runtime.registerScriptFunction("parent", "frame.nomad", "");
    auto* parent = runtime.getScriptFunction(parentId);
    parent->registerVariable("uncaptured", runtime.getIntegerType());
    const auto id = runtime.registerScriptFunction("frame", "frame.nomad", "");
    auto* function = runtime.getScriptFunction(id);
    function->setParentId(parentId);
    function->addParameter("value", runtime.getIntegerType(), "");
    function->addParameter("text", runtime.getStringType(), "");
    function->createCapture(VariableId{0}, "captured", runtime.getIntegerType());
    function->registerVariable("local", runtime.getIntegerType());
    function->registerVariable("localText", runtime.getStringType());
    function->setReturnType(runtime.getVoidType());
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    const auto initializeFrame = runtime.registerInstruction("test.initializeFrame", [](VirtualMachine* machine) {
        machine->setFunctionVariableValue(
            machine->getCurrentFunction()->getVariableId("local"), RuntimeValue{NomadInteger{2}});
        machine->setIntegerResult(77);
        machine->setIntegerIntermediate(88);
    }, NomadDoc("Initialize the test frame"), {});
    const auto evaluateFrame = runtime.registerInstruction("test.evaluateFrame", evaluateFrameExpressions,
        NomadDoc("Evaluate expressions in the test frame"), {});
    compiler->addOpCode(initializeFrame);
    compiler->addOpCode(evaluateFrame);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    const auto contextId = runtime.registerOrUpdateContextVariable("global.score", runtime.getIntegerType());
    runtime.setContextVariableValue(contextId, RuntimeValue{NomadInteger{5}});

    auto machine = runtime.createVirtualMachine();
    RuntimeValue text;
    text.setStringValue("hello");
#ifdef NOMAD_DEBUG
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
#endif
    machine->run(function, {RuntimeValue{NomadInteger{40}}, text}, {RuntimeValue{NomadInteger{6}}});
#ifdef NOMAD_DEBUG
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
#endif
    text.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(interpreter_context_stores_persistent_typed_variables)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    const RuntimeValue integerValue{NomadInteger{42}};
    context.setVariable("score", runtime.getIntegerType(), integerValue);

    const auto* variableValue = context.getVariableValue("score");
    BOOST_REQUIRE(variableValue != nullptr);
    BOOST_TEST(context.getVariableType("score") == runtime.getIntegerType());
    BOOST_TEST(variableValue->getIntegerValue() == 42);

    const RuntimeValue updatedValue{NomadInteger{99}};
    context.setVariable("score", runtime.getIntegerType(), updatedValue);
    BOOST_TEST(context.getVariableValue("score")->getIntegerValue() == 99);
}

BOOST_AUTO_TEST_CASE(interpreter_contexts_are_independent_and_borrow_the_callers_vm)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    {
        InterpreterContext first(interpreter);
        InterpreterContext second(interpreter);
        BOOST_TEST(first.getVirtualMachine() == machine.get());
        runtime.registerNativeFunction("test.sameVm", [&](VirtualMachine* calledMachine) {
            calledMachine->setBooleanResult(calledMachine == machine.get());
        }, {}, runtime.getBooleanType(), NomadDoc("Check VM identity"));
        machine->setIntegerResult(71);
        machine->setIntegerIntermediate(72);
        BOOST_REQUIRE(interpreter.execute("test.sameVm", first));
        BOOST_TEST(first.getResult()->getValue().getBooleanValue());
        BOOST_TEST(machine->getResult().getIntegerValue() == 71);
        BOOST_TEST(machine->getIntermediate().getIntegerValue() == 72);
        BOOST_REQUIRE(interpreter.execute("score = 10", first));
        BOOST_TEST(!interpreter.execute("score", second));
        BOOST_REQUIRE(interpreter.execute("score = \"second\"", second));
        BOOST_REQUIRE(interpreter.execute("score + 5", first));
        BOOST_TEST(first.getResult()->getValue().getIntegerValue() == 15);
        BOOST_TEST(second.getVariableType("score") == runtime.getStringType());
        BOOST_TEST(!interpreter.execute("unknown.name = 1", first));
        BOOST_TEST(first.getVariableValue("unknown.name") == nullptr);
    }
    InterpreterContext next(interpreter);
    BOOST_REQUIRE(interpreter.execute("2 + 3", next));
    BOOST_TEST(next.getResult()->getValue().getIntegerValue() == 5);
}

BOOST_AUTO_TEST_CASE(interpreter_context_stores_expression_and_void_results)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(context.getResult() == nullptr);

    context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{10}});
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 10);

    context.setVoidResult();
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType()->isVoid());

    context.clearResult();
    BOOST_TEST(context.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(interpreter_context_stores_and_clears_errors)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(!context.hasError());
    BOOST_TEST(context.getError() == nullptr);

    context.setError("Unknown variable 'score'");
    BOOST_TEST(context.hasError());
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown variable 'score'");

    context.setError("A later nested error");
    BOOST_TEST(*context.getError() == "Unknown variable 'score'");
    BOOST_REQUIRE_EQUAL(context.getDiagnostics().size(), 2);
    BOOST_TEST(context.getDiagnostics()[1].message == "A later nested error");

    context.clearError();
    BOOST_TEST(!context.hasError());
    BOOST_TEST(!context.hasWarning());
    BOOST_TEST(context.getError() == nullptr);

    context.setError("A later input error");
    BOOST_TEST(context.hasError());
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "A later input error");
}

BOOST_AUTO_TEST_CASE(interpreter_context_retains_multiple_diagnostics_and_first_error)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);
    context.addDiagnostic({DiagnosticSeverity::Warning, "warning", "test.nomad", 1, 2});
    BOOST_TEST(!context.hasError());
    BOOST_TEST(context.hasWarning());
    BOOST_TEST(context.getError() == nullptr);
    context.addDiagnostic({DiagnosticSeverity::Error, "first error", "test.nomad", 2, 3});
    context.addDiagnostic({DiagnosticSeverity::Error, "second error", "test.nomad", 3, 4});
    BOOST_REQUIRE_EQUAL(context.getDiagnostics().size(), 3);
    BOOST_TEST(context.getDiagnostics()[0].message == "warning");
    BOOST_TEST(context.getDiagnostics()[1].message == "first error");
    BOOST_TEST(context.getDiagnostics()[2].message == "second error");
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "first error");
    BOOST_TEST(context.hasError());
    BOOST_TEST(context.hasWarning());
    context.clearError();
    BOOST_TEST(context.getDiagnostics().empty());
    BOOST_TEST(!context.hasError());
    BOOST_TEST(!context.hasWarning());
    BOOST_TEST(!context.getInterpreter().execute("missingLeft + missingRight", context));
    BOOST_REQUIRE_EQUAL(context.getDiagnostics().size(), 2);
    BOOST_TEST(context.getDiagnostics()[0].message == "Unknown identifier: missingLeft");
    BOOST_TEST(context.getDiagnostics()[1].message == "Unknown identifier: missingRight");
    BOOST_REQUIRE(context.getInterpreter().execute("1 + 2", context));
    BOOST_TEST(context.getDiagnostics().empty());
    BOOST_TEST(!context.hasError());
}

BOOST_AUTO_TEST_CASE(interpreter_context_executes_single_line_assignments_and_expressions)
{
    Runtime runtime;
    registerSystemFunctions(&runtime);
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(context.getInterpreter().execute("score = 10", context));
    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE(context.getVariableValue("score") != nullptr);
    BOOST_TEST(context.getVariableValue("score")->getIntegerValue() == 10);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 10);

    BOOST_TEST(context.getInterpreter().execute("score = score + 5", context));
    BOOST_TEST(context.getInterpreter().execute("score", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 15);

    BOOST_TEST(context.getInterpreter().execute("math.floor 4.8", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getFloatType());
    BOOST_TEST(context.getResult()->getValue().getFloatValue() == 4.0f);
}

BOOST_AUTO_TEST_CASE(interpreter_evaluates_format_literal_arithmetic)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_REQUIRE(context.getInterpreter().execute("number = 4", context));
    const std::vector<std::pair<NomadString, NomadString>> cases{
        {R"($"{1+2}")", "3"},
        {R"(    $"{1+2}")", "3"},
        {R"($"literal")", "literal"},
        {R"($"value {number + 2}")", "value 6"},
        {R"($"nested {$"sum {1+2}"}")", "nested sum 3"},
        {R"($"{"hello".toUpper}")", "HELLO"},
        {R"($"line\n{1+2}")", "line\n3"},
    };
    for (const auto& [source, expected] : cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto succeeded = context.getInterpreter().execute(source, context);
            BOOST_REQUIRE_MESSAGE(succeeded, (context.getError() ? *context.getError() : source));
            BOOST_REQUIRE(context.getResult() != nullptr);
            BOOST_TEST(context.getResult()->getType() == runtime.getStringType());
            BOOST_TEST(context.getResult()->getValue().getStringValue() == expected);
        }
    }
    BOOST_REQUIRE(context.getInterpreter().execute(R"(text = $"{1+2}")", context));
    BOOST_REQUIRE(context.getInterpreter().execute("text", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "3");

    for (const auto* source : {"", "   ", "# comment"}) {
        BOOST_REQUIRE(context.getInterpreter().execute(source, context));
        BOOST_REQUIRE(context.getResult() != nullptr);
        BOOST_TEST(context.getResult()->getType()->isVoid());
    }
}

BOOST_AUTO_TEST_CASE(interpreter_reports_unknown_function_calls)
{
    Runtime runtime;
    registerSystemFunctions(&runtime);
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(!context.getInterpreter().execute("toInteger 1.2", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown function 'toInteger'");

    BOOST_TEST(!context.getInterpreter().execute("toInteger \"12\"", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown function 'toInteger'");

    BOOST_TEST(!context.getInterpreter().execute("result = toInteger 1.2", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown function 'toInteger'");

    BOOST_TEST(!context.getInterpreter().execute("missing + 1", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown identifier: missing");

    BOOST_TEST(context.getInterpreter().execute("toInt \"12\"", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 12);
}

BOOST_AUTO_TEST_CASE(interpreter_reads_and_writes_context_variables)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(context.getInterpreter().execute("global.score = 10", context));
    BOOST_TEST(!context.hasError());

    // The value must land in the runtime's global context, not in a console variable.
    const auto contextId = runtime.getContextId("global");
    BOOST_REQUIRE(contextId.isValid());
    auto* variableContext = runtime.getVariableContext(contextId);
    BOOST_REQUIRE(variableContext != nullptr);
    const auto variableId = variableContext->getVariableId("global.score");
    BOOST_REQUIRE(variableId.isValid());

    RuntimeValue stored;
    variableContext->getValue(variableId, stored);
    BOOST_TEST(stored.getIntegerValue() == 10);

    BOOST_TEST(context.getInterpreter().execute("global.score", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 10);

    BOOST_TEST(context.getInterpreter().execute("global.score = global.score + 5", context));
    BOOST_TEST(context.getInterpreter().execute("global.score", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 15);
}

BOOST_AUTO_TEST_CASE(interpreter_reassigns_string_context_variables)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    // Reassignment must release the replaced buffer and keep the new one alive.
    BOOST_TEST(context.getInterpreter().execute("global.name = \"first\"", context));
    BOOST_TEST(context.getInterpreter().execute("global.name = \"second\"", context));

    BOOST_TEST(context.getInterpreter().execute("global.name", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getStringType());
    BOOST_REQUIRE(context.getResult()->getValue().getStringValue() != nullptr);
    BOOST_TEST(NomadString(context.getResult()->getValue().getStringValue()) == "second");
}

BOOST_AUTO_TEST_CASE(interpreter_reads_and_writes_dynamic_variables)
{
    Runtime runtime;
    NomadInteger backingValue = 7;

    runtime.registerDynamicVariable(
        "test.value",
        [&backingValue](VirtualMachine*, const RuntimeValue& value) {
            backingValue = value.getIntegerValue();
        },
        [&backingValue](VirtualMachine*, RuntimeValue& value) {
            value.setIntegerValue(backingValue);
        },
        runtime.getIntegerType(),
        NomadDoc("Test dynamic variable.")
    );

    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(context.getInterpreter().execute("test.value", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 7);

    BOOST_TEST(context.getInterpreter().execute("test.value = 42", context));
    BOOST_TEST(!context.hasError());
    BOOST_TEST(backingValue == 42);

    BOOST_TEST(context.getInterpreter().execute("test.value + 1", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 43);
}

BOOST_AUTO_TEST_CASE(interpreter_reads_and_writes_string_dynamic_variables)
{
    Runtime runtime;
    NomadString backingValue = "initial";

    runtime.registerDynamicVariable(
        "test.title",
        [&backingValue](VirtualMachine*, const RuntimeValue& value) {
            backingValue = value.getStringValue();
        },
        [&backingValue](VirtualMachine*, RuntimeValue& value) {
            value.setStringValue(backingValue);
        },
        runtime.getStringType(),
        NomadDoc("Test string dynamic variable.")
    );

    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(context.getInterpreter().execute("test.title", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_REQUIRE(context.getResult()->getValue().getStringValue() != nullptr);
    BOOST_TEST(NomadString(context.getResult()->getValue().getStringValue()) == "initial");

    BOOST_TEST(context.getInterpreter().execute("test.title = \"updated\"", context));
    BOOST_TEST(!context.hasError());
    BOOST_TEST(backingValue == "updated");
}

BOOST_AUTO_TEST_CASE(interpreter_rejects_writing_read_only_dynamic_variables)
{
    Runtime runtime;

    runtime.registerDynamicVariable(
        "test.readOnly",
        nullptr,
        [](VirtualMachine*, RuntimeValue& value) {
            value.setIntegerValue(3);
        },
        runtime.getIntegerType(),
        NomadDoc("Read-only test dynamic variable.")
    );

    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(context.getInterpreter().execute("test.readOnly", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 3);

    BOOST_TEST(!context.getInterpreter().execute("test.readOnly = 9", context));
    BOOST_TEST(context.hasError());
}

BOOST_AUTO_TEST_CASE(interpreter_rejects_type_mismatched_dynamic_assignment)
{
    Runtime runtime;
    NomadInteger backingValue = 1;

    runtime.registerDynamicVariable(
        "test.count",
        [&backingValue](VirtualMachine*, const RuntimeValue& value) {
            backingValue = value.getIntegerValue();
        },
        [&backingValue](VirtualMachine*, RuntimeValue& value) {
            value.setIntegerValue(backingValue);
        },
        runtime.getIntegerType(),
        NomadDoc("Test dynamic variable.")
    );

    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(!context.getInterpreter().execute("test.count = \"text\"", context));
    BOOST_TEST(context.hasError());
    BOOST_TEST(backingValue == 1);
}

BOOST_AUTO_TEST_CASE(interpreter_context_executes_compiled_value_returning_functions)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compileContext(compiler.get());
    compiler->registerSources(&compileContext, {{{"increment", {"increment.nomad",
        "params value:int\nreturn value + 1"}}}, {}});
    const auto functionId = runtime.getFunctionId("increment");
    BOOST_REQUIRE(functionId.isValid());
    BOOST_REQUIRE(compiler->compileFunctions(&compileContext));

    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);
    BOOST_TEST(context.getInterpreter().execute("increment 41", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(interpreter_context_executes_nativeFunctions_with_hidden_source_arguments)
{
    Runtime runtime;
    registerSystemFunctions(&runtime);
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);
    NomadString fileName;
    NomadString functionName;
    NomadInteger lineNumber = 0;
    NomadString message;

    runtime.registerNativeFunction(
        "test.hiddenSource",
        [&](const VirtualMachine* virtualMachine) {
            fileName = virtualMachine->getStringParameter(0)->getString();
            functionName = virtualMachine->getStringParameter(1)->getString();
            lineNumber = virtualMachine->getIntegerParameter(2);
            message = virtualMachine->getStringParameter(3)->getString();
        },
        {
            defParameter("$file", runtime.getFileNameType(), ""),
            defParameter("$function", runtime.getFunctionNameType(), ""),
            defParameter("$line", runtime.getLineNumberType(), ""),
            defParameter("message", runtime.getStringType(), "")
        },
        runtime.getVoidType(),
        ""
    );

    BOOST_TEST(context.getInterpreter().execute("test.hiddenSource(\"test\")", context));
    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType()->isVoid());

    BOOST_TEST(context.getInterpreter().execute("test.hiddenSource(\"payload\")", context));
    BOOST_TEST(fileName == "<console>");
    BOOST_TEST(functionName == "<console>");
    BOOST_TEST(lineNumber == 1);
    BOOST_TEST(message == "payload");
    BOOST_REQUIRE(context.getInterpreter().execute(R"(test.hiddenSource $"{1+2}")", context));
    BOOST_TEST(lineNumber == 1);
    BOOST_TEST(message == "3");
}

BOOST_AUTO_TEST_CASE(interpreter_context_reports_line_errors_and_clears_them_on_next_execute)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(!context.getInterpreter().execute("if", context));
    BOOST_TEST(context.hasError());
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(!context.getError()->empty());
    BOOST_TEST(context.getResult() == nullptr);

    BOOST_TEST(context.getInterpreter().execute("40 + 2", context));
    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(interpreter_context_executes_multiline_assignments)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_REQUIRE(context.getInterpreter().execute("score = 10\nscore = score + 1", context));
    BOOST_REQUIRE(context.getVariableValue("score") != nullptr);
    BOOST_TEST(context.getVariableValue("score")->getIntegerValue() == 11);
}

BOOST_AUTO_TEST_CASE(interpreter_context_lists_variables_in_name_order)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    context.setVariable("zeta", runtime.getBooleanType(), RuntimeValue{true});
    RuntimeValue stringValue{NomadString{"ready"}};
    context.setVariable("alpha", runtime.getStringType(), stringValue);
    runtime.getStringType()->freeValue(&stringValue);

    const auto variables = context.listVariables();
    BOOST_REQUIRE_EQUAL(variables.size(), 2U);
    BOOST_TEST(variables[0].name == "alpha");
    BOOST_TEST(variables[0].type == runtime.getStringType());
    BOOST_TEST(variables[0].value == "ready");
    BOOST_TEST(variables[1].name == "zeta");
    BOOST_TEST(variables[1].value == "true");
}

BOOST_AUTO_TEST_CASE(interpreter_context_rejects_invalid_types)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_CHECK_THROW(context.setVariable("", runtime.getIntegerType(), RuntimeValue{NomadInteger{1}}),
                      std::invalid_argument);
    BOOST_CHECK_THROW(context.setVariable("missing-type", nullptr, RuntimeValue{NomadInteger{1}}),
                      std::invalid_argument);
    BOOST_CHECK_THROW(context.setVariable("void-value", runtime.getVoidType(), RuntimeValue{}),
                      std::invalid_argument);

    BOOST_TEST(context.getVariableValue("unknown") == nullptr);
    BOOST_TEST(context.getVariableType("unknown") == nullptr);
    BOOST_TEST(context.getFunction()->getVariableCount() == 0U);
}

BOOST_AUTO_TEST_CASE(interpreter_context_variables_may_change_type)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    context.setVariable("value", runtime.getIntegerType(), RuntimeValue{NomadInteger{1}});
    context.setVariable("value", runtime.getFloatType(), RuntimeValue{NomadFloat{2.5f}});

    const auto* value = context.getVariableValue("value");
    BOOST_REQUIRE(value != nullptr);
    BOOST_TEST(context.getVariableType("value") == runtime.getFloatType());
    BOOST_TEST(value->getFloatValue() == 2.5f);

    const auto* function = context.getFunction();
    const auto variableId = function->getVariableId("value");
    BOOST_REQUIRE(variableId.isValid());
    BOOST_TEST(function->getVariableType(variableId) == runtime.getFloatType());
    BOOST_TEST(function->getVariableCount() == 1U);
}

BOOST_AUTO_TEST_CASE(interpreter_context_changes_variable_type_through_execute)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_REQUIRE(context.getInterpreter().execute("score = 10", context));
    BOOST_REQUIRE(context.getInterpreter().execute("score = \"ten\"", context));

    const auto* score = context.getVariableValue("score");
    BOOST_REQUIRE(score != nullptr);
    BOOST_TEST(context.getVariableType("score") == runtime.getStringType());
    BOOST_TEST(score->getStringValue() == "ten");
}

BOOST_AUTO_TEST_CASE(interpreter_context_defines_variables_in_console_function)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_REQUIRE(context.getInterpreter().execute("first = 1", context));
    BOOST_REQUIRE(context.getInterpreter().execute("second = true", context));

    const auto* function = context.getFunction();
    BOOST_REQUIRE(function != nullptr);
    BOOST_TEST(function->getId().isInvalid());
    BOOST_TEST(runtime.getFunctionId(function->getName()).isInvalid());
    BOOST_TEST(function->getVariableCount() == 2U);
    BOOST_TEST(function->getVariableId("first") == VariableId{0});
    BOOST_TEST(function->getVariableId("second") == VariableId{1});
    BOOST_TEST(function->getVariableType(VariableId{1}) == runtime.getBooleanType());
}

BOOST_AUTO_TEST_CASE(interpreter_context_failed_line_does_not_assign_variables)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_REQUIRE(context.getInterpreter().execute("kept = 1", context));
    BOOST_TEST(!context.getInterpreter().execute("broken = missing + 1", context));

    BOOST_TEST(context.getVariableValue("broken") == nullptr);
    BOOST_TEST(context.getVariableValue("kept")->getIntegerValue() == 1);

    const auto variables = context.listVariables();
    BOOST_REQUIRE_EQUAL(variables.size(), 1U);
    BOOST_TEST(variables[0].name == "kept");
}

BOOST_AUTO_TEST_CASE(interpreter_context_reports_resolve_errors_before_evaluation)
{
    Runtime runtime;
    NomadInteger calledValue = 0;
    runtime.registerNativeFunction(
        "remember",
        [&calledValue](VirtualMachine* interpreter) {
            calledValue = interpreter->getIntegerParameter(0);
        },
        {
            defParameter("value", runtime.getIntegerType(), NomadParamDoc("Value to remember"))
        },
        runtime.getVoidType(),
        NomadDoc("Remember an integer")
    );
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(!context.getInterpreter().execute("missing + 1", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown identifier: missing");

    BOOST_TEST(!context.getInterpreter().execute("1 + true", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->starts_with("Invalid binary operator '+'"));

    BOOST_TEST(!context.getInterpreter().execute("pi = 3.0", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Cannot assign value to 'pi' in the interpreter");

    BOOST_TEST(!context.getInterpreter().execute("remember true", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->starts_with("Argument type mismatch."));
    BOOST_TEST(calledValue == 0);

    BOOST_TEST(!context.getInterpreter().execute("value = remember 1", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->find("does not return a value") != NomadString::npos);
    BOOST_TEST(calledValue == 0);
    BOOST_TEST(context.getVariableValue("value") == nullptr);
}
