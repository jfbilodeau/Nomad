#include <LanguageTestFixture.hpp>

#include <nomad/compiler/Compiler.hpp>

#include <nomad/interpreter/Interpreter.hpp>

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
using namespace nomad::test;

namespace {

void addStructMethodSources(LanguageTestFixture& fixture) {
    fixture.addFunction("structMethodTypes",
        "class MethodSharedOwner\nnumber = 5\nend\n"
        "struct HostMethodEmpty\n"
        "fun emptyReturn\nreturn this\nend\n"
        "fun make number:int\nreturn new HostMethodValue number \"made\"\nend\n"
        "fun scalar\nreturn 7\nend\n"
        "fun clear\nreturn\nend\nend\n"
        "struct HostMethodValue\nnumber = 7\ntext = \"default\"\nowner = new MethodSharedOwner\n"
        "init number:int text:string\nthis.number = number\nthis.text = text\nend\n"
        "fun mutate snapshot:HostMethodValue empty:HostMethodEmpty delta:int\n"
        "this.number = this.number + delta\nthis.text = \"updated\"\n"
        "this.owner.number = this.number\nsnapshot.number = 99\nsnapshot.text = \"argument copy\"\nreturn this\nend\n"
        "fun readText\nreturn this.text\nend\n"
        "fun readOwner\nreturn this.owner\nend\n"
        "fun set number:int\nthis.number = number\nreturn\nend\n"
        "fun empty\nthis.number = this.number + 1\nreturn new HostMethodEmpty\nend\nend\n"
        "struct HostMethodOuter\nleaf = new HostMethodValue 9 \"nested\"\nlabel = \"outer\"\n"
        "fun update number:int\nthis.leaf.number = number\nthis.leaf.text = \"nested update\"\nreturn this.leaf\nend\nend\n"
        "return 0");
}

void addCounterMethodSources(LanguageTestFixture& fixture) {
    fixture.addFunction("counterMethodTypes",
        "struct Counter\nvalue = 0\n"
        "fun next\nthis.value = this.value + 1\nreturn this.value\nend\n"
        "fun bump delta:int\nthis.value = this.value + delta\nreturn this.value\nend\n"
        "fun add first:int second:int\nthis.value = this.value + first * 10 + second\nreturn this.value\nend\n"
        "fun relay\nreturn this.next + this.next\nend\nend\n"
        "struct CounterOuter\nprefix = 7\ncounter = new Counter\nlabel = \"outer\"\nend\n"
        "class CounterHolder\ncounter = new CounterOuter\nend\nreturn 0");
}

} // namespace

BOOST_AUTO_TEST_SUITE(interpreter_struct_methods)

BOOST_AUTO_TEST_CASE(source_counter_calls_mutate_the_original_before_the_next_operand_and_preserve_value_copies) {
    LanguageTestFixture fixture;
    addCounterMethodSources(fixture);
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* counter = runtime.getTypeByName("Counter");
    const auto instructionsBefore = runtime.getInstructions().size();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    machine->pushInteger(1234);
    machine->setIntegerResult(41);
    machine->setIntegerIntermediate(43);
    {
        InterpreterContext context(interpreter);
        BOOST_REQUIRE_MESSAGE(interpreter.execute("c = new Counter\ntotal = c.next + c.next", context),
            (context.hasError() ? context.getFirstError()->message : ""));
        BOOST_TEST(context.getVariableStorage("c")[0].getIntegerValue() == 2);
        BOOST_TEST(context.getVariableValue("total")->getIntegerValue() == 3);
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 3);
        const auto* address = context.getVariableStorage("c").data();
        const InterpreterValue snapshot(counter, context.getVariableStorage("c"));
        BOOST_REQUIRE(interpreter.execute("copy = c\ncopyResult = copy.next\nnested = c.relay", context));
        BOOST_TEST(context.getVariableStorage("c").data() == address);
        BOOST_TEST(context.getVariableStorage("copy").data() != address);
        BOOST_TEST(context.getVariableStorage("copy")[0].getIntegerValue() == 3);
        BOOST_TEST(context.getVariableStorage("c")[0].getIntegerValue() == 4);
        BOOST_TEST(context.getVariableValue("nested")->getIntegerValue() == 7);
        BOOST_TEST(snapshot.getStorage()[0].getIntegerValue() == 2);
        BOOST_TEST(runtime.getInstructions().size() == instructionsBefore);
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
    }
    machine->pop1();
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(source_methods_borrow_nested_and_object_owned_fields_and_discard_temporary_storage) {
    LanguageTestFixture fixture;
    addCounterMethodSources(fixture);
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    {
        InterpreterContext context(interpreter);
        BOOST_REQUIRE_MESSAGE(interpreter.execute(
            "outer = new CounterOuter\nfirst = outer.counter.next\nsecond = outer.counter.next\n"
            "holder = new CounterHolder\nheld = holder.counter.counter.next\n"
            "temporary = (new Counter).next\nnestedTemporary = (new CounterOuter).counter.next", context),
            (context.hasError() ? context.getFirstError()->message : ""));
        BOOST_TEST(context.getVariableStorage("outer")[0].getIntegerValue() == 7);
        BOOST_TEST(context.getVariableStorage("outer")[1].getIntegerValue() == 2);
        BOOST_TEST(context.getVariableStorage("outer")[2].getStringValue() == "outer");
        BOOST_TEST(context.getVariableValue("first")->getIntegerValue() == 1);
        BOOST_TEST(context.getVariableValue("second")->getIntegerValue() == 2);
        BOOST_TEST(context.getVariableValue("held")->getIntegerValue() == 1);
        BOOST_TEST(context.getVariableValue("temporary")->getIntegerValue() == 1);
        BOOST_TEST(context.getVariableValue("nestedTemporary")->getIntegerValue() == 1);
        BOOST_REQUIRE(interpreter.execute("holder.counter.counter.value", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 1);
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(source_method_parameters_and_returned_structs_are_independent_and_empty_methods_remain_callable) {
    LanguageTestFixture fixture;
    addStructMethodSources(fixture);
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    {
        InterpreterContext context(interpreter);
        BOOST_REQUIRE_MESSAGE(interpreter.execute(
            "value = new HostMethodValue 17 \"source\"\n"
            "returned = value.mutate value (new HostMethodEmpty) 11\n"
            "returned.number = 41\nempty = new HostMethodEmpty\n"
            "same = empty.emptyReturn\nscalar = empty.scalar\n"
            "made = empty.make 9\nempty.clear", context),
            (context.hasError() ? context.getFirstError()->message : ""));
        BOOST_TEST(context.getVariableStorage("value")[0].getIntegerValue() == 28);
        BOOST_TEST(context.getVariableStorage("value")[1].getStringValue() == "updated");
        BOOST_TEST(context.getVariableStorage("returned")[0].getIntegerValue() == 41);
        BOOST_TEST(context.getVariableStorage("returned").data() != context.getVariableStorage("value").data());
        BOOST_TEST(context.getVariableType("same") == runtime.getTypeByName("HostMethodEmpty"));
        BOOST_TEST(context.getVariableStorage("same").empty());
        BOOST_TEST(context.getVariableValue("scalar")->getIntegerValue() == 7);
        BOOST_TEST(context.getVariableStorage("made")[0].getIntegerValue() == 9);
        BOOST_TEST(context.getResult()->getType()->isVoid());
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(source_receiver_evaluation_precedes_right_to_left_arguments_and_rebinds_context_storage) {
    LanguageTestFixture fixture;
    addCounterMethodSources(fixture);
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* counter = runtime.getTypeByName("Counter");
    std::vector<NomadString> events;
    const auto make = runtime.registerNativeFunction("makeSourceCounter", [&](VirtualMachine* active) {
        events.emplace_back("receiver");
        active->getStorageParameter(0, 0).setIntegerValue(0);
    }, {}, counter, "");
    const auto first = runtime.registerNativeFunction("firstSourceArgument", [&](VirtualMachine* active) {
        events.emplace_back("first");
        active->setIntegerResult(1);
    }, {}, runtime.getIntegerType(), "");
    const auto second = runtime.registerNativeFunction("secondSourceArgument", [&](VirtualMachine* active) {
        events.emplace_back("second");
        active->setIntegerResult(2);
    }, {}, runtime.getIntegerType(), "");
    BOOST_REQUIRE(make.isValid() && first.isValid() && second.isValid());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    BOOST_REQUIRE_MESSAGE(interpreter.execute(
        "result = (makeSourceCounter).add (firstSourceArgument) (secondSourceArgument)", context),
        (context.hasError() ? context.getFirstError()->message : ""));
    const std::vector<NomadString> expected{"receiver", "second", "first"};
    BOOST_TEST(events == expected, boost::test_tools::per_element());
    BOOST_TEST(context.getVariableValue("result")->getIntegerValue() == 12);
    BOOST_REQUIRE(interpreter.execute("c = new Counter", context));
    const auto* address = context.getVariableStorage("c").data();
    auto replacementMode = 0;
    const auto replace = runtime.registerNativeFunction("replaceSourceCounter", [&](VirtualMachine* active) {
        if (replacementMode == 0) {
            const std::array<RuntimeValue, 1> replacement{RuntimeValue{NomadInteger{100}}};
            context.setVariable("c", counter, replacement);
        } else if (replacementMode == 1) {
            context.clearVariableValue("c");
        } else {
            context.setVariable("c", runtime.getIntegerType(), RuntimeValue{NomadInteger{42}});
        }
        active->setIntegerResult(1);
    }, {}, runtime.getIntegerType(), "");
    BOOST_REQUIRE(replace.isValid());
    BOOST_REQUIRE(interpreter.execute("result = c.bump (replaceSourceCounter)", context));
    BOOST_TEST(context.getVariableStorage("c").data() == address);
    BOOST_TEST(context.getVariableStorage("c")[0].getIntegerValue() == 101);
    BOOST_TEST(context.getVariableValue("result")->getIntegerValue() == 101);
    replacementMode = 1;
    BOOST_TEST(!interpreter.execute("result = c.bump (replaceSourceCounter)", context));
    BOOST_REQUIRE(context.hasError());
    BOOST_TEST(context.getFirstError()->message.find("has not been assigned") != NomadString::npos);
    BOOST_TEST(context.getResult() == nullptr);
    BOOST_REQUIRE(interpreter.execute("c = new Counter", context));
    replacementMode = 2;
    BOOST_TEST(!interpreter.execute("result = c.bump (replaceSourceCounter)", context));
    BOOST_REQUIRE(context.hasError());
    BOOST_TEST(context.getFirstError()->message.find("changed type") != NomadString::npos);
    BOOST_TEST(context.getVariableValue("c")->getIntegerValue() == 42);
    BOOST_TEST(context.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(source_object_field_receivers_keep_the_evaluated_owner_across_argument_replacement) {
    LanguageTestFixture fixture;
    addCounterMethodSources(fixture);
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* holder = runtime.getTypeByName("CounterHolder");
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    BOOST_REQUIRE(interpreter.execute("holder = new CounterHolder", context));
    const InterpreterValue original(holder, context.getVariableStorage("holder"));
    const auto replace = runtime.registerNativeFunction("replaceSourceHolder", [&](VirtualMachine* active) {
        context.setVariable("holder", runtime.getIntegerType(), RuntimeValue{NomadInteger{42}});
        active->setIntegerResult(1);
    }, {}, runtime.getIntegerType(), "");
    BOOST_REQUIRE(replace.isValid());
    BOOST_REQUIRE_MESSAGE(interpreter.execute("result = holder.counter.counter.bump (replaceSourceHolder)", context),
        (context.hasError() ? context.getFirstError()->message : ""));
    BOOST_TEST(context.getVariableValue("holder")->getIntegerValue() == 42);
    BOOST_TEST(context.getVariableValue("result")->getIntegerValue() == 1);
    const auto storage = original.getValue().asObject<ScriptObject>()->getFieldStorage(0);
    BOOST_TEST(storage[0].getIntegerValue() == 7);
    BOOST_TEST(storage[1].getIntegerValue() == 1);
    BOOST_TEST(storage[2].getStringValue() == "outer");
}

BOOST_AUTO_TEST_CASE(source_methods_expose_live_mutations_during_native_reentry_and_preserve_writes_before_failure) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    InterpreterContext* evaluation = nullptr;
    const Interpreter* evaluator = nullptr;
    auto observations = 0;
    const auto observe = runtime.registerNativeFunction("observeSourceCounter", [&](VirtualMachine* active) {
        ++observations;
        const auto* callable = active->getCurrentCallable();
        const auto parameter = active->getParameterStorage(0, 1);
        BOOST_TEST(parameter[0].getIntegerValue() == 7);
        BOOST_TEST(evaluation->getVariableStorage("c")[0].getIntegerValue() == 1);
        BOOST_REQUIRE(evaluator->execute("nested = c.next", *evaluation));
        BOOST_TEST(evaluation->getVariableStorage("c")[0].getIntegerValue() == 2);
        BOOST_TEST(active->getCurrentCallable() == callable);
        BOOST_TEST(active->getParameterStorage(0, 1).data() == parameter.data());
    }, {Parameter{.name = "marker", .type = runtime.getIntegerType()}}, runtime.getVoidType(), "");
    const auto fail = runtime.registerNativeFunction("failSourceCounter", [](VirtualMachine*) {
        throw NomadException("Source counter failure");
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(observe.isValid() && fail.isValid());
    fixture.addFunction("observedCounterTypes",
        "struct Counter\nvalue = 0\n"
        "fun next\nthis.value = this.value + 1\nreturn this.value\nend\n"
        "fun observe\nthis.value = this.value + 1\nobserveSourceCounter 7\nreturn this.value\nend\n"
        "fun fail\nthis.value = this.value + 1\nfailSourceCounter\nreturn this.value\nend\nend\nreturn 0");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    evaluation = &context;
    evaluator = &interpreter;
    machine->pushInteger(1234);
    machine->setIntegerResult(41);
    machine->setIntegerIntermediate(43);
    BOOST_REQUIRE_MESSAGE(interpreter.execute("c = new Counter\nresult = c.observe", context),
        (context.hasError() ? context.getFirstError()->message : ""));
    BOOST_TEST(observations == 1);
    BOOST_TEST(context.getVariableValue("nested")->getIntegerValue() == 2);
    BOOST_TEST(context.getVariableValue("result")->getIntegerValue() == 2);
    BOOST_TEST(!interpreter.execute("result = c.fail", context));
    BOOST_REQUIRE(context.hasError());
    BOOST_TEST(context.getFirstError()->message.find("Source counter failure") != NomadString::npos);
    BOOST_TEST(context.getVariableStorage("c")[0].getIntegerValue() == 3);
    BOOST_TEST(context.getVariableValue("result")->getIntegerValue() == 2);
    BOOST_TEST(context.getResult() == nullptr);
    BOOST_TEST(machine->getIntegerResult() == 41);
    BOOST_TEST(machine->getIntegerIntermediate() == 43);
    BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
    BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
    machine->pop1();
}

BOOST_AUTO_TEST_CASE(script_methods_return_independent_receiver_and_result_values_without_implicit_writeback) {
    LanguageTestFixture fixture;
    addStructMethodSources(fixture);
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* type = runtime.getTypeByName("HostMethodValue");
    const auto* emptyType = runtime.getTypeByName("HostMethodEmpty");
    const auto mutate = runtime.resolveMethod(type, "mutate", {type, emptyType, runtime.getIntegerType()});
    const auto set = runtime.resolveMethod(type, "set", {runtime.getIntegerType()});
    const auto readText = runtime.resolveMethod(type, "readText", {});
    const auto readOwner = runtime.resolveMethod(type, "readOwner", {});
    const auto emptyReturn = runtime.resolveMethod(type, "empty", {});
    BOOST_REQUIRE(mutate.isResolved() && set.isResolved() && readText.isResolved() &&
        readOwner.isResolved() && emptyReturn.isResolved());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    const auto instructionsBefore = runtime.getInstructions().size();
    {
        std::optional<StructMethodResult> retained;
        std::optional<StructMethodResult> retainedText;
        std::optional<StructMethodResult> retainedOwner;
        {
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            InterpreterContext context(interpreter);
            BOOST_REQUIRE(interpreter.execute("value = new HostMethodValue 17 \"source\"", context));
            const InterpreterValue receiver(type, context.getVariableStorage("value"));
            const InterpreterValue empty(emptyType, std::span<const RuntimeValue>{});
            const InterpreterValue delta(runtime.getIntegerType(), RuntimeValue{NomadInteger{11}});
            const std::array arguments{receiver, empty, delta};
            context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{99}});
            RuntimeValue savedResult{NomadString{"saved struct result"}};
            RuntimeValue savedIntermediate{NomadString{"saved struct intermediate"}};
            machine->setResult(savedResult);
            machine->setIntermediate(savedIntermediate);
            machine->pushInteger(1234);
            retained = interpreter.runStructMethod(receiver, mutate.id, arguments);
            BOOST_TEST(retained->receiver.getType() == type);
            BOOST_TEST(retained->result.getType() == type);
            BOOST_REQUIRE_EQUAL(retained->receiver.getStorage().size(), 3U);
            BOOST_REQUIRE_EQUAL(retained->result.getStorage().size(), 3U);
            BOOST_TEST(retained->receiver.getStorage().data() != receiver.getStorage().data());
            BOOST_TEST(retained->receiver.getStorage().data() != retained->result.getStorage().data());
            BOOST_TEST(retained->receiver.getStorage()[0].getIntegerValue() == 28);
            BOOST_TEST(retained->result.getStorage()[0].getIntegerValue() == 28);
            BOOST_TEST(retained->receiver.getStorage()[1].getStringValue() == "updated");
            BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(receiver.getStorage()[1].getStringValue() == "source");
            BOOST_TEST(arguments[0].getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(context.getVariableStorage("value")[0].getIntegerValue() == 17);
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
            BOOST_TEST(retained->receiver.getStorage()[2].getObjectValue() == receiver.getStorage()[2].getObjectValue());
            BOOST_TEST(receiver.getStorage()[2].asObject<ScriptObject>()->getField(0).getIntegerValue() == 28);
            const InterpreterValue replacement(runtime.getIntegerType(), RuntimeValue{NomadInteger{41}});
            const auto changed = interpreter.runStructMethod(retained->receiver, set.id, {&replacement, 1});
            BOOST_TEST(changed.receiver.getStorage()[0].getIntegerValue() == 41);
            BOOST_TEST(changed.result.getType()->isVoid());
            BOOST_TEST(changed.result.getStorage().data() == nullptr);
            BOOST_TEST(retained->receiver.getStorage()[0].getIntegerValue() == 28);
            const auto emptyResult = interpreter.runStructMethod(changed.receiver, emptyReturn.id, {});
            BOOST_TEST(emptyResult.receiver.getStorage()[0].getIntegerValue() == 42);
            BOOST_TEST(emptyResult.result.getType() == emptyType);
            BOOST_TEST(emptyResult.result.getStorage().data() == nullptr);
            retainedText = interpreter.runStructMethod(receiver, readText.id, {});
            retainedOwner = interpreter.runStructMethod(receiver, readOwner.id, {});
            BOOST_TEST(retainedText->result.getValue().getStringValue() == "source");
            BOOST_TEST(retainedOwner->result.getValue().getObjectValue() == receiver.getStorage()[2].getObjectValue());
            context.setVariable("value", retained->receiver.getType(), retained->receiver.getStorage());
            context.setResult(retained->result);
            BOOST_TEST(context.getVariableStorage("value")[0].getIntegerValue() == 28);
            BOOST_TEST(context.getResult()->getStorage()[0].getIntegerValue() == 28);
            BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(machine->getResult().getObjectValue() == savedResult.getObjectValue());
            BOOST_TEST(machine->getIntermediate().getObjectValue() == savedIntermediate.getObjectValue());
            BOOST_TEST(savedResult.getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(savedIntermediate.getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
            machine->pop1();
            machine->setResult(RuntimeValue{});
            machine->setIntermediate(RuntimeValue{});
            savedResult.freeObjectValue();
            savedIntermediate.freeObjectValue();
        }
        BOOST_TEST(retained->receiver.getStorage()[1].getStringValue() == "updated");
        BOOST_TEST(retained->result.getStorage()[1].getStringValue() == "updated");
        BOOST_TEST(retainedText->result.getValue().getStringValue() == "source");
        BOOST_TEST(retainedOwner->result.getValue().asObject<ScriptObject>()->getField(0).getIntegerValue() == 28);
    }
    BOOST_TEST(runtime.getInstructions().size() == instructionsBefore);
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(nested_script_receivers_and_empty_receiver_result_combinations_keep_complete_types) {
    LanguageTestFixture fixture;
    addStructMethodSources(fixture);
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* outerType = runtime.getTypeByName("HostMethodOuter");
    const auto* valueType = runtime.getTypeByName("HostMethodValue");
    const auto* emptyType = runtime.getTypeByName("HostMethodEmpty");
    const auto update = runtime.resolveMethod(outerType, "update", {runtime.getIntegerType()});
    const auto emptyReturn = runtime.resolveMethod(emptyType, "emptyReturn", {});
    const auto make = runtime.resolveMethod(emptyType, "make", {runtime.getIntegerType()});
    const auto scalar = runtime.resolveMethod(emptyType, "scalar", {});
    const auto clear = runtime.resolveMethod(emptyType, "clear", {});
    BOOST_REQUIRE(update.isResolved() && emptyReturn.isResolved() && make.isResolved() &&
        scalar.isResolved() && clear.isResolved());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        const Interpreter interpreter(*machine);
        const auto outer = interpreter.construct(outerType->asStructType()->getConstructors().front()->getId(), {});
        const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{41}});
        const auto updated = interpreter.runStructMethod(outer, update.id, {&number, 1});
        BOOST_TEST(updated.receiver.getType() == outerType);
        BOOST_REQUIRE_EQUAL(updated.receiver.getStorage().size(), 4U);
        BOOST_TEST(updated.result.getType() == valueType);
        BOOST_REQUIRE_EQUAL(updated.result.getStorage().size(), 3U);
        BOOST_TEST(updated.receiver.getStorage()[0].getIntegerValue() == 41);
        BOOST_TEST(updated.result.getStorage()[0].getIntegerValue() == 41);
        BOOST_TEST(updated.result.getStorage()[1].getStringValue() == "nested update");
        BOOST_TEST(updated.receiver.getStorage()[3].getStringValue() == "outer");
        BOOST_TEST(outer.getStorage()[0].getIntegerValue() == 9);
        BOOST_TEST(outer.getStorage()[1].getStringValue() == "nested");
        const InterpreterValue empty(emptyType, std::span<const RuntimeValue>{});
        const auto emptyResult = interpreter.runStructMethod(empty, emptyReturn.id, {});
        BOOST_TEST(emptyResult.receiver.getType() == emptyType);
        BOOST_TEST(emptyResult.result.getType() == emptyType);
        BOOST_TEST(emptyResult.receiver.getStorage().data() == nullptr);
        BOOST_TEST(emptyResult.result.getStorage().data() == nullptr);
        const auto created = interpreter.runStructMethod(empty, make.id, {&number, 1});
        BOOST_TEST(created.receiver.getType() == emptyType);
        BOOST_TEST(created.receiver.getStorage().data() == nullptr);
        BOOST_TEST(created.result.getType() == valueType);
        BOOST_REQUIRE_EQUAL(created.result.getStorage().size(), 3U);
        BOOST_TEST(created.result.getStorage()[0].getIntegerValue() == 41);
        BOOST_TEST(created.result.getStorage()[1].getStringValue() == "made");
        const auto scalarResult = interpreter.runStructMethod(empty, scalar.id, {});
        BOOST_TEST(scalarResult.receiver.getStorage().data() == nullptr);
        BOOST_TEST(scalarResult.result.getValue().getIntegerValue() == 7);
        const auto cleared = interpreter.runStructMethod(empty, clear.id, {});
        BOOST_TEST(cleared.receiver.getType() == emptyType);
        BOOST_TEST(cleared.result.getType()->isVoid());
        BOOST_TEST(cleared.receiver.getStorage().data() == nullptr);
        BOOST_TEST(cleared.result.getStorage().data() == nullptr);
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(native_methods_use_distinct_receiver_argument_and_result_storage_and_allow_reentry) {
    Runtime runtime;
    const auto valueId = runtime.getTypeManager().registerStruct("NativeHostMethodValue");
    const auto emptyId = runtime.getTypeManager().registerStruct("NativeHostMethodEmpty");
    const auto* type = runtime.getTypeByName("NativeHostMethodValue");
    const auto* emptyType = runtime.getTypeByName("NativeHostMethodEmpty");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "text", runtime.getStringType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(valueId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(emptyId);
    const auto readId = runtime.getTypeManager().registerNativeStructMethod(valueId, {
        .name = "read", .parameters = {}, .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) { active->setIntegerResult(active->getStorageParameter(0, 0).getIntegerValue()); }
    });
    const auto mutateId = runtime.getTypeManager().registerNativeStructMethod(valueId, {
        .name = "mutate",
        .parameters = {{valueId, "snapshot", ""}, {emptyId, "empty", ""}, {runtime.getIntegerType()->getId(), "delta", ""}},
        .returnTypeId = valueId,
        .function = [type, readId](VirtualMachine* active) {
            auto* receiver = &active->getStorageParameter(3, 0);
            auto* destination = &active->getStorageParameter(4, 0);
            const auto argument = active->getParameterStorage(0, 2);
            BOOST_TEST(receiver != argument.data());
            BOOST_TEST(receiver != destination);
            BOOST_TEST(destination != argument.data());
            receiver[0].setIntegerValue(receiver[0].getIntegerValue() + active->getIntegerParameter(2));
            receiver[1].freeObjectValue();
            receiver[1].setStringValue("native update");
            argument[0].setIntegerValue(99);
            argument[1].freeObjectValue();
            argument[1].setStringValue("argument copy");
            const InterpreterValue snapshot(type, std::span<const RuntimeValue>{receiver, 2});
            const Interpreter nested(*active);
            const auto read = nested.runStructMethod(snapshot, readId, {});
            BOOST_TEST(read.result.getValue().getIntegerValue() == 28);
            BOOST_TEST(active->getIntegerParameter(2) == 11);
            BOOST_TEST(&active->getStorageParameter(3, 0) == receiver);
            BOOST_TEST(&active->getStorageParameter(4, 0) == destination);
            BOOST_CHECK_THROW(static_cast<void>(active->getThis()), VirtualMachineException);
            type->copyValue(receiver, destination);
            destination[0].setIntegerValue(destination[0].getIntegerValue() + 1000);
        }
    });
    BOOST_REQUIRE(readId.isValid() && mutateId.isValid());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        std::optional<StructMethodResult> retained;
        {
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            std::array<RuntimeValue, 2> source{RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadString{"source"}}};
            const InterpreterValue receiver(type, source);
            type->freeValue(source.data());
            const InterpreterValue empty(emptyType, std::span<const RuntimeValue>{});
            const std::array arguments{
                receiver, empty, InterpreterValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{11}})
            };
            machine->pushInteger(1234);
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            retained = interpreter.runStructMethod(receiver, mutateId, arguments);
            BOOST_TEST(retained->receiver.getStorage()[0].getIntegerValue() == 28);
            BOOST_TEST(retained->result.getStorage()[0].getIntegerValue() == 1028);
            BOOST_TEST(retained->receiver.getStorage()[1].getStringValue() == "native update");
            BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(receiver.getStorage()[1].getStringValue() == "source");
            BOOST_TEST(arguments[0].getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(arguments[0].getStorage()[1].getStringValue() == "source");
            BOOST_TEST(machine->getIntegerResult() == 41);
            BOOST_TEST(machine->getIntegerIntermediate() == 43);
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            machine->pop1();
        }
        BOOST_TEST(retained->receiver.getStorage()[1].getStringValue() == "native update");
        BOOST_TEST(retained->result.getStorage()[1].getStringValue() == "native update");
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(native_empty_and_one_slot_receivers_keep_hidden_pointer_layouts_distinct) {
    Runtime runtime;
    const auto emptyId = runtime.getTypeManager().registerStruct("NativeMethodEmpty");
    const auto oneId = runtime.getTypeManager().registerStruct("NativeMethodOne");
    const auto* emptyType = runtime.getTypeByName("NativeMethodEmpty");
    const auto* oneType = runtime.getTypeByName("NativeMethodOne");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(oneId, "number", runtime.getIntegerType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(emptyId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(oneId);
    auto emptyCalls = 0;
    const auto emptyMethodId = runtime.getTypeManager().registerNativeStructMethod(emptyId, {
        .name = "empty", .parameters = {{emptyId, "argument", ""}}, .returnTypeId = emptyId,
        .function = [&](VirtualMachine*) { ++emptyCalls; }
    });
    const auto makeId = runtime.getTypeManager().registerNativeStructMethod(emptyId, {
        .name = "make", .parameters = {}, .returnTypeId = oneId,
        .function = [](VirtualMachine* active) { active->getStorageParameter(0, 0).setIntegerValue(9); }
    });
    const auto clearId = runtime.getTypeManager().registerNativeStructMethod(oneId, {
        .name = "clear", .parameters = {}, .returnTypeId = runtime.getVoidType()->getId(),
        .function = [](VirtualMachine* active) { active->getStorageParameter(0, 0).setIntegerValue(0); }
    });
    BOOST_REQUIRE(emptyMethodId.isValid() && makeId.isValid() && clearId.isValid());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const InterpreterValue empty(emptyType, std::span<const RuntimeValue>{});
    const auto same = interpreter.runStructMethod(empty, emptyMethodId, {&empty, 1});
    BOOST_TEST(emptyCalls == 1);
    BOOST_TEST(same.receiver.getType() == emptyType);
    BOOST_TEST(same.result.getType() == emptyType);
    BOOST_TEST(same.receiver.getStorage().data() == nullptr);
    BOOST_TEST(same.result.getStorage().data() == nullptr);
    const auto made = interpreter.runStructMethod(empty, makeId, {});
    BOOST_TEST(made.receiver.getStorage().data() == nullptr);
    BOOST_TEST(made.result.getType() == oneType);
    BOOST_REQUIRE_EQUAL(made.result.getStorage().size(), 1U);
    BOOST_TEST(made.result.getStorage()[0].getIntegerValue() == 9);
    const auto cleared = interpreter.runStructMethod(made.result, clearId, {});
    BOOST_TEST(cleared.receiver.getType() == oneType);
    BOOST_TEST(cleared.receiver.getStorage()[0].getIntegerValue() == 0);
    BOOST_TEST(cleared.result.getType()->isVoid());
    BOOST_TEST(cleared.result.getStorage().data() == nullptr);
    BOOST_TEST(made.result.getStorage()[0].getIntegerValue() == 9);
}

BOOST_AUTO_TEST_CASE(validation_rejects_bad_calls_before_execution_and_source_struct_calls_are_enabled) {
    Runtime runtime;
    const auto valueId = runtime.getTypeManager().registerStruct("CheckedStructReceiver");
    const auto otherId = runtime.getTypeManager().registerStruct("OtherStructReceiver");
    const auto* type = runtime.getTypeByName("CheckedStructReceiver");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "number", runtime.getIntegerType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(valueId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(otherId);
    auto calls = 0;
    const auto methodId = runtime.getTypeManager().registerNativeStructMethod(valueId, {
        .name = "read", .parameters = {{runtime.getIntegerType()->getId(), "number", ""}},
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [&](VirtualMachine* active) { ++calls; active->setIntegerResult(active->getIntegerParameter(0)); }
    });
    const auto functionId = runtime.registerNativeFunction("notStructMethod", [](VirtualMachine*) {},
        {}, runtime.getVoidType(), "");
    const auto uncompiledId = runtime.getTypeManager().registerScriptStructMethod(valueId, "uncompiled", {},
        "uncompiled.nomad", "return 1");
    const auto classId = runtime.getTypeManager().registerClass<ClassType>("ClassMethodReceiver", ClassMode::Closed);
    const auto* classType = runtime.getType(classId).value();
    const auto classMethodId = runtime.getTypeManager().registerNativeClassMethod(classId, {
        .name = "classOnly", .parameters = {}, .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine*) { ++calls; }
    });
    const auto uncompiledClassId = runtime.getTypeManager().registerScriptClassMethod(
        classId,
        "uncompiledClass",
        {},
        "uncompiled-class.nomad",
        "return 1"
    );
    BOOST_REQUIRE(methodId.isValid() && functionId.isValid() && uncompiledId.isValid() && classMethodId.isValid());
    BOOST_REQUIRE(uncompiledClassId.isValid());
    runtime.getScriptCallable(uncompiledId)->setReturnType(runtime.getIntegerType());
    runtime.getScriptCallable(uncompiledClassId)->setReturnType(runtime.getIntegerType());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const std::array<RuntimeValue, 1> source{RuntimeValue{NomadInteger{17}}};
    const InterpreterValue receiver(type, source);
    const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{3}});
    const InterpreterValue boolean(runtime.getBooleanType(), RuntimeValue{true});
    const InterpreterValue other(runtime.getTypeByName("OtherStructReceiver"), std::span<const RuntimeValue>{});
    const InterpreterValue classReceiver(classType, RuntimeValue{});
    Runtime foreignRuntime;
    const auto foreignId = foreignRuntime.getTypeManager().registerStruct("CheckedStructReceiver");
    BOOST_REQUIRE(foreignRuntime.getTypeManager().registerScriptStructProperty(
        foreignId, "number", foreignRuntime.getIntegerType()
    ).isValid());
    foreignRuntime.getTypeManager().finalizeCompositeFieldLayout(foreignId);
    const InterpreterValue foreignReceiver(foreignRuntime.getTypeByName("CheckedStructReceiver"), source);
    const InterpreterValue foreignArgument(foreignRuntime.getIntegerType(), RuntimeValue{NomadInteger{3}});
    auto moved = receiver;
    const auto movedDestination = std::move(moved);
    machine->pushInteger(1234);
    machine->setIntegerResult(41);
    machine->setIntegerIntermediate(43);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, CallableId{}, {})),
        std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, functionId, {})),
        std::invalid_argument);
    BOOST_CHECK_EXCEPTION(
        static_cast<void>(interpreter.runObjectMethod(receiver, functionId, {&number, 1})),
        std::invalid_argument,
        [](const auto& error) { return NomadString{error.what()} == "Unknown method id"; }
    );
    BOOST_CHECK_EXCEPTION(
        static_cast<void>(interpreter.runStructMethod(classReceiver, methodId, {})),
        std::invalid_argument,
        [](const auto& error) {
            return NomadString{error.what()} == "Incorrect argument count for callable 'read'";
        }
    );
    BOOST_CHECK_EXCEPTION(
        static_cast<void>(interpreter.runObjectMethod(receiver, classMethodId, {&number, 1})),
        std::invalid_argument,
        [](const auto& error) {
            return NomadString{error.what()} == "Incorrect argument count for callable 'classOnly'";
        }
    );
    BOOST_CHECK_EXCEPTION(
        static_cast<void>(interpreter.runStructMethod(classReceiver, uncompiledId, {})),
        std::invalid_argument,
        [](const auto& error) { return NomadString{error.what()} == "Method receiver type does not match its owner"; }
    );
    BOOST_CHECK_EXCEPTION(
        static_cast<void>(interpreter.runObjectMethod(receiver, uncompiledId, {})),
        std::invalid_argument,
        [](const auto& error) { return NomadString{error.what()} == "Object method calls require a class receiver"; }
    );
    BOOST_CHECK_EXCEPTION(
        static_cast<void>(interpreter.runStructMethod(classReceiver, uncompiledClassId, {})),
        std::invalid_argument,
        [](const auto& error) { return NomadString{error.what()} == "Struct method calls require a struct receiver"; }
    );
    BOOST_CHECK_EXCEPTION(
        static_cast<void>(interpreter.runObjectMethod(classReceiver, uncompiledClassId, {})),
        std::invalid_argument,
        [](const auto& error) { return NomadString{error.what()} == "Method 'uncompiledClass' is not compiled"; }
    );
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, methodId, {})),
        std::invalid_argument);
    const std::array extraArguments{number, number};
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, methodId, extraArguments)),
        std::invalid_argument);
    for (const auto* argument : std::array{&boolean, &foreignArgument}) {
        BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, methodId, {argument, 1})),
            std::invalid_argument);
    }
    const std::array<const InterpreterValue*, 4> invalidReceivers{&number, &other, &foreignReceiver, &moved};
    for (const auto* invalid : invalidReceivers) {
        BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(*invalid, methodId, {&number, 1})),
            std::invalid_argument);
    }
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(classReceiver, classMethodId, {})),
        std::invalid_argument);
    BOOST_CHECK_EXCEPTION(static_cast<void>(interpreter.runStructMethod(receiver, uncompiledId, {})),
        std::invalid_argument,
        [](const auto& error) { return NomadString{error.what()}.find("not compiled") != NomadString::npos; });
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runObjectMethod(receiver, methodId, {&number, 1})),
        std::invalid_argument);
    BOOST_TEST(calls == 0);
    BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
    BOOST_TEST(movedDestination.getStorage()[0].getIntegerValue() == 17);
    BOOST_TEST(machine->getIntegerResult() == 41);
    BOOST_TEST(machine->getIntegerIntermediate() == 43);
    BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
    InterpreterContext context(interpreter);
    context.setVariable("value", type, receiver.getStorage());
    BOOST_REQUIRE(interpreter.execute("value.read 3", context));
    BOOST_TEST(!context.hasError());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 3);
    BOOST_TEST(calls == 1);
    BOOST_TEST(context.getVariableStorage("value")[0].getIntegerValue() == 17);
    const auto result = interpreter.runStructMethod(receiver, methodId, {&number, 1});
    BOOST_TEST(result.result.getValue().getIntegerValue() == 3);
    BOOST_TEST(result.receiver.getStorage()[0].getIntegerValue() == 17);
    BOOST_TEST(calls == 2);
    machine->pop1();
}

BOOST_AUTO_TEST_CASE(native_failures_discard_struct_replacements_but_keep_shared_object_side_effects) {
    Runtime runtime;
    const auto ownerId = runtime.getTypeManager().registerClass<ClassType>("FailedMethodOwner", ClassMode::Closed);
    const auto* ownerType = runtime.getType(ownerId).value()->asClassType();
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(ownerId, "number", runtime.getIntegerType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(ownerId);
    const auto valueId = runtime.getTypeManager().registerStruct("FailedMethodValue");
    const auto* type = runtime.getTypeByName("FailedMethodValue");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "owner", ownerType).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(valueId);
    const auto methodId = runtime.getTypeManager().registerNativeStructMethod(valueId, {
        .name = "fail", .parameters = {}, .returnTypeId = valueId,
        .function = [](VirtualMachine* active) {
            auto* receiver = &active->getStorageParameter(0, 0);
            auto* destination = &active->getStorageParameter(1, 0);
            receiver[0].setIntegerValue(99);
            receiver[1].freeObjectValue();
            receiver[1].setStringValue("changed receiver");
            receiver[2].asObject<ScriptObject>()->getFieldStorage(0)[0].setIntegerValue(99);
            destination[0].setIntegerValue(77);
            destination[1].setStringValue("partial result");
            throw NomadException("Expected struct method failure");
        }
    });
    BOOST_REQUIRE(methodId.isValid());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        const Interpreter interpreter(*machine);
        std::array<RuntimeValue, 3> source{
            RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadString{"source"}}, RuntimeValue{}
        };
        source[2].setObjectValue(new ScriptObject(ownerType));
        source[2].asObject<ScriptObject>()->getFieldStorage(0)[0].setIntegerValue(5);
        const InterpreterValue receiver(type, source);
        type->freeValue(source.data());
        InterpreterContext context(interpreter);
        context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{99}});
        machine->pushInteger(1234);
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        const auto live = RuntimeValue::getLiveStringCount();
        const auto objects = NomadObject::getLiveObjectCount();
        BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, methodId, {})), NomadException);
        BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
        BOOST_TEST(receiver.getStorage()[1].getStringValue() == "source");
        BOOST_TEST(receiver.getStorage()[2].asObject<ScriptObject>()->getField(0).getIntegerValue() == 99);
        BOOST_TEST(receiver.getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(receiver.getStorage()[2].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objects);
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        machine->pop1();
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(native_struct_completion_stop_and_exceptions_restore_caller_state) {
    Runtime runtime;
    const auto typeId = runtime.getTypeManager().registerStruct("NativeCompletionStruct");
    const auto* type = runtime.getTypeByName("NativeCompletionStruct");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(typeId, "number", runtime.getIntegerType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(typeId);
    auto stop = false;
    auto fail = false;
    const auto methodId = runtime.getTypeManager().registerNativeStructMethod(typeId, {
        .name = "complete", .parameters = {}, .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [&](VirtualMachine* active) {
            active->getStorageParameter(0, 0).setIntegerValue(99);
            active->setIntegerResult(37);
            if (stop) {
                active->stop(RuntimeValue{NomadInteger{37}});
            }

            if (fail) {
                throw NomadException("Expected struct completion failure");
            }

        }
    });
    BOOST_REQUIRE(methodId.isValid());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const std::array source{RuntimeValue{NomadInteger{17}}};
    const InterpreterValue receiver(type, source);
    machine->pushInteger(1234);
    for (const auto initiallyRunning : {false, true}) {
        for (const auto shouldStop : {false, true}) {
            for (const auto shouldFail : {false, true}) {
                stop = shouldStop;
                fail = shouldFail;
                auto before = machine->captureExecutionState();
                before.running = initiallyRunning;
                machine->restoreExecutionState(before);
                machine->setIntegerResult(41);
                machine->setIntegerIntermediate(43);
                if (fail) {
                    BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, methodId, {})), NomadException);
                } else {
                    const auto result = interpreter.runStructMethod(receiver, methodId, {});
                    BOOST_TEST(result.result.getValue().getIntegerValue() == 37);
                    BOOST_TEST(result.receiver.getStorage()[0].getIntegerValue() == 99);
                }

                const auto after = machine->captureExecutionState();
                BOOST_TEST(after.running == before.running);
                BOOST_TEST(after.instructionIndex == before.instructionIndex);
                BOOST_TEST(after.stackIndex == before.stackIndex);
                BOOST_TEST(after.parameterIndex == before.parameterIndex);
                BOOST_TEST(after.variableIndex == before.variableIndex);
                BOOST_TEST(after.receiver.getObjectValue() == before.receiver.getObjectValue());
                BOOST_TEST(machine->getIntegerResult() == 41);
                BOOST_TEST(machine->getIntegerIntermediate() == 43);
                BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
                BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
            }

        }

    }

    machine->pop1();
}

BOOST_AUTO_TEST_CASE(native_struct_reentry_preserves_enclosing_class_receiver_and_borrowed_script_frame) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto probeId = runtime.getTypeManager().registerStruct("HostFrameProbe");
    const auto* probeType = runtime.getTypeByName("HostFrameProbe");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(probeId, "number", runtime.getIntegerType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(probeId);
    auto probes = 0;
    const auto methodId = runtime.getTypeManager().registerNativeStructMethod(probeId, {
        .name = "inspect", .parameters = {}, .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [&](VirtualMachine* active) {
            ++probes;
            const auto* callable = active->getCurrentCallable();
            BOOST_REQUIRE(callable != nullptr);
            const Interpreter interpreter(*active);
            InterpreterContext frame(interpreter, *callable);
            BOOST_TEST(frame.getParameterStorage(0)[0].getIntegerValue() == 5);
            BOOST_TEST(frame.getVariableStorage("local")[0].getIntegerValue() == 3);
            frame.setVariable("local", runtime.getIntegerType(), RuntimeValue{NomadInteger{4}});
            BOOST_CHECK_THROW(static_cast<void>(active->getThis()), VirtualMachineException);
            active->setIntegerResult(frame.getParameterStorage(0)[0].getIntegerValue() +
                frame.getVariableStorage("local")[0].getIntegerValue());
        }
    });
    BOOST_REQUIRE(methodId.isValid());
    const std::array<RuntimeValue, 1> source{RuntimeValue{NomadInteger{7}}};
    const InterpreterValue probe(probeType, source);
    fixture.getCompiler().registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) {
            return;
        }
        const auto* owner = runtime.getTypeByName("EnclosingStructHost");
        BOOST_REQUIRE(owner != nullptr);
        BOOST_REQUIRE(runtime.getTypeManager().registerNativeClassMethod(owner->getId(), {
            .name = "probeStructHost", .parameters = {}, .returnTypeId = runtime.getVoidType()->getId(),
            .function = [&](VirtualMachine* active) {
                const auto* callable = active->getCurrentCallable();
                auto* receiver = active->getThis();
                const Interpreter interpreter(*active);
                const auto result = interpreter.runStructMethod(probe, methodId, {});
                BOOST_TEST(result.result.getValue().getIntegerValue() == 9);
                BOOST_TEST(result.receiver.getStorage()[0].getIntegerValue() == 7);
                BOOST_TEST(active->getThis() == receiver);
                BOOST_TEST(active->getCurrentCallable() == callable);
            }
        }).isValid());
    });
    fixture.addFunction("enclosingStructHostTypes",
        "class EnclosingStructHost\nnumber = 17\n"
        "fun inspect counter:int\nlocal = 3\nthis.probeStructHost\nreturn this.number + local + counter\nend\nend\nreturn 0");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* owner = runtime.getTypeByName("EnclosingStructHost")->asClassType();
    const auto inspect = runtime.resolveMethod(owner, "inspect", {runtime.getIntegerType()});
    BOOST_REQUIRE(inspect.isResolved());
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        const Interpreter interpreter(*machine);
        const auto receiver = interpreter.construct(owner->getConstructors().front()->getId(), {});
        const InterpreterValue counter(runtime.getIntegerType(), RuntimeValue{NomadInteger{5}});
        machine->pushInteger(1234);
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        const auto result = interpreter.runObjectMethod(receiver, inspect.id, {&counter, 1});
        BOOST_TEST(result.getValue().getIntegerValue() == 26);
        BOOST_TEST(probes == 1);
        BOOST_TEST(probe.getStorage()[0].getIntegerValue() == 7);
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        machine->pop1();
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_SUITE_END()
