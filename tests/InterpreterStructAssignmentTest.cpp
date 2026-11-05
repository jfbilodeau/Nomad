#include <LanguageTestFixture.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/TypedValue.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <utility>

using namespace nomad;

namespace {

void addAssignmentTypes(nomad::test::LanguageTestFixture& fixture) {
    fixture.addFunction("assignmentTypes",
        "class AssignmentOwner\nnumber = 11\nend\n"
        "struct AssignmentEmpty\nend\n"
        "struct AssignmentLeaf\nnumber = 17\ntext = assignmentText\n"
        "owner = new AssignmentOwner\nempty = new AssignmentEmpty\nend\n"
        "struct AssignmentRoot\nleaf = new AssignmentLeaf\nsibling = 3\n"
        "fun inspect\ninspectAssignmentFrame\nreturn this.leaf.number\nend\nend\n"
        "class AssignmentHolder\nvalue = new AssignmentRoot\nend\nreturn 0");
}

class FailingAssignmentCopyType final : public ClassType {
public:
    FailingAssignmentCopyType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        FailingAssignmentCopyType*& descriptor
    ):
        ClassType(runtime, id, std::move(name), mode)
    {
        descriptor = this;
    }

    void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override {
        ClassType::copyValue(source, destination);
        if (++copyCount == failCopyAt) {
            throw NomadException("Assignment copy failed");
        }
    }

    mutable NomadIndex copyCount = 0;
    NomadIndex failCopyAt = 0;
};

} // namespace

BOOST_AUTO_TEST_SUITE(interpreter_struct_assignments)

BOOST_AUTO_TEST_CASE(named_roots_copy_nested_fields_and_preserve_shared_references) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto emptyCalls = 0;
    const auto textId = runtime.registerNativeFunction("assignmentText", [](VirtualMachine* machine) {
        machine->setStringResult("owned assignment");
    }, {}, runtime.getStringType(), "");
    const auto inspectId = runtime.registerNativeFunction("inspectAssignmentFrame", [](VirtualMachine*) {},
        {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(textId.isValid() && inspectId.isValid());
    addAssignmentTypes(fixture);
    const auto effectId = runtime.registerNativeFunction("assignmentEmptyEffect", [&](VirtualMachine*) {
        ++emptyCalls;
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(effectId.isValid());
    fixture.addFunction("makeAssignmentEmpty", "assignmentEmptyEffect\nreturn new AssignmentEmpty");
    fixture.addFunction("main",
        "root = new AssignmentRoot\nsource = new AssignmentLeaf\n"
        "root.leaf.number = 29\nroot.leaf = source\nroot.leaf = root.leaf\n"
        "root.leaf.text = \"changed\"\nreturn root.leaf.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    auto machine = runtime.createVirtualMachine();
    for (auto index = 0; index < 3; ++index) {
        {
            InterpreterContext context(*machine);
            const auto execute = [&](const NomadString& source) {
                const auto success = Interpreter{}.execute(source, context);
                BOOST_REQUIRE_MESSAGE(success, (context.getError() ? *context.getError() : source));
            };
            const auto integerResult = [&](const NomadString& source, NomadInteger expected) {
                const auto success = Interpreter{}.evaluateExpression(source, context);
                BOOST_REQUIRE_MESSAGE(success, (context.getError() ? *context.getError() : source));
                BOOST_TEST(context.getResult()->getValue().getIntegerValue() == expected);
            };
            execute("root = new AssignmentRoot\nroot.leaf.number = 29\nsource = new AssignmentLeaf");
            integerResult("root.leaf.number", 29);
            execute("root.leaf = source\nroot.leaf.number = 41\nroot.leaf.text = \"changed\"");
            integerResult("source.number", 17);
            integerResult("root.leaf.number", 41);
            BOOST_REQUIRE(Interpreter{}.evaluateExpression("source.text", context));
            BOOST_TEST(context.getResult()->getValue().getStringValue() == "owned assignment");
            execute("root.leaf = root.leaf");
            BOOST_TEST(context.getResult()->getType() == runtime.getTypeByName("AssignmentLeaf"));
            const TypedValue snapshot(*context.getResult());
            execute("root.leaf.number = 53");
            BOOST_TEST(snapshot.getStorage()[0].getIntegerValue() == 41);
            execute("root.leaf.owner = source.owner\nroot.leaf.owner.number = 67");
            integerResult("source.owner.number", 67);
            execute("root.leaf.empty = makeAssignmentEmpty");
            BOOST_TEST(context.getResult()->getStorage().data() == nullptr);
            BOOST_TEST(context.getResult()->getType() == runtime.getTypeByName("AssignmentEmpty"));
            BOOST_TEST(!Interpreter{}.execute("root.leaf.number = \"wrong\"", context));
            BOOST_TEST(context.getResult() == nullptr);
            BOOST_TEST(!Interpreter{}.execute("root.leaf.missing = 5", context));
            BOOST_TEST(!Interpreter{}.execute("root.leaf.text = source", context));
            integerResult("root.leaf.number", 53);
            execute("holder = new AssignmentHolder");
            BOOST_TEST(!Interpreter{}.execute("holder.value.leaf.number = 7", context));
            BOOST_TEST(!Interpreter{}.execute("(new AssignmentRoot).leaf.number = 7", context));
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(outcome.leakedStrings == 0);
    }
    BOOST_TEST(emptyCalls == 3);
}

BOOST_AUTO_TEST_CASE(frame_updates_preserve_rhs_sibling_changes_and_original_caller_copies) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto textId = runtime.registerNativeFunction("assignmentText", [](VirtualMachine* machine) {
        machine->setStringResult("frame assignment");
    }, {}, runtime.getStringType(), "");
    BOOST_REQUIRE(textId.isValid());
    auto visits = 0;
    const auto siblingId = runtime.registerNativeFunction("changeAssignmentSibling", [&](VirtualMachine* machine) {
        const auto* callable = machine->getCurrentCallable();
        InterpreterContext nested(*machine, *callable);
        const auto* source = callable->getClassType() == nullptr ? "local.sibling = 71" : "this.sibling = 71";
        const auto success = Interpreter{}.execute(source, nested);
        BOOST_REQUIRE_MESSAGE(success, (nested.getError() ? *nested.getError() : source));
        machine->setIntegerResult(43);
    }, {}, runtime.getIntegerType(), "");
    const auto inspectId = runtime.registerNativeFunction("inspectAssignmentFrame", [&](VirtualMachine* machine) {
        const auto* callable = machine->getCurrentCallable();
        BOOST_REQUIRE(callable != nullptr);
        const auto instructionIndex = machine->getInstructionIndex();
        const auto variableCount = callable->getVariableCount();
        const auto savedResult = machine->getResult();
        const auto savedIntermediate = machine->getIntermediate();
        InterpreterContext context(*machine, *callable);
        const auto* source = callable->getClassType() == nullptr ?
            "value.leaf.number = 31\nlocal.leaf.number = changeAssignmentSibling" :
            "this.leaf.number = changeAssignmentSibling";
        const auto success = Interpreter{}.execute(source, context);
        BOOST_REQUIRE_MESSAGE(success, (context.getError() ? *context.getError() : source));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 43);
        const auto* sibling = callable->getClassType() == nullptr ? "local.sibling" : "this.sibling";
        BOOST_REQUIRE(Interpreter{}.evaluateExpression(sibling, context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 71);
        BOOST_TEST(machine->getCurrentCallable() == callable);
        BOOST_TEST(machine->getInstructionIndex() == instructionIndex);
        BOOST_TEST(machine->getResult().getIntegerValue() == savedResult.getIntegerValue());
        BOOST_TEST(machine->getIntermediate().getIntegerValue() == savedIntermediate.getIntegerValue());
        BOOST_TEST(callable->getVariableCount() == variableCount);
        ++visits;
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(siblingId.isValid() && inspectId.isValid());
    addAssignmentTypes(fixture);
    fixture.addFunction("assignmentFrame",
        "params value:AssignmentRoot\nlocal = value\ninspectAssignmentFrame\n"
        "assert value.leaf.number == 31\nassert local.sibling == 71\nreturn local.leaf.number");
    fixture.addFunction("main",
        "root = new AssignmentRoot\nassert (assignmentFrame root) == 43\n"
        "assert root.leaf.number == 17\nassert root.sibling == 3\n"
        "assert root.inspect == 43\nassert root.sibling == 71\nreturn root.leaf.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 43);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(visits == 6);
}

BOOST_AUTO_TEST_CASE(empty_receiver_writes_preserve_effects_without_storage_pointers) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto effects = 0;
    const auto effectId = runtime.registerNativeFunction("assignmentEmptyEffect", [&](VirtualMachine*) {
        ++effects;
    }, {}, runtime.getVoidType(), "");
    const auto inspectId = runtime.registerNativeFunction("inspectEmptyAssignment", [](VirtualMachine* machine) {
        const auto* callable = machine->getCurrentCallable();
        BOOST_REQUIRE(callable != nullptr);
        BOOST_TEST(!callable->getCallStorageLayout().receiverOffset.has_value());
        InterpreterContext context(*machine, *callable);
        const auto success = Interpreter{}.execute("this.empty = makeEmptyAssignment", context);
        BOOST_REQUIRE_MESSAGE(success, (context.getError() ? *context.getError() : ""));
        BOOST_TEST(context.getResult()->getStorage().data() == nullptr);
        BOOST_TEST(context.getResult()->getType()->getTypeName() == "EmptyAssignment");
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(effectId.isValid() && inspectId.isValid());
    fixture.addFunction("types",
        "struct EmptyAssignment\nend\nstruct EmptyAssignmentReceiver\nempty = new EmptyAssignment\n"
        "init\ninspectEmptyAssignment\nend\n"
        "fun inspect\ninspectEmptyAssignment\nreturn 17\nend\nend\nreturn 0");
    fixture.addFunction("makeEmptyAssignment", "assignmentEmptyEffect\nreturn new EmptyAssignment");
    fixture.addFunction("main", "value = new EmptyAssignmentReceiver\nreturn value.inspect");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(effects == 6);
}

BOOST_AUTO_TEST_CASE(failed_copying_before_commit_preserves_the_destination) {
    Runtime runtime;
    FailingAssignmentCopyType* field = nullptr;
    const auto fieldId = runtime.getTypeManager().registerClass<FailingAssignmentCopyType>(
        "AssignmentCopy", ClassMode::Closed, field);
    BOOST_REQUIRE(fieldId.isValid());
    const auto pairId = runtime.registerStruct("AssignmentPair");
    const auto* pair = runtime.getTypeByName("AssignmentPair");
    BOOST_REQUIRE(runtime.registerProperty(pairId, "first", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "second", field).isValid());
    const auto rootId = runtime.registerStruct("AssignmentCopyRoot");
    const auto* root = runtime.getTypeByName("AssignmentCopyRoot");
    BOOST_REQUIRE(runtime.registerProperty(rootId, "pair", pair).isValid());
    runtime.finalizeCompositeFieldLayout(rootId);
    auto machine = runtime.createVirtualMachine();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        {
            InterpreterContext context(*machine);
            std::array<RuntimeValue, 2> oldValues{
                RuntimeValue{NomadString{"old first"}}, RuntimeValue{NomadString{"old second"}}};
            std::array<RuntimeValue, 2> newValues{
                RuntimeValue{NomadString{"new first"}}, RuntimeValue{NomadString{"new second"}}};
            context.setVariable("root", root, oldValues);
            context.setVariable("source", pair, newValues);
            root->freeValue(oldValues.data());
            pair->freeValue(newValues.data());
            const auto live = RuntimeValue::getLiveStringCount();
            for (const auto failAt : {2, 6, 8, 10}) {
                field->copyCount = 0;
                field->failCopyAt = failAt;
                BOOST_TEST(!Interpreter{}.execute("root.pair = source", context));
                BOOST_REQUIRE(context.getError() != nullptr);
                BOOST_TEST(context.getError()->find("Assignment copy failed") != NomadString::npos);
                BOOST_TEST(context.getResult() == nullptr);
                BOOST_TEST(context.getVariableStorage("root")[0].getStringValue() == "old first");
                BOOST_TEST(context.getVariableStorage("root")[1].getStringValue() == "old second");
                BOOST_TEST(context.getVariableStorage("source")[0].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(context.getVariableStorage("source")[1].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            }
            field->failCopyAt = 0;
            BOOST_REQUIRE(Interpreter{}.execute("root.pair = source", context));
            BOOST_TEST(context.getVariableStorage("root")[0].getStringValue() == "new first");
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_SUITE_END()
