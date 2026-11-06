#include <LanguageTestFixture.hpp>

#include <nomad/compiler/Compiler.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/InterpreterCallFrame.hpp>
#include <nomad/script/InterpreterValue.hpp>
#include <nomad/script/NativeCallable.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace nomad;

namespace {

void addAggregateCallSources(nomad::test::LanguageTestFixture& fixture) {
    fixture.addFunction("callTypes",
        "class CallOwner\nnumber = 5\nend\n"
        "struct CallEmpty\ninit\nrecordEmptyCall\nend\nend\n"
        "struct CallLeaf\nnumber = 7\ntext = \"default\"\nowner = new CallOwner\n"
        "init number:int text:string\nthis.number = number\nthis.text = text\nend\nend\n"
        "struct CallOuter\nleaf = new CallLeaf 9 \"nested\"\nlabel = \"outer\"\nempty = new CallEmpty\n"
        "init leaf:CallLeaf\nthis.leaf = leaf\nleaf.number = 99\nend\nend\n"
        "class CallHolder\nvalue = new CallOuter (new CallLeaf 0 \"holder\")\n"
        "init value:CallOuter\nthis.value = value\nvalue.leaf.number = 88\nend\nend\n"
        "return 0");
    fixture.addFunction("copyAggregate",
        "params value:CallOuter empty:CallEmpty tail:int\n"
        "value.leaf.number = tail\nreturn value");
    fixture.addFunction("readAggregateText", "params value:CallOuter\nreturn value.leaf.text");
    fixture.addFunction("makeAggregate", "params number:int\nreturn new CallOuter (new CallLeaf number \"created\")");
    fixture.addFunction("copyCallEmpty", "params value:CallEmpty\nreturn value");
    fixture.addFunction("consumeAggregate",
        "params value:CallOuter empty:CallEmpty tail:int\n"
        "assert value.leaf.number == tail\nvalue.leaf.number = 100");
}

class FailingCallCopyType final : public ClassType {
public:
    FailingCallCopyType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        FailingCallCopyType*& descriptor
    ):
        ClassType(runtime, id, std::move(name), mode)
    {
        descriptor = this;
    }

    void initValue(RuntimeValue* value) const override {
        if (failInitialization) {
            value->setStringValue("partial initialization");
            throw NomadException("Call initialization failed");
        }
        ClassType::initValue(value);
    }

    void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override {
        ClassType::copyValue(source, destination);
        if (++copyCount == failCopyAt) {
            throw NomadException("Call copy failed");
        }
    }

    mutable NomadIndex copyCount = 0;
    NomadIndex failCopyAt = 0;
    bool failInitialization = false;
};

} // namespace

BOOST_AUTO_TEST_SUITE(interpreter_struct_calls)

BOOST_AUTO_TEST_CASE(script_construction_and_calls_preserve_nested_value_ownership) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto emptyCalls = 0;
    const auto recordId = runtime.registerNativeFunction("recordEmptyCall", [&](VirtualMachine*) {
        ++emptyCalls;
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(recordId.isValid());
    addAggregateCallSources(fixture);
    fixture.addFunction("main",
        "source = new CallOuter (new CallLeaf 17 \"source\")\n"
        "copy = copyAggregate source (new CallEmpty) 41\n"
        "assert source.leaf.number == 17\nassert copy.leaf.number == 41\n"
        "assert (readAggregateText copy) == \"source\"\n"
        "holder = new CallHolder source\nassert holder.value.leaf.number == 17\n"
        "assert source.leaf.number == 17\n"
        "empty = copyCallEmpty (new CallEmpty)\n"
        "consumeAggregate source empty 17\nreturn copy.leaf.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto* outer = runtime.getTypeByName("CallOuter");
    const auto* empty = runtime.getTypeByName("CallEmpty");
    const auto instructionsBefore = runtime.getInstructions().size();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    auto machine = runtime.createVirtualMachine();
    for (auto index = 0; index < 3; ++index) {
        {
            RuntimeValue savedResult{NomadString{"saved result"}};
            RuntimeValue savedIntermediate{NomadString{"saved intermediate"}};
            machine->setResult(savedResult);
            machine->setIntermediate(savedIntermediate);
            machine->pushInteger(1234);
            const Interpreter contextEvaluator;
            InterpreterContext context(contextEvaluator, *machine);
            const auto construct = context.getInterpreter().execute("source = new CallOuter (new CallLeaf 17 \"source\")", context);
            BOOST_REQUIRE_MESSAGE(construct, (context.getError() ? *context.getError() : ""));
            const auto source = context.getVariableStorage("source");
            BOOST_REQUIRE_EQUAL(source.size(), 4U);
            BOOST_TEST(source[0].getIntegerValue() == 17);
            BOOST_TEST(source[1].getStringValue() == "source");
            BOOST_TEST(source[3].getStringValue() == "outer");
            const auto copied = context.getInterpreter().execute("copy = copyAggregate source (new CallEmpty) 41", context);
            BOOST_REQUIRE_MESSAGE(copied, (context.getError() ? *context.getError() : ""));
            const auto copy = context.getVariableStorage("copy");
            BOOST_TEST(copy.data() != source.data());
            BOOST_TEST(copy[0].getIntegerValue() == 41);
            BOOST_TEST(source[0].getIntegerValue() == 17);
            BOOST_TEST(copy[2].getObjectValue() == source[2].getObjectValue());
            BOOST_REQUIRE(context.getInterpreter().evaluateExpression("readAggregateText copy", context));
            const InterpreterValue text(*context.getResult());
            BOOST_TEST(text.getValue().getStringValue() == "source");
            BOOST_REQUIRE(context.getInterpreter().execute("holder = new CallHolder source", context));
            const auto* holder = dynamic_cast<const ScriptObject*>(context.getVariableValue("holder")->getObjectValue());
            BOOST_REQUIRE(holder != nullptr);
            BOOST_TEST(holder->getFieldStorage(0)[0].getIntegerValue() == 17);
            BOOST_TEST(source[0].getIntegerValue() == 17);
            BOOST_REQUIRE(context.getInterpreter().evaluateExpression("makeAggregate 23", context));
            BOOST_TEST(context.getResult()->getType() == outer);
            BOOST_TEST(context.getResult()->getStorage()[0].getIntegerValue() == 23);
            BOOST_TEST(context.getResult()->getStorage()[1].getStringValue() == "created");
            const InterpreterValue snapshot(*context.getResult());
            const auto emptyBefore = emptyCalls;
            BOOST_REQUIRE(context.getInterpreter().evaluateExpression("copyCallEmpty (new CallEmpty)", context));
            BOOST_TEST(emptyCalls == emptyBefore + 1);
            BOOST_TEST(context.getResult()->getType() == empty);
            BOOST_TEST(context.getResult()->getStorage().empty());
            BOOST_TEST(context.getResult()->getStorage().data() == nullptr);
            BOOST_REQUIRE(context.getInterpreter().execute(
                "copyAggregate source (new CallEmpty) 90\nconsumeAggregate source (new CallEmpty) 17", context));
            BOOST_TEST(context.getResult()->getType()->isVoid());
            BOOST_TEST(source[0].getIntegerValue() == 17);
            context.clearVariableValue("source");
            context.clearVariableValue("copy");
            context.clearVariableValue("holder");
            BOOST_TEST(text.getValue().getStringValue() == "source");
            BOOST_TEST(snapshot.getStorage()[1].getStringValue() == "created");
            BOOST_TEST(machine->getResult().getObjectValue() == savedResult.getObjectValue());
            BOOST_TEST(machine->getIntermediate().getObjectValue() == savedIntermediate.getObjectValue());
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            machine->pop1();
            machine->setResult(RuntimeValue{});
            machine->setIntermediate(RuntimeValue{});
            savedResult.freeObjectValue();
            savedIntermediate.freeObjectValue();
        }
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 41);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(runtime.getInstructions().size() == instructionsBefore);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(native_aggregate_calls_use_physical_offsets_and_allow_nested_interpreter_reentry) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto valueId = runtime.registerStruct("NativeCallValue");
    const auto emptyId = runtime.registerStruct("NativeCallEmpty");
    const auto* valueType = runtime.getTypeByName("NativeCallValue");
    const auto* emptyType = runtime.getTypeByName("NativeCallEmpty");
    BOOST_REQUIRE(runtime.registerProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    runtime.finalizeCompositeFieldLayout(emptyId);
    auto emptyCalls = 0;
    const auto emptyConstructor = runtime.registerNativeStructConstructor(emptyId, {
        .parameters = {},
        .function = [&](VirtualMachine*) { ++emptyCalls; }
    });
    const auto constructor = runtime.registerNativeStructConstructor(valueId, {
        .parameters = {
            {runtime.getIntegerType()->getId(), "number", ""},
            {runtime.getStringType()->getId(), "text", ""}
        },
        .function = [](VirtualMachine* active) {
            active->getStorageParameter(2, 0) = active->getParameter(0);
            active->getStorageParameter(2, 1).freeObjectValue();
            active->getStorageParameter(2, 1).copyObjectValue(active->getParameter(1));
        }
    });
    BOOST_REQUIRE(emptyConstructor.isValid() && constructor.isValid());
    const auto scoreId = runtime.registerNativeFunction("nativeCallScore", [](VirtualMachine* active) {
        active->setIntegerResult(active->getParameterStorage(0, 2)[0].getIntegerValue());
    }, {defParameter("value", valueType, "")}, runtime.getIntegerType(), "");
    BOOST_REQUIRE(scoreId.isValid());
    auto reentries = 0;
    const auto transformId = runtime.registerNativeFunction("nativeCallTransform", [&](VirtualMachine* active) {
        const auto source = active->getParameterStorage(0, 2);
        auto* destination = active->getParameter(3).getStorageAddress();
        BOOST_TEST(destination != source.data());
        const auto tail = active->getIntegerParameter(2);
        const Interpreter nestedEvaluator;
        InterpreterContext nested(nestedEvaluator, *active);
        nested.setVariable("value", valueType, source);
        const auto evaluated = nested.getInterpreter().evaluateExpression("nativeCallScore value", nested);
        BOOST_REQUIRE_MESSAGE(evaluated, (nested.getError() ? *nested.getError() : ""));
        BOOST_TEST(nested.getResult()->getValue().getIntegerValue() == source[0].getIntegerValue());
        BOOST_TEST(active->getIntegerParameter(2) == tail);
        valueType->copyValue(source.data(), destination);
        destination[0].setIntegerValue(tail);
        active->getParameterStorage(0, 2)[0].setIntegerValue(99);
        ++reentries;
    }, {defParameter("value", valueType, ""), defParameter("empty", emptyType, ""),
        defParameter("tail", runtime.getIntegerType(), "")}, valueType, "");
    BOOST_REQUIRE(transformId.isValid());
    auto emptyReturns = 0;
    const auto emptyFactory = runtime.registerNativeFunction("nativeCallEmpty", [&](VirtualMachine* active) {
        BOOST_TEST(active->getIntegerParameter(0) == 7);
        ++emptyReturns;
    }, {defParameter("number", runtime.getIntegerType(), "")}, emptyType, "");
    BOOST_REQUIRE(emptyFactory.isValid());
    fixture.addFunction("forwardNativeAggregate",
        "params value:NativeCallValue empty:NativeCallEmpty tail:int\n"
        "return nativeCallTransform value empty tail");
    fixture.addFunction("main",
        "source = new NativeCallValue 17 \"native\"\n"
        "copy = forwardNativeAggregate source (new NativeCallEmpty) 51\n"
        "assert source.number == 17\nassert copy.number == 51\n"
        "assert copy.text == \"native\"\nempty = nativeCallEmpty 7\nreturn copy.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    auto machine = runtime.createVirtualMachine();
    for (auto index = 0; index < 3; ++index) {
        {
            const Interpreter contextEvaluator;
            InterpreterContext context(contextEvaluator, *machine);
            BOOST_REQUIRE(context.getInterpreter().execute("source = new NativeCallValue 17 \"native\"", context));
            const auto source = context.getVariableStorage("source");
            BOOST_REQUIRE(context.getInterpreter().execute(
                "copy = nativeCallTransform source (new NativeCallEmpty) 41", context));
            BOOST_TEST(source[0].getIntegerValue() == 17);
            BOOST_TEST(context.getVariableStorage("copy")[0].getIntegerValue() == 41);
            BOOST_TEST(context.getVariableStorage("copy")[1].getStringValue() == "native");
            BOOST_REQUIRE(context.getInterpreter().evaluateExpression(
                "forwardNativeAggregate source (new NativeCallEmpty) 51", context));
            BOOST_TEST(context.getResult()->getStorage()[0].getIntegerValue() == 51);
            BOOST_TEST(source[0].getIntegerValue() == 17);
            const auto callsBefore = emptyReturns;
            BOOST_REQUIRE(context.getInterpreter().evaluateExpression("nativeCallEmpty 7", context));
            BOOST_TEST(emptyReturns == callsBefore + 1);
            BOOST_TEST(context.getResult()->getType() == emptyType);
            BOOST_TEST(context.getResult()->getStorage().data() == nullptr);
            BOOST_REQUIRE(context.getInterpreter().execute(
                "nativeCallTransform source (new NativeCallEmpty) 61\nnativeCallEmpty 7", context));
            BOOST_TEST(context.getResult()->getType()->isVoid());
            BOOST_TEST(source[0].getIntegerValue() == 17);
        }
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 51);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(reentries == 12);
    BOOST_TEST(emptyCalls == 12);
    BOOST_TEST(emptyReturns == 9);
}

BOOST_AUTO_TEST_CASE(shared_argument_evaluation_matches_compiled_right_to_left_order) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<NomadInteger> order;
    const auto noteId = runtime.registerNativeFunction("callNote", [&](VirtualMachine* active) {
        const auto number = active->getIntegerParameter(0);
        order.push_back(number);
        active->setIntegerResult(number);
    }, {defParameter("number", runtime.getIntegerType(), "")}, runtime.getIntegerType(), "");
    const auto pairId = runtime.registerNativeFunction("nativeCallPair", [&](VirtualMachine* active) {
        order.push_back(3);
        active->setIntegerResult(active->getIntegerParameter(0) * 10 + active->getIntegerParameter(1));
    }, {defParameter("first", runtime.getIntegerType(), ""), defParameter("second", runtime.getIntegerType(), "")},
        runtime.getIntegerType(), "");
    BOOST_REQUIRE(noteId.isValid() && pairId.isValid());
    const auto boxId = runtime.registerClass("CallOrderBox", ClassMode::Closed);
    const auto* boxType = runtime.getTypeByName("CallOrderBox");
    const auto makeBoxId = runtime.registerNativeFunction("makeCallOrderBox", [&](VirtualMachine* active) {
        order.push_back(0);
        active->getResult().setObjectValue(new NomadObject());
    }, {}, boxType, "");
    const auto combineId = runtime.addMethod(boxId, {
        .name = "combine",
        .parameters = {
            {runtime.getIntegerType()->getId(), "first", ""},
            {runtime.getIntegerType()->getId(), "second", ""}
        },
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [&](VirtualMachine* active) {
            BOOST_TEST(active->getThis() != nullptr);
            order.push_back(3);
            active->setIntegerResult(active->getIntegerParameter(0) * 10 + active->getIntegerParameter(1));
        }
    });
    BOOST_REQUIRE(makeBoxId.isValid() && combineId.isValid());
    fixture.addFunction("scriptCallPair",
        "params first:int second:int\ncallNote 3\nreturn first * 10 + second");
    fixture.addFunction("types",
        "struct OrderedCallValue\nnumber = 0\n"
        "init first:int second:int\nthis.number = first * 10 + second\ncallNote 3\nend\nend\nreturn 0");
    fixture.addFunction("main",
        "first = nativeCallPair (callNote 1) (callNote 2)\n"
        "second = scriptCallPair (callNote 1) (callNote 2)\n"
        "third = new OrderedCallValue (callNote 1) (callNote 2)\n"
        "fourth = (makeCallOrderBox).combine (callNote 1) (callNote 2)\n"
        "return first + second + third.number + fourth");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator;
    InterpreterContext context(contextEvaluator, *machine);
    for (const auto* source : {
        "nativeCallPair (callNote 1) (callNote 2)",
        "scriptCallPair (callNote 1) (callNote 2)",
        "new OrderedCallValue (callNote 1) (callNote 2)"
    }) {
        order.clear();
        const auto evaluated = context.getInterpreter().evaluateExpression(source, context);
        BOOST_REQUIRE_MESSAGE(evaluated, (context.getError() ? *context.getError() : ""));
        BOOST_REQUIRE_EQUAL(order.size(), 3U);
        BOOST_TEST(order[0] == 2);
        BOOST_TEST(order[1] == 1);
        BOOST_TEST(order[2] == 3);
    }
    order.clear();
    BOOST_REQUIRE(context.getInterpreter().evaluateExpression(
        "(makeCallOrderBox).combine (callNote 1) (callNote 2)", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 12);
    BOOST_REQUIRE_EQUAL(order.size(), 4U);
    BOOST_TEST(order[0] == 0);
    BOOST_TEST(order[1] == 2);
    BOOST_TEST(order[2] == 1);
    BOOST_TEST(order[3] == 3);
    order.clear();
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.integerValue == 48);
    BOOST_REQUIRE_EQUAL(order.size(), 13U);
    for (auto offset = NomadIndex{0}; offset < 9; offset += 3) {
        BOOST_TEST(order[offset] == 2);
        BOOST_TEST(order[offset + 1] == 1);
        BOOST_TEST(order[offset + 2] == 3);
    }
    BOOST_TEST(order[9] == 0);
    BOOST_TEST(order[10] == 2);
    BOOST_TEST(order[11] == 1);
    BOOST_TEST(order[12] == 3);
}

BOOST_AUTO_TEST_CASE(aggregate_calls_preserve_active_method_frames_during_native_inspection) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto emptyId = runtime.registerNativeFunction(
        "recordEmptyCall", [](VirtualMachine*) {}, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(emptyId.isValid());
    auto visits = 0;
    const auto inspectId = runtime.registerNativeFunction("inspectPreparedAggregateCalls", [&](VirtualMachine* active) {
        const auto* callable = active->getCurrentCallable();
        BOOST_REQUIRE(callable != nullptr);
        BOOST_TEST(callable->getClassType() == runtime.getTypeByName("CallFrameHost"));
        const auto variableCount = callable->getVariableCount();
        const auto parameterCount = callable->getParameterCount();
        const auto instructionCount = runtime.getInstructions().size();
        const auto instructionIndex = active->getInstructionIndex();
        const auto savedResult = active->getResult();
        const auto savedIntermediate = active->getIntermediate();
        const Interpreter contextEvaluator;
        InterpreterContext context(contextEvaluator, *active, *callable);
        BOOST_TEST(context.getFunction() == nullptr);
        const auto source = context.getVariableStorage("value");
        BOOST_REQUIRE_EQUAL(source.size(), 4U);
        BOOST_TEST(source[0].getIntegerValue() == 17);
        const auto copied = context.getInterpreter().execute("local = copyAggregate value empty 71", context);
        BOOST_REQUIRE_MESSAGE(copied, (context.getError() ? *context.getError() : ""));
        BOOST_TEST(context.getVariableStorage("local")[0].getIntegerValue() == 71);
        BOOST_TEST(context.getVariableStorage("value")[0].getIntegerValue() == 17);
        BOOST_REQUIRE(context.getInterpreter().evaluateExpression("readAggregateText local", context));
        BOOST_TEST(context.getResult()->getValue().getStringValue() == "frame");
        BOOST_REQUIRE(context.getInterpreter().execute("created = makeAggregate tail", context));
        BOOST_TEST(context.getVariableStorage("created")[0].getIntegerValue() == 23);
        BOOST_TEST(context.getVariableValue("tail")->getIntegerValue() == 23);
        BOOST_TEST(context.getVariableValue("prefix")->getStringValue() == "prefix");
        BOOST_TEST(active->getCurrentCallable() == callable);
        BOOST_TEST(active->getInstructionIndex() == instructionIndex);
        BOOST_TEST(active->getResult().getIntegerValue() == savedResult.getIntegerValue());
        BOOST_TEST(active->getIntermediate().getIntegerValue() == savedIntermediate.getIntegerValue());
        BOOST_TEST(callable->getVariableCount() == variableCount);
        BOOST_TEST(callable->getParameterCount() == parameterCount);
        BOOST_TEST(runtime.getInstructions().size() == instructionCount);
        ++visits;
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(inspectId.isValid());
    addAggregateCallSources(fixture);
    fixture.addFunction("frameCallTypes",
        "class CallFrameHost\nmarker = 5\n"
        "fun inspect value:CallOuter empty:CallEmpty tail:int\n"
        "prefix = \"prefix\"\nlocal = value\ninspectPreparedAggregateCalls\n"
        "assert value.leaf.number == 17\nassert local.leaf.number == 71\n"
        "return local.leaf.number + tail + this.marker\nend\nend\nreturn 0");
    fixture.addFunction("main",
        "source = new CallOuter (new CallLeaf 17 \"frame\")\n"
        "host = new CallFrameHost\nresult = host.inspect source (new CallEmpty) 23\n"
        "assert source.leaf.number == 17\nreturn result");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 99);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(visits == 3);
}

BOOST_AUTO_TEST_CASE(raw_storage_push_checks_full_width_before_mutating_the_stack) {
    Runtime runtime;
    VirtualMachine machine(&runtime, 4);
    machine.pushInteger(17);
    const std::array<RuntimeValue, 3> overflow{
        RuntimeValue{NomadInteger{19}}, RuntimeValue{NomadInteger{23}}, RuntimeValue{NomadInteger{29}}
    };
    BOOST_CHECK_THROW(machine.pushStorage(overflow), VirtualMachineException);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 17);
    machine.pushStorage(std::span<const RuntimeValue>{overflow}.first(2));
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 23);
    BOOST_TEST(machine.peekStack(1).getIntegerValue() == 19);
    machine.pushStorage({});
    machine.pop2();
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 17);
    machine.pop1();
}

BOOST_AUTO_TEST_CASE(partial_call_copies_and_initialization_failures_release_owned_storage) {
    Runtime runtime;
    FailingCallCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingCallCopyType>("FailingCallField", ClassMode::Closed, field);
    const auto valueId = runtime.registerStruct("FailingCallValue");
    const auto* valueType = runtime.getTypeByName("FailingCallValue");
    BOOST_REQUIRE(fieldId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "first", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "second", field).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    auto calls = 0;
    const auto consumeId = runtime.registerNativeFunction("consumeFailingCall", [&](VirtualMachine*) {
        ++calls;
    }, {defParameter("value", valueType, "")}, runtime.getVoidType(), "");
    const auto makeId = runtime.registerNativeFunction("makeFailingCall", [](VirtualMachine* active) {
        auto* destination = active->getParameter(0).getStorageAddress();
        destination[0].setStringValue("first result");
        destination[1].setStringValue("second result");
    }, {}, valueType, "");
    const auto constructorId = runtime.registerNativeStructConstructor(valueId, {
        .parameters = {},
        .function = [&](VirtualMachine*) { ++calls; }
    });
    BOOST_REQUIRE(consumeId.isValid() && makeId.isValid() && constructorId.isValid());
    auto machine = runtime.createVirtualMachine();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        {
            const Interpreter contextEvaluator;
            InterpreterContext context(contextEvaluator, *machine);
            machine->pushInteger(1234);
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            std::array<RuntimeValue, 2> source{};
            source[0].setStringValue("first source");
            source[1].setStringValue("second source");
            const InterpreterValue argument(valueType, source);
            valueType->freeValue(source.data());
            const auto live = RuntimeValue::getLiveStringCount();
            field->copyCount = 0;
            field->failCopyAt = 2;
            BOOST_CHECK_THROW(static_cast<void>(Interpreter{}.callFunction(
                *machine, consumeId, std::span<const InterpreterValue>{&argument, 1})), NomadException);
            BOOST_TEST(argument.getStorage()[0].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(argument.getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{19}});
            field->copyCount = 0;
            BOOST_CHECK_THROW(static_cast<void>(Interpreter{}.callFunction(*machine, makeId, {})), NomadException);
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 19);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            field->failCopyAt = 0;
            field->failInitialization = true;
            BOOST_CHECK_THROW(static_cast<void>(Interpreter{}.construct(*machine, constructorId, {})), NomadException);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            field->failInitialization = false;
            const auto result = Interpreter{}.callFunction(
                *machine, consumeId, std::span<const InterpreterValue>{&argument, 1});
            BOOST_TEST(result.getType()->isVoid());
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 19);
            BOOST_TEST(machine->getIntegerResult() == 41);
            BOOST_TEST(machine->getIntegerIntermediate() == 43);
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            machine->pop1();
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(calls == 3);
}

BOOST_AUTO_TEST_CASE(host_script_calls_and_construction_return_independent_values_without_a_context) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto recordId = runtime.registerNativeFunction("recordEmptyCall", [](VirtualMachine*) {},
        {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(recordId.isValid());
    addAggregateCallSources(fixture);
    fixture.addFunction("hostScalar", "params value:int\nreturn value + 1");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto* leafType = runtime.getTypeByName("CallLeaf");
    const auto* outerType = runtime.getTypeByName("CallOuter");
    const auto* emptyType = runtime.getTypeByName("CallEmpty");
    const auto* holderType = runtime.getTypeByName("CallHolder");
    const auto leafConstructor = leafType->asCompositeType()->getConstructors().front()->getId();
    const auto outerConstructor = outerType->asCompositeType()->getConstructors().front()->getId();
    const auto emptyConstructor = emptyType->asCompositeType()->getConstructors().front()->getId();
    const auto holderConstructor = holderType->asCompositeType()->getConstructors().front()->getId();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        std::optional<InterpreterValue> retainedText;
        std::optional<InterpreterValue> retainedStruct;
        std::optional<InterpreterValue> retainedObject;
        {
            auto machine = runtime.createVirtualMachine();
            machine->pushInteger(1234);
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            RuntimeValue text{NomadString{"host source"}};
            const std::array leafArguments{
                InterpreterValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{17}}),
                InterpreterValue(runtime.getStringType(), text)
            };
            text.freeObjectValue();
            const auto leaf = Interpreter{}.construct(*machine, leafConstructor, leafArguments);
            const auto outer = Interpreter{}.construct(*machine, outerConstructor, {&leaf, 1});
            const auto empty = Interpreter{}.construct(*machine, emptyConstructor, {});
            BOOST_TEST(leaf.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(empty.getType() == emptyType);
            BOOST_TEST(empty.getStorage().data() == nullptr);
            const std::array arguments{
                outer, empty, InterpreterValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{41}})
            };
            auto copy = Interpreter{}.callFunction(*machine, runtime.getFunctionId("copyAggregate"), arguments);
            BOOST_TEST(copy.getType() == outerType);
            BOOST_TEST(copy.getStorage().data() != outer.getStorage().data());
            BOOST_TEST(copy.getStorage()[0].getIntegerValue() == 41);
            BOOST_TEST(outer.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(arguments[0].getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(copy.getStorage()[2].getObjectValue() == outer.getStorage()[2].getObjectValue());
            retainedText = Interpreter{}.callFunction(*machine, runtime.getFunctionId("readAggregateText"), {&copy, 1});
            retainedObject = Interpreter{}.construct(*machine, holderConstructor, {&outer, 1});
            retainedStruct = std::move(copy);
            const auto emptyCopy = Interpreter{}.callFunction(
                *machine, runtime.getFunctionId("copyCallEmpty"), {&empty, 1});
            BOOST_TEST(emptyCopy.getType() == emptyType);
            BOOST_TEST(emptyCopy.getStorage().data() == nullptr);
            const std::array consumeArguments{
                outer, empty, InterpreterValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{17}})
            };
            const auto consumed = Interpreter{}.callFunction(
                *machine, runtime.getFunctionId("consumeAggregate"), consumeArguments);
            BOOST_TEST(consumed.getType()->isVoid());
            BOOST_TEST(consumed.getStorage().data() == nullptr);
            const auto scalar = Interpreter{}.callFunction(
                *machine, runtime.getFunctionId("hostScalar"), {&arguments[2], 1});
            BOOST_TEST(scalar.getValue().getIntegerValue() == 42);
            BOOST_TEST(machine->getIntegerResult() == 41);
            BOOST_TEST(machine->getIntegerIntermediate() == 43);
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            machine->pop1();
        }
        BOOST_TEST(retainedText->getValue().getStringValue() == "host source");
        BOOST_TEST(retainedStruct->getStorage()[0].getIntegerValue() == 41);
        BOOST_TEST(retainedStruct->getStorage()[1].getStringValue() == "host source");
        const auto* holder = dynamic_cast<const ScriptObject*>(retainedObject->getValue().getObjectValue());
        BOOST_REQUIRE(holder != nullptr);
        BOOST_TEST(holder->getFieldStorage(0)[0].getIntegerValue() == 17);
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(host_native_calls_and_factories_preserve_values_frames_and_registered_result_types) {
    Runtime runtime;
    const auto valueId = runtime.registerStruct("HostValue");
    BOOST_REQUIRE(runtime.registerProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    const auto* valueType = runtime.getTypeByName("HostValue");
    const auto constructorId = runtime.registerNativeStructConstructor(valueId, {
        .parameters = {{runtime.getIntegerType()->getId(), "number", ""}},
        .function = [](VirtualMachine* active) {
            active->getStorageParameter(1, 0) = active->getParameter(0);
            active->getStorageParameter(1, 1).freeObjectValue();
            active->getStorageParameter(1, 1).setStringValue("host native");
        }
    });
    const auto scalarId = runtime.registerNativeFunction("hostNativeScalar", [](VirtualMachine* active) {
        active->setIntegerResult(active->getIntegerParameter(0) + 1);
    }, {defParameter("number", runtime.getIntegerType(), "")}, runtime.getIntegerType(), "");
    const auto transformId = runtime.registerNativeFunction("hostNativeTransform", [&](VirtualMachine* active) {
        const auto source = active->getParameterStorage(0, 2);
        const InterpreterValue number(runtime.getIntegerType(), source[0]);
        const auto result = Interpreter{}.callFunction(*active, scalarId, {&number, 1});
        BOOST_TEST(source[0].getIntegerValue() == 17);
        auto* destination = active->getParameter(2).getStorageAddress();
        valueType->copyValue(source.data(), destination);
        destination[0] = result.getValue();
        active->getParameterStorage(0, 2)[0].setIntegerValue(99);
    }, {defParameter("value", valueType, "")}, valueType, "");
    const auto ownerId = runtime.registerClass<ClassType>("HostFactory", ClassMode::Closed);
    const auto createdId = runtime.registerClass<ClassType>("HostCreated", ClassMode::Closed);
    const auto factoryId = runtime.registerNativeClassConstructor(ownerId, createdId, {
        .parameters = {},
        .function = [&](VirtualMachine* active) {
            active->getResult().setObjectValue(new ScriptObject(runtime.getType(createdId).value()->asClassType()));
        }
    });
    BOOST_REQUIRE(constructorId.isValid() && scalarId.isValid() && transformId.isValid() && factoryId.isValid());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        machine->pushInteger(1234);
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{17}});
        const auto source = Interpreter{}.construct(*machine, constructorId, {&number, 1});
        const auto copy = Interpreter{}.callFunction(*machine, transformId, {&source, 1});
        BOOST_TEST(source.getStorage()[0].getIntegerValue() == 17);
        BOOST_TEST(copy.getStorage()[0].getIntegerValue() == 18);
        BOOST_TEST(copy.getStorage()[1].getStringValue() == "host native");
        const auto object = Interpreter{}.construct(*machine, factoryId, {});
        BOOST_TEST(object.getType() == runtime.getType(createdId).value());
        BOOST_TEST(object.getValue().getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
        machine->pop1();
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(host_call_validation_rejects_bad_metadata_and_arguments_before_execution) {
    Runtime runtime;
    auto calls = 0;
    const auto functionId = runtime.registerNativeFunction("hostCheckedCall", [&](VirtualMachine* active) {
        ++calls;
        active->setIntegerResult(active->getIntegerParameter(0));
    }, {defParameter("number", runtime.getIntegerType(), "")}, runtime.getIntegerType(), "");
    const auto emptyId = runtime.registerStruct("HostCheckedEmpty");
    runtime.finalizeCompositeFieldLayout(emptyId);
    const auto constructorId = runtime.registerNativeStructConstructor(emptyId, {
        .parameters = {{runtime.getIntegerType()->getId(), "number", ""}},
        .function = [&](VirtualMachine*) { ++calls; }
    });
    const auto uncompiledId = runtime.registerScriptFunction("hostUncompiled", "", "return 1");
    auto compiler = runtime.createCompiler();
    const auto unresolvedId = runtime.registerNativeFunction("hostUnresolved", [&](VirtualMachine*) {
        ++calls;
    }, {defParameter("number", compiler->deferType({"int"}), "")}, runtime.getIntegerType(), "");
    BOOST_REQUIRE(functionId.isValid() && constructorId.isValid() && uncompiledId.isValid() && unresolvedId.isValid());
    VirtualMachine machine(&runtime);
    machine.pushInteger(1234);
    machine.setIntegerResult(41);
    machine.setIntegerIntermediate(43);
    const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{17}});
    const InterpreterValue boolean(runtime.getBooleanType(), RuntimeValue{true});
    const InterpreterValue empty(runtime.getTypeByName("HostCheckedEmpty"), std::span<const RuntimeValue>{});
    const InterpreterValue voidValue(runtime.getVoidType(), std::span<const RuntimeValue>{});
    Runtime otherRuntime;
    const InterpreterValue foreign(otherRuntime.getIntegerType(), RuntimeValue{NomadInteger{17}});
    auto moved = number;
    const auto movedDestination = std::move(moved);
    const Interpreter interpreter;
    BOOST_CHECK_THROW(static_cast<void>(interpreter.callFunction(machine, CallableId{}, {})), std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.callFunction(machine, constructorId, {&number, 1})),
        std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.construct(machine, functionId, {&number, 1})),
        std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.construct(machine, CallableId{}, {})), std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.callFunction(machine, functionId, {})), std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.construct(machine, constructorId, {})), std::invalid_argument);
    const std::array<const InterpreterValue*, 5> invalidArguments{&boolean, &empty, &voidValue, &foreign, &moved};
    for (const auto* argument : invalidArguments) {
        BOOST_CHECK_THROW(static_cast<void>(interpreter.callFunction(machine, functionId, {argument, 1})),
            std::invalid_argument);
        BOOST_CHECK_THROW(static_cast<void>(interpreter.construct(machine, constructorId, {argument, 1})),
            std::invalid_argument);
    }
    BOOST_CHECK_THROW(static_cast<void>(interpreter.callFunction(machine, uncompiledId, {})), std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.callFunction(machine, unresolvedId, {&number, 1})),
        std::invalid_argument);
    BOOST_TEST(calls == 0);
    BOOST_TEST(movedDestination.getValue().getIntegerValue() == 17);
    BOOST_TEST(machine.getIntegerResult() == 41);
    BOOST_TEST(machine.getIntegerIntermediate() == 43);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 1234);
    const auto result = interpreter.callFunction(machine, functionId, {&number, 1});
    BOOST_TEST(result.getValue().getIntegerValue() == 17);
    const auto constructed = interpreter.construct(machine, constructorId, {&number, 1});
    BOOST_TEST(constructed.getType() == empty.getType());
    BOOST_TEST(constructed.getStorage().data() == nullptr);
    BOOST_TEST(calls == 2);
    InterpreterContext context(interpreter, machine);
    runtime.executeNativeFunction(functionId, {RuntimeValue{NomadInteger{25}}}, context);
    BOOST_REQUIRE(!context.hasError());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 25);
    BOOST_TEST(calls == 3);
    BOOST_TEST(machine.getIntegerResult() == 41);
    BOOST_TEST(machine.getIntegerIntermediate() == 43);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 1234);
    machine.pop1();
}

BOOST_AUTO_TEST_CASE(host_calls_preserve_scalar_suspension_and_reject_suspended_aggregate_calls) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto pauseId = runtime.registerNativeFunction("pauseHostCall", [](VirtualMachine* active) {
        active->yield(RuntimeValue{NomadInteger{37}});
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(pauseId.isValid());
    fixture.addFunction("hostSuspensionTypes", "struct HostSuspensionEmpty\nend\nreturn 0");
    fixture.addFunction("hostScalarSuspension", "pauseHostCall\nreturn 99");
    fixture.addFunction("hostAggregateSuspension", "params value:HostSuspensionEmpty\npauseHostCall\nreturn value");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    {
        auto machine = runtime.createVirtualMachine();
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        const auto result = Interpreter{}.callFunction(*machine, runtime.getFunctionId("hostScalarSuspension"), {});
        BOOST_TEST(result.getValue().getIntegerValue() == 37);
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
    }
    {
        auto machine = runtime.createVirtualMachine();
        machine->pushInteger(1234);
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        const InterpreterValue empty(runtime.getTypeByName("HostSuspensionEmpty"), std::span<const RuntimeValue>{});
        BOOST_CHECK_EXCEPTION(static_cast<void>(Interpreter{}.callFunction(
            *machine, runtime.getFunctionId("hostAggregateSuspension"), {&empty, 1})), VirtualMachineException,
            [](const auto& error) { return NomadString{error.what()} == "Aggregate script calls cannot yield"; });
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
        machine->pop1();
    }
}

BOOST_AUTO_TEST_SUITE_END()
