#include <LanguageTestFixture.hpp>

#include <nomad/compiler/Compiler.hpp>

#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/interpreter/InterpreterCallFrame.hpp>
#include <nomad/interpreter/InterpreterValue.hpp>

#include <nomad/script/Closure.hpp>
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
        if (copyCount < copySources.size()) {
            copySources[copyCount] = source;
            copyDestinations[copyCount] = destination;
        }

        ClassType::copyValue(source, destination);
        if (++copyCount == failCopyAt) {
            throw NomadException("Call copy failed");
        }
    }

    mutable NomadIndex copyCount = 0;
    mutable std::array<const RuntimeValue*, 8> copySources{};
    mutable std::array<RuntimeValue*, 8> copyDestinations{};
    NomadIndex failCopyAt = 0;
    bool failInitialization = false;
};

} // namespace

BOOST_AUTO_TEST_SUITE(interpreter_struct_calls)

BOOST_AUTO_TEST_CASE(call_frame_results_borrow_writable_complete_storage_and_preserve_owned_snapshots) {
    Runtime runtime;
    const auto valueId = runtime.registerStruct("FrameResult");
    BOOST_REQUIRE(runtime.registerProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    const auto emptyId = runtime.registerStruct("FrameEmpty");
    runtime.finalizeCompositeFieldLayout(emptyId);
    const auto* valueType = runtime.getType(valueId).value();
    const auto* emptyType = runtime.getType(emptyId).value();
    const auto scalarId = runtime.registerNativeFunction(
        "frameScalar", [](VirtualMachine*) {}, {}, runtime.getStringType(), ""
    );
    const auto aggregateId = runtime.registerNativeFunction(
        "frameAggregate", [](VirtualMachine*) {}, {}, valueType, ""
    );
    const auto emptyFunctionId = runtime.registerNativeFunction(
        "frameEmpty", [](VirtualMachine*) {}, {}, emptyType, ""
    );
    const auto voidId = runtime.registerNativeFunction(
        "frameVoid", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    );
    BOOST_REQUIRE(scalarId.isValid() && aggregateId.isValid() && emptyFunctionId.isValid() && voidId.isValid());
    auto machine = runtime.createVirtualMachine();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    machine->setIntegerResult(71);
    machine->setIntegerIntermediate(83);
    {
        std::optional<InterpreterValue> scalarSnapshot;
        std::optional<InterpreterValue> aggregateSnapshot;
        {
            const InterpreterCallFrame scalar(*machine, *runtime.getFunction(scalarId), {}, runtime.getStringType());
            BOOST_TEST(scalar.getResultSize() == 1U);
            BOOST_TEST(scalar.getResult() == &machine->getResult());
            scalar.getResult()->setStringValue("scalar result");
            auto* scalarObject = scalar.getResult()->getObjectValue();
            BOOST_TEST(scalarObject->getReferenceCount() == 1U);
            BOOST_TEST(scalar.getResult()->getObjectValue() == scalarObject);
            BOOST_TEST(scalarObject->getReferenceCount() == 1U);
            scalarSnapshot.emplace(scalar.getResultValue());
            BOOST_TEST(scalarSnapshot->getType() == runtime.getStringType());
            BOOST_TEST(scalarObject->getReferenceCount() == 2U);
            {
                const InterpreterCallFrame aggregate(*machine, *runtime.getFunction(aggregateId), {}, valueType);
                BOOST_TEST(aggregate.getResultSize() == 2U);
                auto* storage = aggregate.getResult();
                BOOST_REQUIRE(storage != nullptr);
                BOOST_TEST(storage != &machine->getResult());
                BOOST_TEST(machine->getStackAddress(0)->getStorageAddress() == storage);
                storage[0].setIntegerValue(17);
                storage[1].setStringValue("aggregate result");
                aggregateSnapshot.emplace(aggregate.getResultValue());
                BOOST_TEST(aggregateSnapshot->getType() == valueType);
                BOOST_TEST(aggregateSnapshot->getStorage().data() != storage);
                storage[0].setIntegerValue(29);
                BOOST_TEST(aggregate.getResult() == storage);
                BOOST_TEST(aggregateSnapshot->getStorage()[0].getIntegerValue() == 17);
                {
                    const InterpreterCallFrame empty(*machine, *runtime.getFunction(emptyFunctionId), {}, emptyType);
                    BOOST_TEST(empty.getResultSize() == 0U);
                    BOOST_TEST(empty.getResult() == nullptr);
                    const auto snapshot = empty.getResultValue();
                    BOOST_TEST(snapshot.getType() == emptyType);
                    BOOST_TEST(snapshot.getStorage().empty());
                }
                {
                    const InterpreterCallFrame noResult(*machine, *runtime.getFunction(voidId), {}, runtime.getVoidType());
                    BOOST_TEST(noResult.getResultSize() == 0U);
                    BOOST_TEST(noResult.getResult() == nullptr);
                    const auto snapshot = noResult.getResultValue();
                    BOOST_TEST(snapshot.getType() == runtime.getVoidType());
                    BOOST_TEST(snapshot.getStorage().empty());
                }
                BOOST_TEST(aggregate.getResult() == storage);
                BOOST_TEST(storage[0].getIntegerValue() == 29);
            }
            BOOST_TEST(scalar.getResult()->getObjectValue() == scalarObject);
            BOOST_TEST(scalar.getResult()->getStringValue() == "scalar result");
        }
        BOOST_TEST(scalarSnapshot->getValue().getStringValue() == "scalar result");
        BOOST_TEST(scalarSnapshot->getValue().getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(aggregateSnapshot->getStorage()[1].getStringValue() == "aggregate result");
        BOOST_TEST(aggregateSnapshot->getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(machine->getIntegerResult() == 71);
        BOOST_TEST(machine->getIntermediate().getIntegerValue() == 83);
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

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
            const Interpreter contextEvaluator(*machine);
            InterpreterContext context(contextEvaluator);
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
            BOOST_REQUIRE(context.getInterpreter().execute("readAggregateText copy", context));
            const InterpreterValue text(*context.getResult());
            BOOST_TEST(text.getValue().getStringValue() == "source");
            BOOST_REQUIRE(context.getInterpreter().execute("holder = new CallHolder source", context));
            const auto* holder = dynamic_cast<const ScriptObject*>(context.getVariableValue("holder")->getObjectValue());
            BOOST_REQUIRE(holder != nullptr);
            BOOST_TEST(holder->getFieldStorage(0)[0].getIntegerValue() == 17);
            BOOST_TEST(source[0].getIntegerValue() == 17);
            BOOST_REQUIRE(context.getInterpreter().execute("makeAggregate 23", context));
            BOOST_TEST(context.getResult()->getType() == outer);
            BOOST_TEST(context.getResult()->getStorage()[0].getIntegerValue() == 23);
            BOOST_TEST(context.getResult()->getStorage()[1].getStringValue() == "created");
            const InterpreterValue snapshot(*context.getResult());
            const auto emptyBefore = emptyCalls;
            BOOST_REQUIRE(context.getInterpreter().execute("copyCallEmpty (new CallEmpty)", context));
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
        const Interpreter nestedEvaluator(*active);
        InterpreterContext nested(nestedEvaluator);
        nested.setVariable("value", valueType, source);
        const auto evaluated = nested.getInterpreter().execute("nativeCallScore value", nested);
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
            const Interpreter contextEvaluator(*machine);
            InterpreterContext context(contextEvaluator);
            BOOST_REQUIRE(context.getInterpreter().execute("source = new NativeCallValue 17 \"native\"", context));
            const auto source = context.getVariableStorage("source");
            BOOST_REQUIRE(context.getInterpreter().execute(
                "copy = nativeCallTransform source (new NativeCallEmpty) 41", context));
            BOOST_TEST(source[0].getIntegerValue() == 17);
            BOOST_TEST(context.getVariableStorage("copy")[0].getIntegerValue() == 41);
            BOOST_TEST(context.getVariableStorage("copy")[1].getStringValue() == "native");
            BOOST_REQUIRE(context.getInterpreter().execute(
                "forwardNativeAggregate source (new NativeCallEmpty) 51", context));
            BOOST_TEST(context.getResult()->getStorage()[0].getIntegerValue() == 51);
            BOOST_TEST(source[0].getIntegerValue() == 17);
            const auto callsBefore = emptyReturns;
            BOOST_REQUIRE(context.getInterpreter().execute("nativeCallEmpty 7", context));
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
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);
    for (const auto* source : {
        "nativeCallPair (callNote 1) (callNote 2)",
        "scriptCallPair (callNote 1) (callNote 2)",
        "new OrderedCallValue (callNote 1) (callNote 2)"
    }) {
        order.clear();
        const auto evaluated = context.getInterpreter().execute(source, context);
        BOOST_REQUIRE_MESSAGE(evaluated, (context.getError() ? *context.getError() : ""));
        BOOST_REQUIRE_EQUAL(order.size(), 3U);
        BOOST_TEST(order[0] == 2);
        BOOST_TEST(order[1] == 1);
        BOOST_TEST(order[2] == 3);
    }
    order.clear();
    BOOST_REQUIRE(context.getInterpreter().execute(
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
        const Interpreter contextEvaluator(*active);
        InterpreterContext context(contextEvaluator, *callable);
        BOOST_TEST(context.getFunction() == nullptr);
        const auto source = context.getVariableStorage("value");
        BOOST_REQUIRE_EQUAL(source.size(), 4U);
        BOOST_TEST(source[0].getIntegerValue() == 17);
        const auto copied = context.getInterpreter().execute("local = copyAggregate value empty 71", context);
        BOOST_REQUIRE_MESSAGE(copied, (context.getError() ? *context.getError() : ""));
        BOOST_TEST(context.getVariableStorage("local")[0].getIntegerValue() == 71);
        BOOST_TEST(context.getVariableStorage("value")[0].getIntegerValue() == 17);
        BOOST_REQUIRE(context.getInterpreter().execute("readAggregateText local", context));
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
            const Interpreter contextEvaluator(*machine);
            InterpreterContext context(contextEvaluator);
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
            BOOST_CHECK_THROW(static_cast<void>(Interpreter{*machine}.runFunction(consumeId, std::span<const InterpreterValue>{&argument, 1})), NomadException);
            BOOST_TEST(argument.getStorage()[0].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(argument.getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{19}});
            field->copyCount = 0;
            BOOST_CHECK_THROW(static_cast<void>(Interpreter{*machine}.runFunction(makeId, {})), NomadException);
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 19);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            field->failCopyAt = 0;
            field->failInitialization = true;
            BOOST_CHECK_THROW(static_cast<void>(Interpreter{*machine}.construct(constructorId, {})), NomadException);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            field->failInitialization = false;
            const auto result = Interpreter{*machine}.runFunction(consumeId, std::span<const InterpreterValue>{&argument, 1});
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
            const auto leaf = Interpreter{*machine}.construct(leafConstructor, leafArguments);
            const auto outer = Interpreter{*machine}.construct(outerConstructor, {&leaf, 1});
            const auto empty = Interpreter{*machine}.construct(emptyConstructor, {});
            BOOST_TEST(leaf.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(empty.getType() == emptyType);
            BOOST_TEST(empty.getStorage().data() == nullptr);
            const std::array arguments{
                outer, empty, InterpreterValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{41}})
            };
            auto copy = Interpreter{*machine}.runFunction(runtime.getFunctionId("copyAggregate"), arguments);
            BOOST_TEST(copy.getType() == outerType);
            BOOST_TEST(copy.getStorage().data() != outer.getStorage().data());
            BOOST_TEST(copy.getStorage()[0].getIntegerValue() == 41);
            BOOST_TEST(outer.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(arguments[0].getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(copy.getStorage()[2].getObjectValue() == outer.getStorage()[2].getObjectValue());
            retainedText = Interpreter{*machine}.runFunction(runtime.getFunctionId("readAggregateText"), {&copy, 1});
            retainedObject = Interpreter{*machine}.construct(holderConstructor, {&outer, 1});
            retainedStruct = std::move(copy);
            const auto emptyCopy = Interpreter{*machine}.runFunction(runtime.getFunctionId("copyCallEmpty"), {&empty, 1});
            BOOST_TEST(emptyCopy.getType() == emptyType);
            BOOST_TEST(emptyCopy.getStorage().data() == nullptr);
            const std::array consumeArguments{
                outer, empty, InterpreterValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{17}})
            };
            const auto consumed = Interpreter{*machine}.runFunction(runtime.getFunctionId("consumeAggregate"), consumeArguments);
            BOOST_TEST(consumed.getType()->isVoid());
            BOOST_TEST(consumed.getStorage().data() == nullptr);
            const auto scalar = Interpreter{*machine}.runFunction(runtime.getFunctionId("hostScalar"), {&arguments[2], 1});
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
        const auto result = Interpreter{*active}.runFunction(scalarId, {&number, 1});
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
        const auto source = Interpreter{*machine}.construct(constructorId, {&number, 1});
        const auto copy = Interpreter{*machine}.runFunction(transformId, {&source, 1});
        BOOST_TEST(source.getStorage()[0].getIntegerValue() == 17);
        BOOST_TEST(copy.getStorage()[0].getIntegerValue() == 18);
        BOOST_TEST(copy.getStorage()[1].getStringValue() == "host native");
        const auto object = Interpreter{*machine}.construct(factoryId, {});
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
    const Interpreter interpreter(machine);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runFunction(CallableId{}, {})), std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runFunction(constructorId, {&number, 1})),
        std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.construct(functionId, {&number, 1})),
        std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.construct(CallableId{}, {})), std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runFunction(functionId, {})), std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.construct(constructorId, {})), std::invalid_argument);
    const std::array<const InterpreterValue*, 5> invalidArguments{&boolean, &empty, &voidValue, &foreign, &moved};
    for (const auto* argument : invalidArguments) {
        BOOST_CHECK_THROW(static_cast<void>(interpreter.runFunction(functionId, {argument, 1})),
            std::invalid_argument);
        BOOST_CHECK_THROW(static_cast<void>(interpreter.construct(constructorId, {argument, 1})),
            std::invalid_argument);
    }
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runFunction(uncompiledId, {})), std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runFunction(unresolvedId, {&number, 1})),
        std::invalid_argument);
    BOOST_TEST(calls == 0);
    BOOST_TEST(movedDestination.getValue().getIntegerValue() == 17);
    BOOST_TEST(machine.getIntegerResult() == 41);
    BOOST_TEST(machine.getIntegerIntermediate() == 43);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 1234);
    const auto result = interpreter.runFunction(functionId, {&number, 1});
    BOOST_TEST(result.getValue().getIntegerValue() == 17);
    const auto constructed = interpreter.construct(constructorId, {&number, 1});
    BOOST_TEST(constructed.getType() == empty.getType());
    BOOST_TEST(constructed.getStorage().data() == nullptr);
    BOOST_TEST(calls == 2);
    InterpreterContext context(interpreter);
    context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{19}});
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runFunction(functionId, {})), std::invalid_argument);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 19);
    const InterpreterValue argument(runtime.getIntegerType(), RuntimeValue{NomadInteger{25}});
    const auto returned = interpreter.runFunction(functionId, {&argument, 1});
    BOOST_TEST(returned.getValue().getIntegerValue() == 25);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 19);
    context.setResult(returned);
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
        const auto before = machine->captureExecutionState();
        const auto result = Interpreter{*machine}.runFunction(runtime.getFunctionId("hostScalarSuspension"), {});
        BOOST_TEST(result.getValue().getIntegerValue() == 37);
        BOOST_TEST(machine->hasYielded());
        BOOST_TEST(!machine->isRunning());
        BOOST_TEST(machine->captureExecutionState().stackIndex > before.stackIndex);
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
    }
    {
        auto machine = runtime.createVirtualMachine();
        machine->pushInteger(1234);
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        const InterpreterValue empty(runtime.getTypeByName("HostSuspensionEmpty"), std::span<const RuntimeValue>{});
        BOOST_CHECK_EXCEPTION(static_cast<void>(Interpreter{*machine}.runFunction(runtime.getFunctionId("hostAggregateSuspension"), {&empty, 1})), VirtualMachineException,
            [](const auto& error) { return NomadString{error.what()} == "Aggregate script calls cannot yield"; });
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
        machine->pop1();
    }
}

BOOST_AUTO_TEST_CASE(host_method_copy_failures_release_arguments_results_and_receiver_stack_references) {
    Runtime runtime;
    FailingCallCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingCallCopyType>("FailingMethodField", ClassMode::Closed, field);
    const auto ownerId = runtime.registerClass<ClassType>("FailingMethodOwner", ClassMode::Closed);
    BOOST_REQUIRE(fieldId.isValid() && ownerId.isValid());
    const auto* owner = runtime.getType(ownerId).value()->asClassType();
    const auto constructorId = runtime.registerNativeClassConstructor(ownerId, {
        .parameters = {},
        .function = [owner](VirtualMachine* active) { active->getResult().setObjectValue(new ScriptObject(owner)); }
    });
    auto calls = 0;
    const auto consumeId = runtime.addMethod(ownerId, {
        .name = "consume", .parameters = {{fieldId, "value", ""}},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine*) { ++calls; }
    });
    const auto makeId = runtime.addMethod(ownerId, {
        .name = "make", .parameters = {}, .returnTypeId = fieldId,
        .function = [](VirtualMachine* active) { active->setStringResult("method result"); }
    });
    BOOST_REQUIRE(constructorId.isValid() && consumeId.isValid() && makeId.isValid());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        const Interpreter interpreter(*machine);
        const auto receiver = interpreter.construct(constructorId, {});
        RuntimeValue source{NomadString{"method argument"}};
        const InterpreterValue argument(field, source);
        source.freeObjectValue();
        machine->pushInteger(1234);
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        const auto live = RuntimeValue::getLiveStringCount();
        field->copyCount = 0;
        field->failCopyAt = 1;
        BOOST_CHECK_THROW(static_cast<void>(interpreter.runObjectMethod(receiver, consumeId, {&argument, 1})), NomadException);
        BOOST_TEST(calls == 0);
        BOOST_TEST(argument.getValue().getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(receiver.getValue().getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
        field->copyCount = 0;
        BOOST_CHECK_THROW(static_cast<void>(interpreter.runObjectMethod(receiver, makeId, {})), NomadException);
        BOOST_TEST(receiver.getValue().getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
        field->failCopyAt = 0;
        const auto consumed = interpreter.runObjectMethod(receiver, consumeId, {&argument, 1});
        BOOST_TEST(consumed.getType()->isVoid());
        BOOST_TEST(calls == 1);
        const auto result = interpreter.runObjectMethod(receiver, makeId, {});
        BOOST_TEST(result.getValue().getStringValue() == "method result");
        BOOST_TEST(argument.getValue().getStringValue() == "method argument");
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        machine->pop1();
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(native_object_methods_use_aggregate_storage_and_preserve_receiver_and_argument_bindings) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("NativeObjectMethodEmpty");
    const auto valueId = runtime.registerStruct("NativeObjectMethodValue");
    const auto* type = runtime.getTypeByName("NativeObjectMethodValue");
    const auto* emptyType = runtime.getTypeByName("NativeObjectMethodEmpty");
    BOOST_REQUIRE(runtime.registerProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "text", runtime.getStringType()).isValid());
    const auto singleId = runtime.registerStruct("NativeObjectMethodSingle");
    const auto* singleType = runtime.getTypeByName("NativeObjectMethodSingle");
    BOOST_REQUIRE(runtime.registerProperty(singleId, "number", runtime.getIntegerType()).isValid());
    const auto ownerId = runtime.registerClass("NativeAggregateOwner", ClassMode::Closed);
    const auto* owner = runtime.getTypeByName("NativeAggregateOwner")->asClassType();
    runtime.finalizeCompositeFieldLayout(valueId);
    runtime.finalizeCompositeFieldLayout(emptyId);
    runtime.finalizeCompositeFieldLayout(singleId);
    const auto constructorId = runtime.registerNativeClassConstructor(ownerId, {
        .parameters = {},
        .function = [owner](VirtualMachine* active) { active->getResult().setObjectValue(new ScriptObject(owner)); }
    });
    const auto readId = runtime.addMethod(ownerId, {
        .name = "read", .parameters = {{valueId, "value", ""}},
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) {
            active->setIntegerResult(active->getParameterStorage(0, 2)[0].getIntegerValue());
        }
    });
    const auto singleMethodId = runtime.addMethod(ownerId, {
        .name = "single", .parameters = {{singleId, "value", ""}}, .returnTypeId = singleId,
        .function = [singleType](VirtualMachine* active) {
            singleType->copyValue(active->getParameterStorage(0, 1).data(), &active->getStorageParameter(1, 0));
        }
    });
    auto calls = 0;
    InterpreterContext* activeContext = nullptr;
    const auto exchangeId = runtime.addMethod(ownerId, {
        .name = "exchange",
        .parameters = {{valueId, "value", ""}, {emptyId, "empty", ""},
            {runtime.getIntegerType()->getId(), "delta", ""}},
        .returnTypeId = valueId,
        .function = [&](VirtualMachine* active) {
            ++calls;
            auto* object = active->getThis();
            const auto argument = active->getParameterStorage(0, 2);
            auto* destination = &active->getStorageParameter(3, 0);
            BOOST_TEST(argument.data() != destination);
            argument[0].setIntegerValue(argument[0].getIntegerValue() + active->getIntegerParameter(2));
            argument[1].freeObjectValue();
            argument[1].setStringValue("native argument copy");
            const Interpreter nested(*active);
            const InterpreterValue snapshot(type, argument);
            const auto result = nested.runObjectMethod(*owner, object, readId, {&snapshot, 1});
            BOOST_TEST(result.getValue().getIntegerValue() == 20);
            BOOST_TEST(active->getThis() == object);
            BOOST_TEST(active->getParameterStorage(0, 2).data() == argument.data());
            BOOST_TEST(&active->getStorageParameter(3, 0) == destination);
            BOOST_TEST(active->getIntegerParameter(2) == 3);
            type->copyValue(argument.data(), destination);
        }
    });
    BOOST_REQUIRE(constructorId.isValid() && readId.isValid() && singleMethodId.isValid() && exchangeId.isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("clearObjectMethodOwner", [&](VirtualMachine*) {
        activeContext->clearVariableValue("owner");
    }, {}, emptyType, "").isValid());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        {
            std::optional<InterpreterValue> retained;
            {
                auto machine = runtime.createVirtualMachine();
                const Interpreter interpreter(*machine);
                const auto receiver = interpreter.construct(constructorId, {});
                std::array<RuntimeValue, 2> source{RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadString{"source"}}};
                const InterpreterValue argument(type, source);
                type->freeValue(source.data());
                const InterpreterValue empty(emptyType, std::span<const RuntimeValue>{});
                const InterpreterValue delta(runtime.getIntegerType(), RuntimeValue{NomadInteger{3}});
                const std::array arguments{argument, empty, delta};
                machine->pushInteger(1234);
                machine->setIntegerResult(41);
                machine->setIntegerIntermediate(43);
                const RuntimeValue singleSource{NomadInteger{31}};
                const InterpreterValue single(singleType, std::span<const RuntimeValue>{&singleSource, 1});
                const auto singleResult = interpreter.runObjectMethod(receiver, singleMethodId, {&single, 1});
                BOOST_TEST(singleResult.getType() == singleType);
                BOOST_TEST(singleResult.getStorage().size() == 1U);
                BOOST_TEST(singleResult.getStorage()[0].getIntegerValue() == 31);
                BOOST_TEST(singleResult.getStorage().data() != single.getStorage().data());
                retained = interpreter.runObjectMethod(receiver, exchangeId, arguments);
                BOOST_TEST(retained->getStorage()[0].getIntegerValue() == 20);
                BOOST_TEST(retained->getStorage()[1].getStringValue() == "native argument copy");
                BOOST_TEST(retained->getStorage().data() != argument.getStorage().data());
                BOOST_TEST(argument.getStorage()[0].getIntegerValue() == 17);
                BOOST_TEST(argument.getStorage()[1].getStringValue() == "source");
                InterpreterContext context(interpreter);
                activeContext = &context;
                context.setVariable("owner", owner, receiver.getStorage());
                context.setVariable("argument", type, argument.getStorage());
                context.setVariable("empty", emptyType, std::span<const RuntimeValue>{});
                BOOST_REQUIRE_MESSAGE(interpreter.execute("result = owner.exchange argument empty 3",
                    context), (context.getError() ? *context.getError() : ""));
                BOOST_TEST(context.getResult()->getStorage()[0].getIntegerValue() == 20);
                BOOST_TEST(context.getVariableStorage("argument")[0].getIntegerValue() == 17);
                {
                    const auto temporaryOwner = interpreter.construct(constructorId, {});
                    context.setVariable("owner", owner, temporaryOwner.getStorage());
                }
                const auto ownersWithTemporary = NomadObject::getLiveObjectCount();
                BOOST_REQUIRE_MESSAGE(interpreter.execute("owner.exchange argument (clearObjectMethodOwner) 3", context),
                    (context.getError() ? *context.getError() : ""));
                BOOST_TEST(context.getVariableType("owner") == nullptr);
                BOOST_TEST(context.getResult()->getStorage()[0].getIntegerValue() == 20);
                context.clearResult();
                BOOST_TEST(NomadObject::getLiveObjectCount() == ownersWithTemporary - 1);
                BOOST_TEST(machine->getIntegerResult() == 41);
                BOOST_TEST(machine->getIntegerIntermediate() == 43);
                BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
                BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
                machine->pop1();
            }
            BOOST_TEST(retained->getStorage()[1].getStringValue() == "native argument copy");
            BOOST_TEST(retained->getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(calls == 9);
}

BOOST_AUTO_TEST_CASE(aggregate_object_method_failures_release_partial_arguments_and_results) {
    Runtime runtime;
    FailingCallCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingCallCopyType>("ObjectAggregateCopyField", ClassMode::Closed, field);
    const auto valueId = runtime.registerStruct("ObjectAggregateCopyValue");
    const auto* type = runtime.getTypeByName("ObjectAggregateCopyValue");
    BOOST_REQUIRE(fieldId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "first", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "second", field).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    const auto ownerId = runtime.registerClass("ObjectAggregateCopyOwner", ClassMode::Closed);
    const auto* owner = runtime.getTypeByName("ObjectAggregateCopyOwner")->asClassType();
    const auto constructorId = runtime.registerNativeClassConstructor(ownerId, {
        .parameters = {},
        .function = [owner](VirtualMachine* active) { active->getResult().setObjectValue(new ScriptObject(owner)); }
    });
    auto calls = 0;
    auto throwAfterWrite = false;
    const auto methodId = runtime.addMethod(ownerId, {
        .name = "copy", .parameters = {{valueId, "argument", ""}}, .returnTypeId = valueId,
        .function = [&](VirtualMachine* active) {
            ++calls;
            auto* destination = &active->getStorageParameter(2, 0);
            if (throwAfterWrite) {
                destination[0].setStringValue("partial object method result");
                throw NomadException("Object method failed");
            }
            type->copyValue(active->getParameterStorage(0, 2).data(), destination);
        }
    });
    BOOST_REQUIRE(constructorId.isValid() && methodId.isValid());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        {
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            const auto receiver = interpreter.construct(constructorId, {});
            std::array<RuntimeValue, 2> source{
                RuntimeValue{NomadString{"first source"}}, RuntimeValue{NomadString{"second source"}}};
            const InterpreterValue argument(type, source);
            type->freeValue(source.data());
            machine->pushInteger(1234);
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            field->copyCount = 0;
            field->failCopyAt = 0;
            {
                const auto result = interpreter.runObjectMethod(receiver, methodId, {&argument, 1});
                BOOST_TEST(result.getStorage()[0].getStringValue() == "first source");
            }
            const auto copyStages = field->copyCount;
            BOOST_TEST(copyStages == 6U);
            const auto live = RuntimeValue::getLiveStringCount();
            for (auto failure = NomadIndex{1}; failure <= copyStages; ++failure) {
                field->copyCount = 0;
                field->failCopyAt = failure;
                BOOST_CHECK_THROW(static_cast<void>(interpreter.runObjectMethod(receiver, methodId, {&argument, 1})),
                    NomadException);
                BOOST_TEST(argument.getStorage()[0].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(argument.getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(receiver.getValue().getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
                BOOST_TEST(machine->getIntegerResult() == 41);
                BOOST_TEST(machine->getIntegerIntermediate() == 43);
                BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
                BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
            }
            field->failCopyAt = 0;
            throwAfterWrite = true;
            BOOST_CHECK_THROW(static_cast<void>(interpreter.runObjectMethod(receiver, methodId, {&argument, 1})),
                NomadException);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            throwAfterWrite = false;
            const auto beforeOverflow = calls;
            {
                VirtualMachine smallMachine(&runtime, 3);
                const Interpreter smallInterpreter(smallMachine);
                smallMachine.pushInteger(4321);
                BOOST_CHECK_EXCEPTION(static_cast<void>(
                    smallInterpreter.runObjectMethod(receiver, methodId, {&argument, 1})), VirtualMachineException,
                    [](const auto& error) { return NomadString{error.what()} == "Stack overflow"; });
                BOOST_TEST(calls == beforeOverflow);
                BOOST_TEST(receiver.getValue().getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
                BOOST_TEST(smallMachine.peekStack(0).getIntegerValue() == 4321);
                smallMachine.pop1();
            }
            machine->pop1();
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(closure_arguments_and_captures_are_copied_once_directly_into_vm_storage) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    FailingCallCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingCallCopyType>(
        "DirectClosureField",
        ClassMode::Closed,
        field
    );
    const auto valueId = runtime.registerStruct("DirectClosurePair");
    const auto* type = runtime.getTypeByName("DirectClosurePair");
    BOOST_REQUIRE(fieldId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "first", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "second", field).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "makeDirectClosurePair",
        [](VirtualMachine* active) {
            active->getStorageParameter(0, 0).setStringValue("first capture");
            active->getStorageParameter(0, 1).setStringValue("second capture");
        },
        {},
        type,
        ""
    ).isValid());
    std::vector<std::unique_ptr<Closure>> callbacks;
    const Closure* activeClosure = nullptr;
    const RuntimeValue* explicitSource = nullptr;
    auto probes = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "keepDirectClosure",
        [&](VirtualMachine* active) {
            callbacks.push_back(active->createClosure(&runtime, active->getCallableIdParameter(0)));
        },
        {defParameter("callback", runtime.getCallbackType({field}, runtime.getIntegerType()), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "inspectDirectClosureCopies",
        [&](VirtualMachine* active) {
            ++probes;
            const auto* function = activeClosure->getFunction();
            const auto capture = activeClosure->getCaptureStorage(0);
            const auto destination = active->getEnclosingParameterStorage(
                function->getParameterOffset(1),
                capture.size()
            );
            BOOST_TEST(field->copyCount == capture.size() + 1);
            for (auto index = NomadIndex{0}; index < capture.size(); ++index) {
                BOOST_TEST(field->copySources[index] == capture.data() + index);
                BOOST_TEST(field->copyDestinations[index] == destination.data() + index);
            }

            BOOST_TEST(field->copySources[capture.size()] == explicitSource);
            BOOST_TEST(field->copyDestinations[capture.size()] ==
                active->getEnclosingParameterStorage(function->getParameterOffset(0), 1).data());
            BOOST_TEST(destination.data() != capture.data());
        },
        {},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction(
        "main",
        "captured = makeDirectClosurePair\n"
        "keepDirectClosure fun value\ninspectDirectClosureCopies\n"
        "captured.first = value\nvalue = captured.second\nreturn 17\nend\n"
        "scalar = captured.first\n"
        "keepDirectClosure fun value\ninspectDirectClosureCopies\nvalue = scalar\nreturn 17\nend\n"
        "return 0"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        {
            const auto outcome = fixture.execute("main");
            BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
            BOOST_REQUIRE(callbacks.size() == 2U);
            RuntimeValue source{NomadString{"explicit argument"}};
            const InterpreterValue argument(field, source);
            source.freeObjectValue();
            const std::vector<RuntimeValue> rawArguments{argument.getValue()};
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            machine->pushInteger(1234);
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            for (const auto& callback : callbacks) {
                activeClosure = callback.get();
                const auto expectedCopies = callback->getCaptureStorage(0).size() + 1;
                for (auto invocation = 0; invocation < 2; ++invocation) {
                    field->copyCount = 0;
                    explicitSource = argument.getStorage().data();
                    BOOST_TEST(interpreter.runFunction(*callback, {&argument, 1}).getValue().getIntegerValue() == 17);
                    BOOST_TEST(field->copyCount == expectedCopies);
                    field->copyCount = 0;
                    explicitSource = rawArguments.data();
                    RuntimeValue result;
                    runtime.executeFunction(callback.get(), rawArguments, result);
                    BOOST_TEST(result.getIntegerValue() == 17);
                    BOOST_TEST(field->copyCount == expectedCopies);
                    BOOST_TEST(argument.getValue().getStringValue() == "explicit argument");
                    BOOST_TEST(argument.getValue().getObjectValue()->getReferenceCount() == 1U);
                    BOOST_TEST(callback->getCaptureStorage(0)[0].getStringValue() == "first capture");
                    BOOST_TEST(machine->getIntegerResult() == 41);
                    BOOST_TEST(machine->getIntegerIntermediate() == 43);
                    BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
                }

                const auto live = RuntimeValue::getLiveStringCount();
                for (auto failure = NomadIndex{1}; failure <= expectedCopies; ++failure) {
                    field->copyCount = 0;
                    field->failCopyAt = failure;
                    BOOST_CHECK_THROW(
                        static_cast<void>(interpreter.runFunction(*callback, {&argument, 1})),
                        NomadException
                    );
                    field->copyCount = 0;
                    RuntimeValue result{NomadInteger{71}};
                    BOOST_CHECK_THROW(runtime.executeFunction(callback.get(), rawArguments, result), NomadException);
                    BOOST_TEST(result.getIntegerValue() == 71);
                    BOOST_TEST(argument.getValue().getObjectValue()->getReferenceCount() == 1U);
                    BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
                    BOOST_TEST(machine->getIntegerResult() == 41);
                    BOOST_TEST(machine->getIntegerIntermediate() == 43);
                    BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
                }

                field->failCopyAt = 0;
            }

            {
                VirtualMachine smallMachine(&runtime, 3);
                const Interpreter smallInterpreter(smallMachine);
                smallMachine.pushInteger(4321);
                field->copyCount = 0;
                BOOST_CHECK_EXCEPTION(
                    static_cast<void>(smallInterpreter.runFunction(*callbacks[0], {&argument, 1})),
                    VirtualMachineException,
                    [](const auto& error) { return NomadString{error.what()} == "Stack overflow"; }
                );
                BOOST_TEST(field->copyCount == 0U);
                BOOST_TEST(smallMachine.peekStack(0).getIntegerValue() == 4321);
                field->copyCount = 0;
                BOOST_CHECK_EXCEPTION(
                    smallMachine.run(*callbacks[0], rawArguments),
                    VirtualMachineException,
                    [](const auto& error) { return NomadString{error.what()} == "Stack overflow"; }
                );
                BOOST_TEST(field->copyCount == 0U);
                BOOST_TEST(smallMachine.peekStack(0).getIntegerValue() == 4321);
                smallMachine.pop1();
            }

            machine->pop1();
        }

        callbacks.clear();
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

    BOOST_TEST(probes == 24);
}

BOOST_AUTO_TEST_CASE(compiled_formatting_reuses_direct_frame_captures_and_cleans_partial_struct_copies) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    FailingCallCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingCallCopyType>(
        "FormattingCopyField",
        ClassMode::Closed,
        field
    );
    const auto valueId = runtime.registerStruct("FormattingCopyPair");
    const auto* type = runtime.getTypeByName("FormattingCopyPair");
    BOOST_REQUIRE(fieldId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "first", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "second", field).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    fixture.addFunction("renderCopyPair", "params value:FormattingCopyPair\nreturn $\"{value.first}\"");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        {
            std::array<RuntimeValue, 2> source{
                RuntimeValue{NomadString{"first formatted"}},
                RuntimeValue{NomadString{"second formatted"}}
            };
            const InterpreterValue argument(type, source);
            type->freeValue(source.data());
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            machine->pushInteger(1234);
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            field->copyCount = 0;
            {
                const auto result = interpreter.runFunction(runtime.getFunctionId("renderCopyPair"), {&argument, 1});
                BOOST_TEST(result.getValue().getStringValue() == "FormattingCopyField");
                BOOST_TEST(field->copyCount == 4U);
                BOOST_TEST(field->copySources[2] == field->copyDestinations[0]);
                BOOST_TEST(field->copySources[3] == field->copyDestinations[1]);
            }

            const auto live = RuntimeValue::getLiveStringCount();
            for (const auto failure : {NomadIndex{3}, NomadIndex{4}}) {
                field->copyCount = 0;
                field->failCopyAt = failure;
                BOOST_CHECK_THROW(
                    static_cast<void>(interpreter.runFunction(runtime.getFunctionId("renderCopyPair"), {&argument, 1})),
                    NomadException
                );
                BOOST_TEST(argument.getStorage()[0].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(argument.getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
                BOOST_TEST(machine->getIntegerResult() == 41);
                BOOST_TEST(machine->getIntegerIntermediate() == 43);
                BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            }

            field->failCopyAt = 0;
            machine->pop1();
        }

        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

}

BOOST_AUTO_TEST_CASE(typed_closure_call_failures_release_capture_arguments_and_result_snapshots) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    FailingCallCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingCallCopyType>(
        "ClosureInvocationField",
        ClassMode::Closed,
        field
    );
    const auto valueId = runtime.registerStruct("ClosureInvocationValue");
    const auto* type = runtime.getTypeByName("ClosureInvocationValue");
    BOOST_REQUIRE(fieldId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "first", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "second", field).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "makeClosureInvocationValue",
        [](VirtualMachine* active) {
            active->getStorageParameter(0, 0).setStringValue("first captured");
            active->getStorageParameter(0, 1).setStringValue("second captured");
        },
        {},
        type,
        ""
    ).isValid());
    std::unique_ptr<Closure> closure;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "keepFailingTypedClosure",
        [&](VirtualMachine* active) {
            closure = active->createClosure(&runtime, active->getCallableIdParameter(0));
        },
        {defParameter("callback", runtime.getCallbackType({}, type), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction(
        "main",
        "captured = makeClosureInvocationValue\n"
        "keepFailingTypedClosure fun\nreturn captured\nend\nreturn 0"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        {
            const auto outcome = fixture.execute("main");
            BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
            BOOST_REQUIRE(closure != nullptr);
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            machine->pushInteger(1234);
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            field->copyCount = 0;
            {
                const auto result = interpreter.runFunction(*closure, {});
                BOOST_TEST(result.getStorage()[0].getStringValue() == "first captured");
                BOOST_TEST(result.getStorage()[1].getStringValue() == "second captured");
            }

            const auto copyStages = field->copyCount;
            BOOST_TEST(copyStages == 4U);
            const auto live = RuntimeValue::getLiveStringCount();
            for (auto failure = NomadIndex{1}; failure <= copyStages; ++failure) {
                field->copyCount = 0;
                field->failCopyAt = failure;
                BOOST_CHECK_THROW(static_cast<void>(interpreter.runFunction(*closure, {})), NomadException);
                BOOST_TEST(closure->getCaptureStorage(0)[0].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(closure->getCaptureStorage(0)[1].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
                BOOST_TEST(machine->getIntegerResult() == 41);
                BOOST_TEST(machine->getIntegerIntermediate() == 43);
                BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            }

            field->failCopyAt = 0;
            {
                VirtualMachine smallMachine(&runtime, 3);
                const Interpreter smallInterpreter(smallMachine);
                smallMachine.pushInteger(4321);
                BOOST_CHECK_EXCEPTION(
                    static_cast<void>(smallInterpreter.runFunction(*closure, {})),
                    VirtualMachineException,
                    [](const auto& error) { return NomadString{error.what()} == "Stack overflow"; }
                );
                BOOST_TEST(smallMachine.peekStack(0).getIntegerValue() == 4321);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
                smallMachine.pop1();
            }

            machine->pop1();
        }

        closure.reset();
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

}

BOOST_AUTO_TEST_CASE(closure_capture_copy_failures_release_completed_and_partial_capture_slots) {
    Runtime runtime;
    FailingCallCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingCallCopyType>(
        "ClosureCopyField",
        ClassMode::Closed,
        field
    );
    const auto valueId = runtime.registerStruct("ClosureCopyValue");
    const auto* type = runtime.getTypeByName("ClosureCopyValue");
    BOOST_REQUIRE(fieldId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "first", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "second", field).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    const auto parentId = runtime.registerScriptFunction("closureCopyParent", "captures.nomad", "");
    auto* parent = runtime.getScriptFunction(parentId);
    parent->addParameter("value", type, "");
    const auto variable = parent->registerVariable("local", type);
    parent->setReturnType(runtime.getVoidType());
    const auto descriptorId = runtime.registerScriptFunction(
        "closureCopyDescriptor",
        "captures.nomad",
        ""
    );
    auto* descriptor = runtime.getScriptFunction(descriptorId);
    descriptor->setParentId(parentId);
    descriptor->createCapture(variable, "local", type);
    descriptor->createCapture(NomadId{0}, "value", type);
    std::span<const RuntimeValue> source;
    auto currentFrame = true;
    const auto probeId = runtime.registerNativeFunction(
        "test.closureCopyProbe",
        [&](VirtualMachine* active) {
            field->failCopyAt = 0;
            if (currentFrame) {
                std::vector<RuntimeValue> scratch(type->getAssignmentScratchSize());
                type->assignValue(
                    source.data(),
                    active->getLocalStorage(parent->getVariableOffset(variable), type->getSize()).data(),
                    scratch.data()
                );
            }

            const auto live = RuntimeValue::getLiveStringCount();
            for (auto failure = NomadIndex{1}; failure <= 4; ++failure) {
                field->copyCount = 0;
                field->failCopyAt = failure;
                if (currentFrame) {
                    BOOST_CHECK_THROW(static_cast<void>(active->captureCurrentFrame(descriptor)), NomadException);
                }

                if (!currentFrame) {
                    BOOST_CHECK_THROW(static_cast<void>(active->createClosure(&runtime, descriptorId)), NomadException);
                }

                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
                BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 3U);
                BOOST_TEST(source[1].getObjectValue()->getReferenceCount() == 3U);
            }

            field->failCopyAt = 0;
            field->copyCount = 0;
            if (currentFrame) {
                const auto snapshots = active->captureCurrentFrame(descriptor);
                BOOST_TEST(snapshots.size() == 2U);
                BOOST_TEST(field->copyCount == 4U);
                currentFrame = false;

                return;
            }

            {
                const auto closure = active->createClosure(&runtime, descriptorId);
                BOOST_TEST(closure->getCaptureCount() == 2U);
                BOOST_TEST(closure->getCaptureStorage(0).size() == 2U);
                BOOST_TEST(closure->getCaptureStorage(1).size() == 2U);
                BOOST_TEST(field->copyCount == 4U);
            }

            BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 3U);
        },
        {},
        runtime.getVoidType(),
        ""
    );
    BOOST_REQUIRE(probeId.isValid());
    const auto probe = runtime.registerInstruction(
        "test.currentClosureCopy",
        [](VirtualMachine* active) {
            const auto* function = active->getRuntime()->getFunction(
                active->getRuntime()->getCallableId("test.closureCopyProbe")
            )->asNativeFunction();
            function->getFn()(active);
        },
        "",
        {}
    );
    auto compiler = runtime.createCompiler();
    parent->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(parent);
    compiler->addOpCode(probe);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(probeId.id);
    compiler->addFunctionReturn(parent);
    parent->setFunctionEnd(runtime.getInstructions().size());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        {
            std::array<RuntimeValue, 2> values{
                RuntimeValue{NomadString{"first capture"}},
                RuntimeValue{NomadString{"second capture"}}
            };
            const InterpreterValue argument(type, values);
            type->freeValue(values.data());
            source = argument.getStorage();
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            currentFrame = true;
            BOOST_TEST(interpreter.runFunction(parentId, {&argument, 1}).getType()->isVoid());
            BOOST_TEST(argument.getStorage()[0].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(argument.getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
        }

        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

}

BOOST_AUTO_TEST_CASE(struct_method_copy_failures_and_stack_overflow_release_every_owned_snapshot) {
    Runtime runtime;
    FailingCallCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingCallCopyType>("StructMethodCopyField", ClassMode::Closed, field);
    const auto valueId = runtime.registerStruct("StructMethodCopyValue");
    const auto* type = runtime.getTypeByName("StructMethodCopyValue");
    BOOST_REQUIRE(fieldId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "first", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "second", field).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    auto calls = 0;
    const auto methodId = runtime.addMethod(valueId, {
        .name = "copy", .parameters = {{valueId, "snapshot", ""}}, .returnTypeId = valueId,
        .function = [&](VirtualMachine* active) {
            ++calls;
            auto* receiver = &active->getStorageParameter(2, 0);
            auto* destination = &active->getStorageParameter(3, 0);
            const auto argument = active->getParameterStorage(0, 2);
            receiver[0].freeObjectValue();
            receiver[0].setStringValue("changed receiver");
            argument[1].freeObjectValue();
            argument[1].setStringValue("changed argument");
            type->copyValue(receiver, destination);
        }
    });
    BOOST_REQUIRE(methodId.isValid());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        const Interpreter interpreter(*machine);
        std::array<RuntimeValue, 2> source{
            RuntimeValue{NomadString{"first source"}}, RuntimeValue{NomadString{"second source"}}
        };
        const InterpreterValue receiver(type, source);
        type->freeValue(source.data());
        field->failInitialization = true;
        machine->pushInteger(1234);
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        const auto live = RuntimeValue::getLiveStringCount();
        for (auto failure = NomadIndex{1}; failure <= 10; ++failure) {
            BOOST_TEST_CONTEXT("Copy failure " << failure) {
                field->copyCount = 0;
                field->failCopyAt = failure;
                BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, methodId, {&receiver, 1})),
                    NomadException);
                BOOST_TEST(receiver.getStorage()[0].getStringValue() == "first source");
                BOOST_TEST(receiver.getStorage()[1].getStringValue() == "second source");
                BOOST_TEST(receiver.getStorage()[0].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(receiver.getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
                BOOST_TEST(machine->getIntegerResult() == 41);
                BOOST_TEST(machine->getIntegerIntermediate() == 43);
                BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
                BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
            }
        }
        BOOST_TEST(calls == 6);
        field->failCopyAt = 0;
        field->copyCount = 0;
        {
            const auto result = interpreter.runStructMethod(receiver, methodId, {&receiver, 1});
            BOOST_TEST(field->copyCount == 10);
            BOOST_TEST(result.receiver.getStorage()[0].getStringValue() == "changed receiver");
            BOOST_TEST(result.result.getStorage()[0].getStringValue() == "changed receiver");
            BOOST_TEST(result.receiver.getStorage()[1].getStringValue() == "second source");
            BOOST_TEST(receiver.getStorage()[0].getStringValue() == "first source");
            BOOST_TEST(result.receiver.getStorage().data() != result.result.getStorage().data());
        }
        BOOST_TEST(calls == 7);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
        {
            VirtualMachine smallMachine(&runtime, 4);
            const Interpreter smallInterpreter(smallMachine);
            smallMachine.pushInteger(4321);
            smallMachine.setIntegerResult(73);
            smallMachine.setIntegerIntermediate(79);
            BOOST_CHECK_EXCEPTION(static_cast<void>(smallInterpreter.runStructMethod(receiver, methodId, {&receiver, 1})),
                VirtualMachineException,
                [](const auto& error) { return NomadString{error.what()} == "Stack overflow"; });
            BOOST_TEST(calls == 7);
            BOOST_TEST(receiver.getStorage()[0].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(receiver.getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            BOOST_TEST(smallMachine.getIntegerResult() == 73);
            BOOST_TEST(smallMachine.getIntegerIntermediate() == 79);
            BOOST_TEST(smallMachine.peekStack(0).getIntegerValue() == 4321);
            smallMachine.pop1();
        }
        machine->pop1();
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(synchronous_script_function_calls_restore_prior_yield_state) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    BOOST_REQUIRE(runtime.registerNativeFunction("pauseSynchronousCall", [](VirtualMachine* active) {
        active->yield(RuntimeValue{NomadInteger{37}});
    }, {}, runtime.getVoidType(), "").isValid());
    fixture.addFunction("synchronousCallTypes", "struct SynchronousCallEmpty\nend\nreturn 0");
    fixture.addFunction("completeSynchronousCall", "params value:SynchronousCallEmpty\nreturn value");
    fixture.addFunction("yieldSynchronousCall",
        "params value:SynchronousCallEmpty\npauseSynchronousCall\nreturn value");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const InterpreterValue empty(runtime.getTypeByName("SynchronousCallEmpty"), std::span<const RuntimeValue>{});
    machine->pushInteger(1234);
    for (const auto previouslyYielded : {false, true}) {
        if (previouslyYielded) {
            machine->yield(RuntimeValue{NomadInteger{41}});
        } else {
            machine->stop(RuntimeValue{NomadInteger{41}});
        }
        machine->setIntegerIntermediate(43);
        const auto instruction = machine->getInstructionIndex();
        const auto result = interpreter.runFunction(runtime.getFunctionId("completeSynchronousCall"), {&empty, 1});
        BOOST_TEST(result.getType() == empty.getType());
        BOOST_TEST(result.getStorage().data() == nullptr);
        BOOST_TEST(machine->hasYielded() == previouslyYielded);
        BOOST_CHECK_EXCEPTION(static_cast<void>(interpreter.runFunction(
            runtime.getFunctionId("yieldSynchronousCall"), {&empty, 1})), VirtualMachineException,
            [](const auto& error) { return NomadString{error.what()} == "Aggregate script calls cannot yield"; });
        BOOST_TEST(machine->hasYielded() == previouslyYielded);
        BOOST_TEST(!machine->isRunning());
        BOOST_TEST(machine->getInstructionIndex() == instruction);
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
    }
    machine->stop(RuntimeValue{});
    machine->pop1();
}

BOOST_AUTO_TEST_SUITE_END()
