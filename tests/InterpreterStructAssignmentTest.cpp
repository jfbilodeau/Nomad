#include <LanguageTestFixture.hpp>

#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/interpreter/InterpreterValue.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <utility>
#include <vector>

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
            freeValue(destination);
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
            const Interpreter contextEvaluator(*machine);
            InterpreterContext context(contextEvaluator);
            const auto execute = [&](const NomadString& source) {
                const auto success = context.getInterpreter().execute(source, context);
                BOOST_REQUIRE_MESSAGE(success, (context.getFirstError() ? context.getFirstError()->message : source));
            };
            const auto integerResult = [&](const NomadString& source, NomadInteger expected) {
                const auto success = context.getInterpreter().execute(source, context);
                BOOST_REQUIRE_MESSAGE(success, (context.getFirstError() ? context.getFirstError()->message : source));
                BOOST_TEST(context.getResult()->getValue().getIntegerValue() == expected);
            };
            execute("root = new AssignmentRoot\nroot.leaf.number = 29\nsource = new AssignmentLeaf");
            integerResult("root.leaf.number", 29);
            execute("root.leaf = source\nroot.leaf.number = 41\nroot.leaf.text = \"changed\"");
            integerResult("source.number", 17);
            integerResult("root.leaf.number", 41);
            BOOST_REQUIRE(context.getInterpreter().execute("source.text", context));
            BOOST_TEST(context.getResult()->getValue().getStringValue() == "owned assignment");
            execute("root.leaf = root.leaf");
            BOOST_TEST(context.getResult()->getType() == runtime.getTypeByName("AssignmentLeaf"));
            const InterpreterValue snapshot(*context.getResult());
            execute("root.leaf.number = 53");
            BOOST_TEST(snapshot.getStorage()[0].getIntegerValue() == 41);
            execute("root.leaf.owner = source.owner\nroot.leaf.owner.number = 67");
            integerResult("source.owner.number", 67);
            execute("root.leaf.empty = makeAssignmentEmpty");
            BOOST_TEST(context.getResult()->getStorage().data() == nullptr);
            BOOST_TEST(context.getResult()->getType() == runtime.getTypeByName("AssignmentEmpty"));
            BOOST_TEST(!context.getInterpreter().execute("root.leaf.number = \"wrong\"", context));
            BOOST_TEST(context.getResult() == nullptr);
            BOOST_TEST(!context.getInterpreter().execute("root.leaf.missing = 5", context));
            BOOST_TEST(!context.getInterpreter().execute("root.leaf.text = source", context));
            integerResult("root.leaf.number", 53);
            execute("holder = new AssignmentHolder");
            execute("holder.value.leaf.number = 7");
            integerResult("holder.value.leaf.number", 7);
            execute("holder.value.leaf = source\nholder.value = root");
            integerResult("holder.value.leaf.number", 53);
            integerResult("(new AssignmentRoot).leaf.number = 7", 7);
            integerResult("root.leaf.number", 53);
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
        const Interpreter nestedEvaluator(*machine);
        InterpreterContext nested(nestedEvaluator, *callable);
        const auto* source = callable->getClassType() == nullptr ? "local.sibling = 71" : "this.sibling = 71";
        const auto success = nested.getInterpreter().execute(source, nested);
        BOOST_REQUIRE_MESSAGE(success, (nested.getFirstError() ? nested.getFirstError()->message : source));
        machine->setIntegerResult(43);
    }, {}, runtime.getIntegerType(), "");
    const auto inspectId = runtime.registerNativeFunction("inspectAssignmentFrame", [&](VirtualMachine* machine) {
        const auto* callable = machine->getCurrentCallable();
        BOOST_REQUIRE(callable != nullptr);
        const auto instructionIndex = machine->getInstructionIndex();
        const auto variableCount = callable->getVariableCount();
        const auto savedResult = machine->getResult();
        const auto savedIntermediate = machine->getIntermediate();
        const Interpreter contextEvaluator(*machine);
        InterpreterContext context(contextEvaluator, *callable);
        const auto* rootType = callable->getClassType() == nullptr ?
            context.getVariableType("local") : callable->getClassType();
        const auto siblingPropertyId = rootType->asCompositeType()->getProperty("sibling")->getId();
        const InterpreterValue alternate(runtime.getIntegerType(), RuntimeValue{NomadInteger{5}});
        const InterpreterValue original(runtime.getIntegerType(), RuntimeValue{NomadInteger{3}});
        if (callable->getClassType() != nullptr) {
            context.setReceiverProperty(siblingPropertyId, alternate);
            context.setReceiverProperty("sibling", original);
        } else {
            context.setVariableProperty("local", siblingPropertyId, alternate);
            context.setVariableProperty("local", "sibling", original);
            context.setVariableProperty("value", siblingPropertyId, alternate);
            context.setVariableProperty("value", "sibling", original);
        }
        const auto* source = callable->getClassType() == nullptr ?
            "value.leaf.number = 31\nlocal.leaf.number = changeAssignmentSibling" :
            "this.leaf.number = changeAssignmentSibling";
        const auto success = context.getInterpreter().execute(source, context);
        BOOST_REQUIRE_MESSAGE(success, (context.getFirstError() ? context.getFirstError()->message : source));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 43);
        const auto* sibling = callable->getClassType() == nullptr ? "local.sibling" : "this.sibling";
        BOOST_REQUIRE(context.getInterpreter().execute(sibling, context));
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
        const Interpreter contextEvaluator(*machine);
        InterpreterContext context(contextEvaluator, *callable);
        const auto* property = callable->getClassType()->asCompositeType()->getProperty("empty");
        const InterpreterValue emptyValue(property->getValueType(), std::span<const RuntimeValue>{});
        const InterpreterValue receiver(callable->getClassType(), context.getReceiverStorage());
        const auto updatedById = contextEvaluator.setProperty(receiver, property->getId(), emptyValue);
        const auto updatedByName = contextEvaluator.setProperty(receiver, "empty", emptyValue);
        BOOST_TEST(updatedById.getType() == receiver.getType());
        BOOST_TEST(updatedByName.getType() == receiver.getType());
        BOOST_TEST(updatedById.getStorage().data() == nullptr);
        BOOST_TEST(updatedByName.getStorage().data() == nullptr);
        BOOST_TEST(contextEvaluator.getProperty(updatedById, property->getId()).getType() == emptyValue.getType());
        BOOST_TEST(contextEvaluator.getProperty(updatedByName, "empty").getStorage().data() == nullptr);
        context.getInterpreter().setProperty(*callable->getClassType()->asStructType(), {},
            property->getId(), emptyValue);
        context.getInterpreter().setProperty(*callable->getClassType()->asStructType(), {},
            "empty", emptyValue);
        context.setReceiverProperty(property->getId(), emptyValue);
        context.setReceiverProperty("empty", emptyValue);
        const auto success = context.getInterpreter().execute("this.empty = makeEmptyAssignment", context);
        BOOST_REQUIRE_MESSAGE(success, (context.getFirstError() ? context.getFirstError()->message : ""));
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
    const auto pairId = runtime.getTypeManager().registerStruct("AssignmentPair");
    const auto* pair = runtime.getTypeByName("AssignmentPair");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(pairId, "first", field).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(pairId, "second", field).isValid());
    const auto rootId = runtime.getTypeManager().registerStruct("AssignmentCopyRoot");
    const auto* root = runtime.getTypeByName("AssignmentCopyRoot");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(rootId, "pair", pair).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(rootId);
    auto machine = runtime.createVirtualMachine();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        {
            const Interpreter contextEvaluator(*machine);
            InterpreterContext context(contextEvaluator);
            std::array<RuntimeValue, 2> oldValues{
                RuntimeValue{NomadString{"old first"}}, RuntimeValue{NomadString{"old second"}}};
            std::array<RuntimeValue, 2> newValues{
                RuntimeValue{NomadString{"new first"}}, RuntimeValue{NomadString{"new second"}}};
            context.setVariable("root", root, oldValues);
            context.setVariable("source", pair, newValues);
            pair->freeValue(newValues.data());
            field->copyCount = 0;
            field->failCopyAt = 0;
            BOOST_REQUIRE(context.getInterpreter().execute("root.pair = source", context));
            const auto copyStages = field->copyCount;
            BOOST_REQUIRE(copyStages > 0);
            context.clearResult();
            context.setVariable("root", root, oldValues);
            root->freeValue(oldValues.data());
            const auto live = RuntimeValue::getLiveStringCount();
            for (NomadIndex failAt = 1; failAt <= copyStages; ++failAt) {
                field->copyCount = 0;
                field->failCopyAt = failAt;
                BOOST_TEST(!context.getInterpreter().execute("root.pair = source", context));
                BOOST_REQUIRE(context.getFirstError().has_value());
                BOOST_TEST(context.getFirstError()->message.find("Assignment copy failed") != NomadString::npos);
                BOOST_TEST(context.getResult() == nullptr);
                BOOST_TEST(context.getVariableStorage("root")[0].getStringValue() == "old first");
                BOOST_TEST(context.getVariableStorage("root")[1].getStringValue() == "old second");
                BOOST_TEST(context.getVariableStorage("source")[0].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(context.getVariableStorage("source")[1].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            }
            field->failCopyAt = 0;
            const InterpreterValue snapshot(root, context.getVariableStorage("root"));
            const InterpreterValue replacement(pair, context.getVariableStorage("source"));
            field->copyCount = 0;
            const auto updated = contextEvaluator.setProperty(snapshot, "pair", replacement);
            const auto hostCopyStages = field->copyCount;
            BOOST_REQUIRE(hostCopyStages > 0);
            BOOST_TEST(updated.getStorage()[0].getStringValue() == "new first");
            for (NomadIndex failAt = 1; failAt <= hostCopyStages; ++failAt) {
                field->copyCount = 0;
                field->failCopyAt = failAt;
                BOOST_CHECK_THROW(
                    static_cast<void>(contextEvaluator.setProperty(snapshot, "pair", replacement)),
                    NomadException
                );
                BOOST_TEST(snapshot.getStorage()[0].getStringValue() == "old first");
                BOOST_TEST(snapshot.getStorage()[1].getStringValue() == "old second");
                BOOST_TEST(replacement.getStorage()[0].getStringValue() == "new first");
                BOOST_TEST(replacement.getStorage()[1].getStringValue() == "new second");
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            }

            field->failCopyAt = 0;
            BOOST_REQUIRE(context.getInterpreter().execute("root.pair = source", context));
            BOOST_TEST(context.getVariableStorage("root")[0].getStringValue() == "new first");
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(stored_field_writes_do_not_copy_unrelated_fields) {
    Runtime runtime;
    FailingAssignmentCopyType* reference = nullptr;
    const auto referenceId = runtime.getTypeManager().registerClass<FailingAssignmentCopyType>(
        "AssignmentUnreadReference", ClassMode::Closed, reference);
    BOOST_REQUIRE(referenceId.isValid());
    const auto leafId = runtime.getTypeManager().registerStruct("AssignmentLiveLeaf");
    const auto* leaf = runtime.getTypeByName("AssignmentLiveLeaf")->asStructType();
    const auto numberId = runtime.getTypeManager().registerScriptStructProperty(leafId, "number", runtime.getIntegerType());
    BOOST_REQUIRE(numberId.isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(leafId, "reference", reference).isValid());
    const auto rootId = runtime.getTypeManager().registerStruct("AssignmentLiveRoot");
    const auto* root = runtime.getTypeByName("AssignmentLiveRoot")->asStructType();
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(rootId, "leaf", leaf).isValid());
    const auto siblingId = runtime.getTypeManager().registerScriptStructProperty(rootId, "sibling", runtime.getIntegerType());
    BOOST_REQUIRE(siblingId.isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(rootId, "reference", reference).isValid());
    const auto ownerId = runtime.getTypeManager().registerClass("AssignmentLiveOwner", ClassMode::Closed);
    const auto* owner = runtime.getTypeByName("AssignmentLiveOwner")->asClassType();
    const auto rootPropertyId = runtime.getTypeManager().registerScriptClassProperty(ownerId, "value", root);
    BOOST_REQUIRE(rootPropertyId.isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(ownerId);
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        {
            InterpreterContext context(interpreter);
            std::array<RuntimeValue, 4> source{
                RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadString{"leaf reference"}},
                RuntimeValue{NomadInteger{3}}, RuntimeValue{NomadString{"root reference"}}};
            context.setVariable("root", root, source);
            const InterpreterValue snapshot(root, source);
            root->freeValue(source.data());
            RuntimeValue object;
            object.setObjectValue(new ScriptObject(owner));
            const auto objectReceiver = interpreter.createObjectReceiver(*owner, object.getObjectValue());
            objectReceiver->setProperty(rootPropertyId, snapshot);
            context.setVariable("holder", owner, object);
            object.freeObjectValue();
            const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{29}});
            const auto storage = context.getMutableVariableStorageById(context.getCallable()->getVariableId("root"));
            const auto rootReceiver = interpreter.createStructReceiver(*root, storage.data());
            const auto leafReceiver = interpreter.createStructReceiver(*leaf, storage.data());
            reference->copyCount = 0;
            reference->failCopyAt = 1;
            context.setVariableProperty("root", siblingId, number);
            context.setVariableProperty("root", "leaf.number", number);
            rootReceiver->setProperty("leaf.number", number);
            leafReceiver->setProperty(numberId, number);
            objectReceiver->setProperty("value.leaf.number", number);
            interpreter.setProperty(*root, storage, "leaf.number", number);
            interpreter.setProperty(*leaf, storage.first(leaf->getSize()), numberId, number);
            BOOST_REQUIRE_MESSAGE(interpreter.execute("root.leaf.number = 41", context),
                (context.getFirstError() ? context.getFirstError()->message : ""));
            BOOST_REQUIRE_MESSAGE(interpreter.execute("holder.value.leaf.number = 53", context),
                (context.getFirstError() ? context.getFirstError()->message : ""));
            BOOST_TEST(reference->copyCount == 0U);
            BOOST_TEST(storage[0].getIntegerValue() == 41);
            BOOST_TEST(storage[2].getIntegerValue() == 29);
            BOOST_TEST(snapshot.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(snapshot.getStorage()[2].getIntegerValue() == 3);
            BOOST_REQUIRE(interpreter.execute("holder.value.leaf.number", context));
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 53);
            BOOST_TEST(reference->copyCount == 0U);
            BOOST_CHECK_THROW(interpreter.setProperty(*root, storage.first(1), siblingId, number),
                std::invalid_argument);
            BOOST_CHECK_THROW(interpreter.setProperty(*root, storage, numberId, number), std::invalid_argument);
            for (const auto* path : {"", ".leaf", "leaf.", "leaf..number", "missing", "sibling.number"}) {
                BOOST_CHECK_THROW(interpreter.setProperty(*root, storage, path, number), std::invalid_argument);
                BOOST_CHECK_THROW(static_cast<void>(interpreter.getProperty(snapshot, path)), std::invalid_argument);
                BOOST_CHECK_THROW(static_cast<void>(interpreter.setProperty(snapshot, path, number)), std::invalid_argument);
            }
            const InterpreterValue wrongType(runtime.getBooleanType(), RuntimeValue{NomadBoolean{true}});
            BOOST_CHECK_THROW(interpreter.setProperty(*root, storage, "leaf.number", wrongType),
                std::invalid_argument);
            BOOST_CHECK_THROW(static_cast<void>(interpreter.setProperty(snapshot, "leaf.number", wrongType)),
                std::invalid_argument);
            BOOST_CHECK_THROW(static_cast<void>(interpreter.setProperty(snapshot, siblingId, wrongType)),
                std::invalid_argument);
            BOOST_CHECK_THROW(static_cast<void>(interpreter.getProperty(number, "number")), std::invalid_argument);
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 53);
            BOOST_TEST(storage[0].getIntegerValue() == 41);
            BOOST_TEST(reference->copyCount == 0U);
            reference->failCopyAt = 0;
            BOOST_REQUIRE(interpreter.execute("holder.value = root", context));
            BOOST_TEST(objectReceiver->getProperty("value.leaf.number").getValue().getIntegerValue() == 41);
            BOOST_REQUIRE(interpreter.execute("root.leaf.number = 67", context));
            BOOST_TEST(objectReceiver->getProperty("value.leaf.number").getValue().getIntegerValue() == 41);
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(assignments_rebind_struct_roots_and_retain_object_and_temporary_owners) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    InterpreterContext* activeContext = nullptr;
    NomadString duringRight;
    auto clearRoot = false;
    auto failRight = false;
    std::vector<NomadString> order;
    const auto textId = runtime.registerNativeFunction("assignmentText", [](VirtualMachine* machine) {
        machine->setStringResult("retained assignment");
    }, {}, runtime.getStringType(), "");
    const auto inspectId = runtime.registerNativeFunction("inspectAssignmentFrame", [](VirtualMachine*) {},
        {}, runtime.getVoidType(), "");
    const auto rightId = runtime.registerNativeFunction("assignmentRight", [&](VirtualMachine* machine) {
        order.push_back("right");
        if (!duringRight.empty()) {
            BOOST_REQUIRE_MESSAGE(activeContext->getInterpreter().execute(duringRight, *activeContext),
                (activeContext->getFirstError() ? activeContext->getFirstError()->message : ""));
        }
        if (clearRoot) {
            activeContext->clearVariableValue("root");
        }
        if (failRight) {
            throw NomadException("Assignment RHS failed");
        }
        machine->setIntegerResult(43);
    }, {}, runtime.getIntegerType(), "");
    const auto ownerId = runtime.registerNativeFunction("assignmentOwnerEffect", [&](VirtualMachine*) {
        order.push_back("owner");
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(textId.isValid() && inspectId.isValid() && rightId.isValid() && ownerId.isValid());
    addAssignmentTypes(fixture);
    fixture.addFunction("makeAssignmentTemporary", "assignmentOwnerEffect\nreturn new AssignmentRoot");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        {
            InterpreterContext context(interpreter);
            activeContext = &context;
            const auto execute = [&](const NomadString& source) {
                BOOST_REQUIRE_MESSAGE(interpreter.execute(source, context),
                    (context.getFirstError() ? context.getFirstError()->message : source));
            };
            execute("root = new AssignmentRoot\nholder = new AssignmentHolder\nsaved = holder");
            duringRight = "root = new AssignmentRoot\nroot.sibling = 71";
            execute("root.leaf.number = assignmentRight");
            execute("root.leaf.number");
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 43);
            execute("root.sibling");
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 71);
            duringRight = "root = 5";
            BOOST_TEST(!interpreter.execute("root.leaf.number = assignmentRight", context));
            BOOST_REQUIRE(context.getFirstError().has_value());
            BOOST_TEST(context.getFirstError()->message.find("changed type") != NomadString::npos);
            BOOST_TEST(context.getResult() == nullptr);
            BOOST_TEST(context.getVariableValue("root")->getIntegerValue() == 5);
            duringRight = "";
            clearRoot = true;
            execute("root = new AssignmentRoot");
            BOOST_TEST(!interpreter.execute("root.leaf.number = assignmentRight", context));
            BOOST_REQUIRE(context.getFirstError().has_value());
            BOOST_TEST(context.getFirstError()->message.find("not been assigned") != NomadString::npos);
            BOOST_TEST(context.getVariableType("root") == nullptr);
            clearRoot = false;
            duringRight = "holder = new AssignmentHolder";
            execute("holder.value.leaf.number = assignmentRight");
            execute("saved.value.leaf.number");
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 43);
            execute("holder.value.leaf.number");
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 17);
            duringRight = "";
            order.clear();
            execute("(makeAssignmentTemporary).leaf.number = assignmentRight");
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 43);
            BOOST_REQUIRE(order.size() == 2U);
            BOOST_TEST(order[0] == "owner");
            BOOST_TEST(order[1] == "right");
            failRight = true;
            BOOST_TEST(!interpreter.execute("(makeAssignmentTemporary).leaf.number = assignmentRight", context));
            BOOST_REQUIRE(context.getFirstError().has_value());
            BOOST_TEST(context.getFirstError()->message.find("Assignment RHS failed") != NomadString::npos);
            BOOST_TEST(context.getResult() == nullptr);
            failRight = false;
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_SUITE_END()
