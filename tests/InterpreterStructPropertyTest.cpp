#include <LanguageTestFixture.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/InterpreterValue.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <span>
#include <utility>
#include <vector>

using namespace nomad;

namespace {

void addPropertySources(nomad::test::LanguageTestFixture& fixture) {
    fixture.addFunction("propertyTypes",
        "class ReadOwner\nnumber = 11\nend\n"
        "struct ReadEmpty\nend\n"
        "struct ReadSingle\nnumber = 23\nend\n"
        "struct ReadLeaf\nempty = new ReadEmpty\nnumber = 17\ntext = readOwnedText\nowner = new ReadOwner\nend\n"
        "struct ReadOuter\nprefix = 3\nleaf = new ReadLeaf\nsingle = new ReadSingle\n"
        "label = readOwnedText\nempty = new ReadEmpty\n"
        "fun inspect\ninspectReadFrame\nreturn this.leaf.number\nend\nend\n"
        "class ReadHolder\nvalue = new ReadOuter\nempty = new ReadEmpty\nsingle = new ReadSingle\nend\n"
        "struct ReadEmptyRoot\nempty = new ReadEmpty\ninit\nreadEmptyEffect\nend\nend\n"
        "return 0");
    fixture.addFunction("makeReadOuter", "readRootEffect\nreturn new ReadOuter");
    fixture.addFunction("makeReadHolder", "readRootEffect\nreturn new ReadHolder");
    fixture.addFunction("readLeafNumber", "params leaf:ReadLeaf\nreturn leaf.number");
    fixture.addFunction("readOuterFrame",
        "params value:ReadOuter empty:ReadEmpty tail:int\nlocal = value\ninspectReadFrame\n"
        "return local.leaf.number + tail");
}

} // namespace

BOOST_AUTO_TEST_SUITE(interpreter_struct_properties)

BOOST_AUTO_TEST_CASE(reads_preserve_nested_copies_reference_ownership_and_temporary_lifetimes) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto roots = 0;
    auto emptyEffects = 0;
    const auto textId = runtime.registerNativeFunction("readOwnedText", [](VirtualMachine* machine) {
        machine->setStringResult("owned property");
    }, {}, runtime.getStringType(), "");
    const auto rootId = runtime.registerNativeFunction("readRootEffect", [&](VirtualMachine*) {
        ++roots;
    }, {}, runtime.getVoidType(), "");
    const auto effectId = runtime.registerNativeFunction("readEmptyEffect", [&](VirtualMachine*) {
        ++emptyEffects;
    }, {}, runtime.getVoidType(), "");
    const auto inspectId = runtime.registerNativeFunction("inspectReadFrame", [](VirtualMachine*) {},
        {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(textId.isValid() && rootId.isValid() && effectId.isValid() && inspectId.isValid());
    addPropertySources(fixture);
    fixture.addFunction("main",
        "root = new ReadOuter\nholder = new ReadHolder\n"
        "assert root.leaf.number == 17\nassert root.leaf.owner.number == 11\n"
        "assert holder.value.leaf.text == \"owned property\"\n"
        "assert (readLeafNumber holder.value.leaf) == 17\n"
        "single = holder.single\nassert single.number == 23\n"
        "empty = (new ReadEmptyRoot).empty\n"
        "assert (makeReadOuter).leaf.number == 17\n"
        "assert (makeReadHolder).value.leaf.number == 17\nreturn 17");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    auto machine = runtime.createVirtualMachine();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    const auto instructionCount = runtime.getInstructions().size();
    for (auto index = 0; index < 3; ++index) {
        {
            const Interpreter contextEvaluator(*machine);
            InterpreterContext context(contextEvaluator);
            BOOST_CHECK_THROW((void)context.getReceiverStorage(), NomadBug);
            BOOST_REQUIRE(context.getInterpreter().execute("root = new ReadOuter\nholder = new ReadHolder", context));
            const auto root = context.getVariableStorage("root");
            const auto leafProperty = runtime.getTypeByName("ReadOuter")->asCompositeType()->getProperty("leaf");
            const auto leafOffset = leafProperty->getOwnerType()->getFieldOffset(leafProperty->getId());
            const auto ownerProperty = runtime.getTypeByName("ReadLeaf")->asCompositeType()->getProperty("owner");
            const auto ownerOffset = leafOffset + ownerProperty->getOwnerType()->getFieldOffset(ownerProperty->getId());
            for (const auto* source : {"root.leaf.number", "holder.value.leaf.number", "(makeReadOuter).leaf.number",
                "(makeReadHolder).value.leaf.number", "readLeafNumber root.leaf"}) {
                const auto evaluated = context.getInterpreter().execute(source, context);
                BOOST_REQUIRE_MESSAGE(evaluated, (context.getError() ? *context.getError() : source));
                BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 17);
            }
            BOOST_REQUIRE(context.getInterpreter().execute("root.leaf.owner.number", context));
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 11);
            BOOST_REQUIRE(context.getInterpreter().execute("root.leaf.text.toUpper", context));
            BOOST_TEST(context.getResult()->getValue().getStringValue() == "OWNED PROPERTY");
            BOOST_REQUIRE(context.getInterpreter().execute("copy = root.leaf\nsingle = holder.single", context));
            BOOST_TEST(context.getVariableStorage("copy").data() != root.data() + leafOffset);
            BOOST_TEST(context.getVariableStorage("single").size() == 1U);
            BOOST_TEST(context.getVariableType("single")->isStruct());
            BOOST_REQUIRE(context.getInterpreter().execute("single.number", context));
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 23);
            BOOST_REQUIRE(context.getInterpreter().execute("root.leaf.owner", context));
            const InterpreterValue owner(*context.getResult());
            BOOST_TEST(owner.getValue().getObjectValue() == root[ownerOffset].getObjectValue());
            BOOST_REQUIRE(context.getInterpreter().execute("copy.owner.number = 31", context));
            BOOST_REQUIRE(context.getInterpreter().execute("root.leaf.owner.number", context));
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 31);
            BOOST_REQUIRE(context.getInterpreter().execute("holder.value.leaf", context));
            const InterpreterValue leaf(*context.getResult());
            BOOST_TEST(leaf.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(leaf.getStorage()[1].getStringValue() == "owned property");
            for (const auto* source : {"root.empty", "root.leaf.empty", "holder.empty", "(new ReadEmptyRoot).empty",
                "(makeReadOuter).empty"}) {
                const auto evaluated = context.getInterpreter().execute(source, context);
                BOOST_REQUIRE_MESSAGE(evaluated, (context.getError() ? *context.getError() : source));
                BOOST_TEST(context.getResult()->getType() == runtime.getTypeByName("ReadEmpty"));
                BOOST_TEST(context.getResult()->getStorage().empty());
                BOOST_TEST(context.getResult()->getStorage().data() == nullptr);
            }
            BOOST_TEST(!context.getInterpreter().execute("root.missing", context));
            BOOST_TEST(context.hasError());
            BOOST_TEST(context.getResult() == nullptr);
            BOOST_REQUIRE(context.getInterpreter().execute("root.leaf.number = 99", context));
            BOOST_REQUIRE(context.getInterpreter().execute("root.leaf.number", context));
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
            BOOST_REQUIRE(context.getInterpreter().execute("root.leaf = new ReadLeaf", context));
            BOOST_REQUIRE(context.getInterpreter().execute("root.leaf.number", context));
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 17);
            context.clearVariableValue("root");
            context.clearVariableValue("holder");
            context.clearVariableValue("copy");
            context.clearVariableValue("single");
            context.clearResult();
            BOOST_TEST(leaf.getStorage()[1].getStringValue() == "owned property");
            BOOST_TEST(owner.getValue().getObjectValue()->getReferenceCount() == 1U);
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
        {
            const Interpreter contextEvaluator(*machine);
            InterpreterContext context(contextEvaluator);
            for (const auto* source : {"(makeReadOuter).leaf.text", "(makeReadHolder).value.leaf.text"}) {
                const auto rootsBefore = roots;
                BOOST_REQUIRE(context.getInterpreter().execute(source, context));
                BOOST_TEST(roots == rootsBefore + 1);
                BOOST_TEST(context.getResult()->getValue().getStringValue() == "owned property");
                BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore + 1);
                context.clearResult();
                BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
                BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
            }
            const auto effectsBefore = emptyEffects;
            BOOST_REQUIRE(context.getInterpreter().execute("(new ReadEmptyRoot).empty", context));
            BOOST_TEST(emptyEffects == effectsBefore + 1);
            BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
        }
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(runtime.getInstructions().size() == instructionCount);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(reads_support_live_parameters_locals_and_borrowed_struct_receivers) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto visits = 0;
    const auto textId = runtime.registerNativeFunction("readOwnedText", [](VirtualMachine* machine) {
        machine->setStringResult("frame property");
    }, {}, runtime.getStringType(), "");
    const auto rootId = runtime.registerNativeFunction("readRootEffect", [](VirtualMachine*) {},
        {}, runtime.getVoidType(), "");
    const auto emptyId = runtime.registerNativeFunction("readEmptyEffect", [](VirtualMachine*) {},
        {}, runtime.getVoidType(), "");
    const auto inspectId = runtime.registerNativeFunction("inspectReadFrame", [&](VirtualMachine* machine) {
        const auto* callable = machine->getCurrentCallable();
        BOOST_REQUIRE(callable != nullptr);
        const auto instructionIndex = machine->getInstructionIndex();
        const auto instructionCount = runtime.getInstructions().size();
        const auto variables = callable->getVariableCount();
        const auto savedResult = machine->getResult();
        const auto savedIntermediate = machine->getIntermediate();
        const Interpreter contextEvaluator(*machine);
        InterpreterContext context(contextEvaluator, *callable);
        const auto isReceiver = callable->getClassType() != nullptr;
        for (const auto* source : isReceiver
            ? std::array<const char*, 3>{"this.leaf.number", "this.leaf.owner.number", "this.leaf.text"}
            : std::array<const char*, 3>{"value.leaf.number", "local.leaf.owner.number", "value.leaf.text"}) {
            const auto evaluated = context.getInterpreter().execute(source, context);
            BOOST_REQUIRE_MESSAGE(evaluated, (context.getError() ? *context.getError() : source));
        }
        BOOST_TEST(context.getResult()->getValue().getStringValue() == "frame property");
        const auto copied = context.getInterpreter().execute(isReceiver ? "copy = this.leaf" : "copy = local.leaf", context);
        BOOST_REQUIRE_MESSAGE(copied, (context.getError() ? *context.getError() : ""));
        BOOST_TEST(context.getVariableStorage("copy")[0].getIntegerValue() == 17);
        BOOST_REQUIRE(context.getInterpreter().execute(isReceiver ? "this.empty" : "value.empty", context));
        BOOST_TEST(context.getResult()->getStorage().data() == nullptr);
        if (isReceiver) {
            const auto receiver = context.getReceiverStorage();
            BOOST_TEST(receiver.size() == callable->getClassType()->getSize());
            BOOST_REQUIRE(context.getInterpreter().execute("this", context));
            BOOST_TEST(context.getResult()->getStorage().data() != receiver.data());
        }
        BOOST_TEST(machine->getCurrentCallable() == callable);
        BOOST_TEST(machine->getInstructionIndex() == instructionIndex);
        BOOST_TEST(machine->getResult().getIntegerValue() == savedResult.getIntegerValue());
        BOOST_TEST(machine->getIntermediate().getIntegerValue() == savedIntermediate.getIntegerValue());
        BOOST_TEST(runtime.getInstructions().size() == instructionCount);
        BOOST_TEST(callable->getVariableCount() == variables);
        ++visits;
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(textId.isValid() && rootId.isValid() && emptyId.isValid() && inspectId.isValid());
    addPropertySources(fixture);
    fixture.addFunction("main",
        "value = new ReadOuter\n"
        "result = readOuterFrame value (new ReadEmpty) 5\n"
        "assert result == 22\nreturn value.inspect");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(visits == 6);
}

BOOST_AUTO_TEST_CASE(empty_struct_receiver_reads_omit_storage_pointers_but_preserve_execution) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto visits = 0;
    const auto inspectId = runtime.registerNativeFunction("inspectEmptyReadFrame", [&](VirtualMachine* machine) {
        const auto* callable = machine->getCurrentCallable();
        BOOST_REQUIRE(callable != nullptr);
        BOOST_TEST(!callable->getCallStorageLayout().receiverOffset.has_value());
        const Interpreter contextEvaluator(*machine);
        InterpreterContext context(contextEvaluator, *callable);
        BOOST_TEST(context.getReceiverStorage().empty());
        BOOST_TEST(context.getReceiverStorage().data() == nullptr);
        for (const auto* source : {"this", "this.empty"}) {
            const auto evaluated = context.getInterpreter().execute(source, context);
            BOOST_REQUIRE_MESSAGE(evaluated, (context.getError() ? *context.getError() : source));
            BOOST_TEST(context.getResult()->getStorage().empty());
            BOOST_TEST(context.getResult()->getStorage().data() == nullptr);
            BOOST_TEST(context.getResult()->getType()->isStruct());
        }
        ++visits;
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(inspectId.isValid());
    fixture.addFunction("main",
        "struct EmptyReadValue\nend\n"
        "struct EmptyReadReceiver\nempty = new EmptyReadValue\n"
        "init\ninspectEmptyReadFrame\nend\n"
        "fun inspect\ninspectEmptyReadFrame\nreturn 17\nend\nend\n"
        "value = new EmptyReadReceiver\nreturn value.inspect");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(visits == 6);
}

BOOST_AUTO_TEST_SUITE_END()
