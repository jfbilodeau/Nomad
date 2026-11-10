// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/collections/RegisterArrayType.hpp>
#include <nomad/collections/RegisterKeyValueType.hpp>
#include <nomad/collections/RegisterListType.hpp>
#include <nomad/collections/RegisterMapType.hpp>

#include <nomad/compiler/ClassDeclaration.hpp>
#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Source.hpp>
#include <nomad/compiler/StructDeclaration.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Hub.hpp>
#include <nomad/script/InstructionDump.hpp>
#include <nomad/script/NomadObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VariableContext.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

using namespace nomad;

static_assert(std::is_same_v<decltype(std::declval<const ClassDeclaration&>().getType()), const ClassType*>);
static_assert(std::is_same_v<decltype(std::declval<const StructDeclaration&>().getType()), const StructType*>);
static_assert(!std::is_base_of_v<ClassDeclaration, StructDeclaration>);
static_assert(!std::is_base_of_v<ClassMemberDeclaration, StructMemberDeclaration>);

BOOST_AUTO_TEST_SUITE(language_structs)

BOOST_AUTO_TEST_CASE(temporary_rooted_reads_preserve_nested_values_and_release_the_complete_root) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto roots = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "rootCreated",
        [&](VirtualMachine*) { ++roots; },
        {},
        runtime.getVoidType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "ownedText",
        [](VirtualMachine* active) { active->setStringResult("owned"); },
        {},
        runtime.getStringType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "liveObjects",
        [](VirtualMachine* active) {
            active->setIntegerResult(static_cast<NomadInteger>(NomadObject::getLiveObjectCount()));
        },
        {},
        runtime.getIntegerType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "class Owner\nnumber = 11\ntext = ownedText\nend\n"
        "struct Point\nnumber = 7\ntext = ownedText\nowner = new Owner\n"
        "fun copied\nreturn this\nend\nend\n"
        "struct Outer\npoint = new Point\nunrelated = ownedText\ninit\nrootCreated\nend\nend\n"
        "fun makeOuter\nreturn new Outer\nend\n"
        "fun readText\nreturn (makeOuter).point.text\nend\n"
        "fun readOwner\nreturn (makeOuter).point.owner\nend\n"
        "fun readPoint\nreturn (makeOuter).point\nend\n"
        "fun methodPoint\nreturn (makeOuter).point.copied\nend\n"
        "fun takeText value:string\nreturn value == \"owned\"\nend\n"
        "fun takePoint value:Point\nvalue.number = 99\nreturn value.text == \"owned\"\nend\n"
        "before = liveObjects\nindex = 0\n"
        "while index < 3\n"
        "assert (new Outer).point.number == 7\nassert liveObjects == before\n"
        "assert (makeOuter).point.text == \"owned\"\nassert liveObjects == before\n"
        "assert (takeText (makeOuter).point.text)\nassert liveObjects == before\n"
        "assert (takePoint (makeOuter).point)\nassert liveObjects == before\n"
        "assert (makeOuter).point.owner.number == 11\nassert liveObjects == before\n"
        "assert ((makeOuter).point.copied).text == \"owned\"\nassert liveObjects == before\n"
        "index = index + 1\nend\n"
        "number = (makeOuter).point.number + (makeOuter).point.number\n"
        "assert liveObjects == before\n"
        "text = readText\nassert text == \"owned\"\nassert liveObjects == before + 1\n"
        "owner = readOwner\nassert owner.text == \"owned\"\nassert liveObjects == before + 3\n"
        "point = readPoint\npoint.number = 17\nassert point.text == \"owned\"\n"
        "method = methodPoint\nassert method.number == 7\n"
        "return number + point.number + method.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto* readPoint = runtime.getScriptFunction(runtime.getFunctionId("readPoint"));
    BOOST_REQUIRE(readPoint != nullptr);
    BOOST_TEST(readPoint->getVariableCount() == 2U);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        roots = 0;
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 38);
        BOOST_TEST(roots == 24);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(temporary_rooted_writes_and_methods_evaluate_the_root_once_before_arguments) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<NomadInteger> calls;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "record",
        [&](VirtualMachine* active) {
            const auto number = active->getIntegerParameter(0);
            calls.push_back(number);
            active->setIntegerResult(number);
        },
        {defParameter("number", runtime.getIntegerType(), "")},
        runtime.getIntegerType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "class Shared\nnumber = 0\nend\n"
        "struct Point\nnumber = 7\ntext = \"point\"\nshared = new Shared\n"
        "fun move first:int second:int\n"
        "this.number = this.number + first + second\n"
        "this.shared.number = this.number\nrecord 100\nreturn this\nend\nend\n"
        "struct Outer\npoint = new Point\nlabel = \"outer\"\nend\n"
        "fun make shared:Shared\nrecord 1\nvalue = new Outer\nvalue.point.shared = shared\nreturn value\nend\n"
        "fun replacement shared:Shared\nrecord 4\npoint = new Point\npoint.shared = shared\nreturn point\nend\n"
        "fun forwarded shared:Shared\nreturn (make shared).point.move (record 2) (record 3)\nend\n"
        "shared = new Shared\n"
        "(make shared).point.number = record 2\n"
        "(make shared).point = replacement shared\n"
        "(make shared).point.shared.number = record 9\nassert shared.number == 9\n"
        "result = forwarded shared\nassert shared.number == 12\nassert result.number == 12\n"
        "return result.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto* forwarded = runtime.getScriptFunction(runtime.getFunctionId("forwarded"));
    BOOST_REQUIRE(forwarded != nullptr);
    BOOST_TEST(forwarded->getVariableCount() == 1U);
    BOOST_TEST(forwarded->getVariableName(VariableId{0}).starts_with("$propertyReceiver"));
    const std::vector<NomadInteger> expectedCalls{1, 2, 1, 4, 1, 9, 1, 3, 2, 100};
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls.clear();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 12);
        BOOST_TEST(calls == expectedCalls, boost::test_tools::per_element());
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(empty_temporary_properties_preserve_root_and_method_side_effects_without_slots) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<NomadInteger> calls;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "ownedEmptyRootText",
        [](VirtualMachine* active) { active->setStringResult("owned empty root"); },
        {},
        runtime.getStringType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "emptyRootObjects",
        [](VirtualMachine* active) {
            active->setIntegerResult(static_cast<NomadInteger>(NomadObject::getLiveObjectCount()));
        },
        {},
        runtime.getIntegerType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "note",
        [&](VirtualMachine* active) { calls.push_back(active->getIntegerParameter(0)); },
        {defParameter("number", runtime.getIntegerType(), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "struct Empty\nfun touch\nnote 2\nend\nend\n"
        "struct Outer\nempty = new Empty\ninit\nnote 1\nend\nend\n"
        "struct OwnedOuter\nempty = new Empty\ntext = ownedEmptyRootText\ninit\nnote 4\nend\nend\n"
        "fun make\nreturn new Outer\nend\n"
        "fun take value:Empty\nnote 3\nend\n"
        "fun read\nreturn (make).empty\nend\n"
        "(make).empty.touch\n"
        "value = (make).empty\n"
        "take (make).empty\n"
        "(make).empty = read\n"
        "return 17");
    fixture.addFunction("owned",
        "before = emptyRootObjects\n"
        "value = (new OwnedOuter).empty\nassert emptyRootObjects == before\n"
        "(new OwnedOuter).empty.touch\nassert emptyRootObjects == before\n"
        "take (new OwnedOuter).empty\nassert emptyRootObjects == before\n"
        "(new OwnedOuter).empty = (new OwnedOuter).empty\nassert emptyRootObjects == before\n"
        "return 19");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    BOOST_TEST(runtime.getTypeByName("Outer")->getSize() == 0U);
    const auto* main = runtime.getScriptFunction(runtime.getFunctionId("main"));
    BOOST_REQUIRE(main != nullptr);
    BOOST_TEST(main->getVariableStorageSize() == 0U);
    const std::vector<NomadInteger> expectedCalls{1, 2, 1, 1, 3, 1, 1};
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls.clear();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(calls == expectedCalls, boost::test_tools::per_element());
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
        calls.clear();
        const auto owned = fixture.execute("owned");
        BOOST_REQUIRE_MESSAGE(!owned.fault.has_value(), owned.fault.value_or(""));
        BOOST_TEST(owned.integerValue == 19);
        const std::vector<NomadInteger> ownedCalls{4, 4, 2, 4, 3, 4, 4};
        BOOST_TEST(calls == ownedCalls, boost::test_tools::per_element());
        BOOST_TEST(owned.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(free_function_return_forwarding_eliminates_intermediate_storage_and_copies) {
    nomad::test::LanguageTestFixture fixture;
    fixture.addFunction("main",
        "struct Payload\n"
        "number = 0\ntext = \"payload\"\n"
        "init number:int\nthis.number = number\nend\n"
        "fun rebuilt\nreturn build this.number\nend\n"
        "end\n"
        "fun build number:int\nreturn new Payload number\nend\n"
        "fun relay number:int\nreturn build number\nend\n"
        "fun relayAgain number:int\nreturn relay number\nend\n"
        "fun identity value:Payload\nreturn value\nend\n"
        "fun fromParameter value:Payload\nreturn identity value\nend\n"
        "fun methodResult value:Payload\nreturn value.rebuilt\nend\n"
        "original = new Payload 7\n"
        "copy = relayAgain 9\nparameterCopy = fromParameter original\n"
        "rebuilt = original.rebuilt\nfromMethod = methodResult original\n"
        "original.number = 99\n"
        "assert copy.text == \"payload\"\nassert parameterCopy.number == 7\n"
        "assert rebuilt.number == 7\nassert fromMethod.number == 7\n"
        "return copy.number + parameterCopy.number + rebuilt.number + fromMethod.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    for (const auto* name : {"build", "relay", "relayAgain", "fromParameter", "methodResult"}) {
        const auto* wrapper = runtime.getScriptFunction(runtime.getFunctionId(name));
        BOOST_REQUIRE(wrapper != nullptr);
        BOOST_TEST(wrapper->getVariableCount() == 0U);
        BOOST_TEST(wrapper->getVariableStorageSize() == 0U);
        std::ostringstream dump;
        dumpInstructions(runtime, *wrapper, dump);
        BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_address_push) != NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_local_address_push) == NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_set) == NomadString::npos);
    }
    for (const auto* name : {"identity"}) {
        const auto* copying = runtime.getScriptFunction(runtime.getFunctionId(name));
        BOOST_REQUIRE(copying != nullptr);
        BOOST_TEST(copying->getVariableCount() == 1U);
        BOOST_TEST(copying->getVariableStorageSize() == 2U);
        BOOST_TEST(copying->getVariableName(VariableId{0}).starts_with("$return"));
        BOOST_TEST(copying->getVisibleVariableName(VariableId{0}).empty());
    }
    const auto* type = runtime.getTypeByName("Payload")->asStructType();
    const auto methods = type->getMethods();
    const auto rebuilt = std::ranges::find_if(methods, [](const auto* candidate) {
        return candidate->getName() == "rebuilt";
    });
    BOOST_REQUIRE(rebuilt != methods.end());
    const auto* callable = (*rebuilt)->asScriptCallable();
    BOOST_REQUIRE(callable != nullptr);
    BOOST_TEST(callable->getVariableCount() == 0U);
    BOOST_REQUIRE(callable->getCallStorageLayout().returnOffset.has_value());
    BOOST_TEST(*callable->getCallStorageLayout().returnOffset == 1U);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 30);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(empty_free_function_return_forwarding_omits_storage_and_preserves_calls) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto calls = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "note",
        [&](VirtualMachine*) { ++calls; },
        {},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "struct Empty\ninit\nnote\nend\nend\n"
        "fun leaf\nreturn new Empty\nend\n"
        "fun forward\nreturn leaf\nend\n"
        "fun chain\nreturn forward\nend\n"
        "value = chain\nchain\nreturn 17");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    for (const auto* name : {"leaf", "forward", "chain"}) {
        const auto* wrapper = runtime.getScriptFunction(runtime.getFunctionId(name));
        BOOST_REQUIRE(wrapper != nullptr);
        BOOST_TEST(wrapper->getVariableCount() == 0U);
        BOOST_TEST(wrapper->getVariableStorageSize() == 0U);
        BOOST_TEST(!wrapper->getCallStorageLayout().returnOffset.has_value());
        std::ostringstream dump;
        dumpInstructions(runtime, *wrapper, dump);
        BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_address_push) == NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_local_address_push) == NomadString::npos);
    }
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls = 0;
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(calls == 2);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(construction_returns_preserve_initialization_order_early_returns_and_borrowed_receivers) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<NomadInteger> calls;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "record",
        [&](VirtualMachine* machine) {
            const auto value = machine->getIntegerParameter(0);
            calls.push_back(value);
            machine->setIntegerResult(value);
        },
        {defParameter("value", runtime.getIntegerType(), "")},
        runtime.getIntegerType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "struct Payload\n"
        "number = record 10\ntext = \"default\"\n"
        "init number:int text:string\n"
        "assert this.number == 10\nassert this.text == \"default\"\n"
        "this.number = number\nthis.text = text\nrecord 20\nend\n"
        "init other:Payload\n"
        "assert this.number == 10\nthis.number = other.number\nthis.text = other.text\n"
        "other.number = 99\nrecord 30\nend\n"
        "fun create amount:int\nreturn new Payload (this.number + amount) this.text\nend\n"
        "end\n"
        "fun textArgument\nrecord 2\nreturn \"owned\"\nend\n"
        "fun make early:bool\n"
        "if early\nreturn new Payload (record 1) (textArgument)\nend\n"
        "return new Payload (record 3) (textArgument)\nend\n"
        "fun duplicate value:Payload\nreturn new Payload value\nend\n"
        "first = make true\nsecond = make false\ncopy = duplicate first\n"
        "created = first.create 4\n"
        "assert first.number == 1\nassert second.number == 3\n"
        "assert copy.number == 1\nassert created.number == 5\n"
        "assert first.text == \"owned\"\nassert created.text == \"owned\"\n"
        "return first.number + second.number + copy.number + created.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    for (const auto* name : {"make", "duplicate"}) {
        const auto* callable = runtime.getScriptFunction(runtime.getFunctionId(name));
        BOOST_REQUIRE(callable != nullptr);
        BOOST_TEST(callable->getVariableCount() == 0U);
        BOOST_TEST(callable->getVariableStorageSize() == 0U);
        std::ostringstream dump;
        dumpInstructions(runtime, *callable, dump);
        BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_address_push) != NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_create_script_struct) != NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_local_address_push) == NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_set) == NomadString::npos);
    }
    const auto* type = runtime.getTypeByName("Payload")->asStructType();
    const auto methods = type->getMethods();
    const auto create = std::ranges::find_if(methods, [](const auto* candidate) {
        return candidate->getName() == "create";
    });
    BOOST_REQUIRE(create != methods.end());
    const auto* method = (*create)->asScriptCallable();
    BOOST_REQUIRE(method != nullptr);
    BOOST_TEST(method->getVariableCount() == 0U);
    BOOST_REQUIRE(method->getCallStorageLayout().returnOffset.has_value());
    BOOST_REQUIRE(method->getCallStorageLayout().receiverOffset.has_value());
    BOOST_TEST(*method->getCallStorageLayout().returnOffset == 2U);
    BOOST_TEST(*method->getCallStorageLayout().receiverOffset == 1U);
    const std::vector<NomadInteger> expectedCalls{2, 1, 10, 20, 2, 3, 10, 20, 10, 30, 10, 20};
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls.clear();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 10);
        BOOST_TEST(calls == expectedCalls, boost::test_tools::per_element());
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(method_return_forwarding_preserves_stored_borrowed_and_temporary_receiver_values) {
    nomad::test::LanguageTestFixture fixture;
    fixture.addFunction("main",
        "struct Payload\n"
        "number = 0\ntext = \"payload\"\n"
        "init number:int\nthis.number = number\nend\n"
        "fun copied amount:int\nthis.number = this.number + amount\nreturn this\nend\n"
        "fun relay amount:int\nreturn this.copied amount\nend\n"
        "end\n"
        "struct Nested\npayload = new Payload 4\nend\n"
        "class Owner\nnested = new Nested\nend\n"
        "fun fromParameter value:Payload\nreturn value.copied 1\nend\n"
        "fun fromLocal number:int\nvalue = new Payload number\nreturn value.copied 2\nend\n"
        "fun fromOwner owner:Owner\nreturn owner.nested.payload.copied 3\nend\n"
        "fun fromClassTemporary\nreturn (new Owner).nested.payload.copied 4\nend\n"
        "fun fromTemporary number:int\nreturn (new Payload number).copied 4\nend\n"
        "fun fromChain number:int\nreturn ((new Payload number).copied 5).copied 6\nend\n"
        "fun propertyCopy owner:Owner\nreturn owner.nested.payload\nend\n"
        "owner = new Owner\noriginal = new Payload 10\n"
        "first = fromParameter original\nsecond = fromLocal 20\n"
        "third = fromOwner owner\nfourth = fromTemporary 30\nfifth = fromChain 40\n"
        "sixth = original.relay 7\nproperty = propertyCopy owner\n"
        "classTemporary = fromClassTemporary\nassert classTemporary.number == 8\n"
        "owner.nested.payload = fromParameter owner.nested.payload\n"
        "assert original.number == 17\nassert first.number == 11\n"
        "assert second.number == 22\nassert third.number == 7\n"
        "assert fourth.number == 34\nassert fifth.number == 51\nassert sixth.number == 17\n"
        "assert owner.nested.payload.number == 8\nassert property.number == 7\n"
        "assert first.text == \"payload\"\nassert fifth.text == \"payload\"\n"
        "return first.number + second.number + third.number + fourth.number + fifth.number + sixth.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    for (const auto* name : {
        "fromParameter", "fromLocal", "fromOwner", "fromClassTemporary", "fromTemporary", "fromChain"
    }) {
        const auto* wrapper = runtime.getScriptFunction(runtime.getFunctionId(name));
        BOOST_REQUIRE(wrapper != nullptr);
        for (auto index = NomadIndex{0}; index < wrapper->getVariableCount(); ++index) {
            BOOST_TEST(!wrapper->getVariableName(VariableId{toNomadId(index)}).starts_with("$return"));
        }
        std::ostringstream dump;
        dumpInstructions(runtime, *wrapper, dump);
        BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_address_push) != NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_set) == NomadString::npos);
    }
    BOOST_TEST(runtime.getScriptFunction(runtime.getFunctionId("fromParameter"))->getVariableCount() == 0U);
    BOOST_TEST(runtime.getScriptFunction(runtime.getFunctionId("fromOwner"))->getVariableCount() == 0U);
    BOOST_TEST(runtime.getScriptFunction(runtime.getFunctionId("fromTemporary"))->getVariableCount() == 1U);
    BOOST_TEST(runtime.getScriptFunction(runtime.getFunctionId("fromChain"))->getVariableCount() == 2U);
    const auto* propertyCopy = runtime.getScriptFunction(runtime.getFunctionId("propertyCopy"));
    BOOST_TEST(propertyCopy->getVariableCount() == 1U);
    BOOST_TEST(propertyCopy->getVariableName(VariableId{0}).starts_with("$return"));
    const auto* payload = runtime.getTypeByName("Payload")->asStructType();
    const auto methods = payload->getMethods();
    const auto relay = std::ranges::find_if(methods, [](const auto* method) {
        return method->getName() == "relay";
    });
    BOOST_REQUIRE(relay != methods.end());
    const auto* callable = (*relay)->asScriptCallable();
    BOOST_REQUIRE(callable != nullptr);
    BOOST_TEST(callable->getVariableCount() == 0U);
    BOOST_REQUIRE(callable->getCallStorageLayout().returnOffset.has_value());
    BOOST_REQUIRE(callable->getCallStorageLayout().receiverOffset.has_value());
    BOOST_TEST(*callable->getCallStorageLayout().returnOffset == 2U);
    BOOST_TEST(*callable->getCallStorageLayout().receiverOffset == 1U);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 142);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(class_method_struct_results_support_local_and_forwarded_destinations_and_temporary_roots) {
    nomad::test::LanguageTestFixture fixture;
    fixture.addFunction("main",
        "struct ClassReturnEmpty\nend\n"
        "struct ClassReturnSingle\nnumber = 3\nend\n"
        "class ClassReturnShared\nnumber = 5\nend\n"
        "struct ClassReturnLeaf\nnumber = 7\ntext = \"leaf\"\nshared = new ClassReturnShared\nend\n"
        "struct ClassReturnPayload\nleaf = new ClassReturnLeaf\nlabel = \"outer\"\nempty = new ClassReturnEmpty\nend\n"
        "class ClassReturnFactory\npayload = new ClassReturnPayload\ncount = 0\n"
        "fun copy delta:int\nthis.count = this.count + 1\n"
        "result = this.payload\nresult.leaf.number = result.leaf.number + delta\n"
        "result.leaf.text = \"copied\"\nreturn result\nend\n"
        "fun relay delta:int\nreturn this.copy delta\nend\n"
        "fun combine first:ClassReturnPayload empty:ClassReturnEmpty second:ClassReturnPayload\n"
        "first.leaf.number = first.leaf.number + second.leaf.number\nreturn first\nend\n"
        "fun single\nreturn new ClassReturnSingle\nend\n"
        "fun empty\nthis.count = this.count + 1\nreturn new ClassReturnEmpty\nend\n"
        "fun nested other:ClassReturnFactory\nresult = other.copy 1\n"
        "this.count = this.count + 10\nreturn result\nend\nend\n"
        "class ClassReturnContainer\nfactory = new ClassReturnFactory\nend\n"
        "class ClassReturnResultHolder\npayload = (new ClassReturnFactory).copy 1\nend\n"
        "fun forwardClassReturn factory:ClassReturnFactory\nreturn factory.copy 1\nend\n"
        "fun forwardTemporaryClassReturn\nreturn (new ClassReturnFactory).copy 2\nend\n"
        "fun forwardNestedClassReturn container:ClassReturnContainer\nreturn container.factory.relay 3\nend\n"
        "fun forwardClassEmpty factory:ClassReturnFactory\nreturn factory.empty\nend\n"
        "fun forwardClassSingle factory:ClassReturnFactory\nreturn factory.single\nend\n"
        "factory = new ClassReturnFactory\nother = new ClassReturnFactory\ncontainer = new ClassReturnContainer\n"
        "first = factory.copy 2\nsecond = forwardClassReturn factory\n"
        "temporary = forwardTemporaryClassReturn\nnested = forwardNestedClassReturn container\n"
        "assert factory.payload.leaf.number == 7\nassert first.leaf.number == 9\n"
        "assert second.leaf.number == 8\nassert temporary.leaf.number == 9\nassert nested.leaf.number == 10\n"
        "holder = new ClassReturnResultHolder\nassert holder.payload.leaf.number == 8\n"
        "first.leaf.shared.number = 29\nassert factory.payload.leaf.shared.number == 29\n"
        "combined = factory.combine (factory.copy 1) (new ClassReturnEmpty) (other.copy 2)\n"
        "assert combined.leaf.number == 17\nassert other.payload.leaf.number == 7\n"
        "factory.payload = factory.copy 4\nassert factory.payload.leaf.number == 11\n"
        "assert first.leaf.number == 9\nassert factory.payload.leaf.text == \"copied\"\n"
        "read = ((new ClassReturnFactory).copy 5).leaf.text\nassert read == \"copied\"\n"
        "temporaryNumber = (container.factory.copy 2).leaf.number\nassert temporaryNumber == 9\n"
        "factory.copy 3\nforwardClassEmpty factory\n(new ClassReturnFactory).empty\n"
        "single = forwardClassSingle factory\nassert single.number == 3\n"
        "before = factory.count\nnestedResult = factory.nested other\n"
        "assert nestedResult.leaf.number == 8\nassert factory.count == before + 10\n"
        "return combined.leaf.number + factory.payload.leaf.number + single.number");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    for (const auto* name : {
        "forwardClassReturn", "forwardTemporaryClassReturn", "forwardNestedClassReturn",
        "forwardClassEmpty", "forwardClassSingle"
    }) {
        const auto* function = runtime.getScriptFunction(runtime.getFunctionId(name));
        BOOST_REQUIRE(function != nullptr);
        BOOST_TEST(function->getVariableCount() == 0U);
        std::ostringstream dump;
        dumpInstructions(runtime, *function, dump);
        BOOST_TEST(dump.str().find(OpCodes::op_call_script_method) != NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_local_address_push) == NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_set) == NomadString::npos);
        BOOST_TEST((dump.str().find(OpCodes::op_storage_parameter_address_push) != NomadString::npos) ==
            (NomadStringView{name} != "forwardClassEmpty"));
    }
    const auto* factory = runtime.getTypeByName("ClassReturnFactory")->asClassType();
    const auto relay = runtime.resolveMethod(factory, "relay", {runtime.getIntegerType()});
    BOOST_REQUIRE(relay.isResolved());
    const auto* callable = runtime.getMethod(relay.id)->asScriptCallable();
    BOOST_REQUIRE(callable != nullptr);
    BOOST_TEST(callable->getVariableCount() == 0U);
    BOOST_TEST(!callable->getCallStorageLayout().receiverOffset.has_value());
    BOOST_TEST(callable->getCallStorageLayout().returnOffset.value() == 1U);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 31);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(method_return_forwarding_handles_empty_receiver_and_result_combinations) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<NomadInteger> calls;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "note",
        [&](VirtualMachine* active) { calls.push_back(active->getIntegerParameter(0)); },
        {defParameter("number", runtime.getIntegerType(), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "struct Empty\n"
        "fun copied\nnote 1\nreturn this\nend\n"
        "fun payload\nnote 2\nreturn new Payload\nend\n"
        "end\n"
        "struct Payload\nnumber = 17\ntext = \"payload\"\n"
        "fun empty\nnote 3\nreturn new Empty\nend\nend\n"
        "class Owner\nempty = new Empty\nend\n"
        "fun emptyBoth value:Empty\nreturn value.copied\nend\n"
        "fun nonemptyResult value:Empty\nreturn value.payload\nend\n"
        "fun emptyResult value:Payload\nreturn value.empty\nend\n"
        "fun classOwnedEmpty owner:Owner\nreturn owner.empty.payload\nend\n"
        "empty = new Empty\nemptyBoth empty\n"
        "value = nonemptyResult empty\nemptyResult value\n"
        "owner = new Owner\nother = classOwnedEmpty owner\nreturn value.number + other.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    for (const auto* name : {"emptyBoth", "nonemptyResult", "emptyResult", "classOwnedEmpty"}) {
        const auto* wrapper = runtime.getScriptFunction(runtime.getFunctionId(name));
        BOOST_REQUIRE(wrapper != nullptr);
        BOOST_TEST(wrapper->getVariableCount() == 0U);
        BOOST_TEST(wrapper->getVariableStorageSize() == 0U);
        std::ostringstream dump;
        dumpInstructions(runtime, *wrapper, dump);
        BOOST_TEST(dump.str().find(OpCodes::op_local_address_push) == NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_set) == NomadString::npos);
    }
    const std::vector<NomadInteger> expectedCalls{1, 2, 3, 2};
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls.clear();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 34);
        BOOST_TEST(calls == expectedCalls, boost::test_tools::per_element());
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(temporary_script_receivers_preserve_results_and_release_owned_fields_after_calls) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "countObjects",
        [](VirtualMachine* machine) {
            machine->setIntegerResult(static_cast<NomadInteger>(NomadObject::getLiveObjectCount()));
        },
        {},
        runtime.getIntegerType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "ownedText",
        [](VirtualMachine* machine) { machine->setStringResult("owned"); },
        {},
        runtime.getStringType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "class Owner\nvalue = 0\ntext = ownedText\nend\n"
        "struct Point\n"
        "value = 0\nowner = new Owner\n"
        "init value:int\nthis.value = value\nthis.owner.value = value\nend\n"
        "fun move amount:int\nthis.value = this.value + amount\nend\n"
        "fun read\nreturn this.value\nend\n"
        "fun text\nreturn this.owner.text\nend\n"
        "fun getOwner\nreturn this.owner\nend\n"
        "fun copied\nreturn this\nend\n"
        "end\n"
        "fun matches text:string\nreturn text == \"owned\"\nend\n"
        "fun inspect owner:Owner\nreturn owner.value\nend\n"
        "before = countObjects\n"
        "index = 0\nwhile index < 4\n"
        "(new Point index).move 2\nassert countObjects == before\n"
        "index = index + 1\n"
        "end\n"
        "assert (new Point 2).read == 2\nassert countObjects == before\n"
        "assert (new Point 3).text == \"owned\"\nassert countObjects == before\n"
        "assert matches ((new Point 4).text)\nassert countObjects == before\n"
        "assert (inspect ((new Point 5).getOwner)) == 5\nassert countObjects == before\n"
        "assert ((new Point 6).copied).read == 6\nassert countObjects == before\n"
        "(new Point 7).copied\nassert countObjects == before\n"
        "text = (new Point 8).text\nassert text == \"owned\"\n"
        "assert countObjects == before + 1\n"
        "copy = (new Point 9).copied\ncopy.move 1\n"
        "return copy.read + (new Point 11).read");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 21);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(temporary_receivers_are_evaluated_once_before_right_to_left_nested_arguments) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<NomadInteger> calls;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "record",
        [&](VirtualMachine* machine) {
            const auto value = machine->getIntegerParameter(0);
            calls.push_back(value);
            machine->setIntegerResult(value);
        },
        {defParameter("value", runtime.getIntegerType(), "")},
        runtime.getIntegerType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "struct Point\n"
        "value = 0\nlabel = \"point\"\n"
        "init value:int\nthis.value = value\nend\n"
        "fun read\nreturn this.value\nend\n"
        "fun mix first:int second:int\n"
        "record 100\nreturn this.value + first + second\nend\n"
        "end\n"
        "fun make value:int\nrecord value\npoint = new Point value\nreturn point\nend\n"
        "return (make 1).mix ((make 2).read) ((make 3).read)");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const std::vector<NomadInteger> expectedCalls{1, 3, 2, 100};
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls.clear();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 6);
        BOOST_TEST(calls == expectedCalls, boost::test_tools::per_element());
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(empty_temporary_receivers_preserve_construction_copy_and_method_side_effects) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<NomadInteger> calls;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "note",
        [&](VirtualMachine* machine) { calls.push_back(machine->getIntegerParameter(0)); },
        {defParameter("value", runtime.getIntegerType(), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "struct Empty\n"
        "init\nnote 1\nend\n"
        "fun ping value:int\nnote value\nend\n"
        "fun copied\nnote 3\nreturn this\nend\n"
        "end\n"
        "fun make\nnote 2\nvalue = new Empty\nreturn value\nend\n"
        "(new Empty).ping 4\n"
        "(make).ping 5\n"
        "((new Empty).copied).ping 6\n"
        "(new Empty).copied\n"
        "return 17");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    BOOST_TEST(runtime.getTypeByName("Empty")->getSize() == 0U);
    BOOST_TEST(runtime.getScriptFunction(runtime.getFunctionId("main"))->getVariableStorageSize() == 0U);
    const std::vector<NomadInteger> expectedCalls{1, 4, 2, 1, 5, 1, 3, 6, 1, 3};
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls.clear();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(calls == expectedCalls, boost::test_tools::per_element());
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(compiled_constructors_run_field_initializers_before_selected_bodies) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<NomadInteger> calls;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "record",
        [&](VirtualMachine* machine) {
            const auto value = machine->getIntegerParameter(0);
            calls.push_back(value);
            machine->setIntegerResult(value);
        },
        {defParameter("value", runtime.getIntegerType(), "")},
        runtime.getIntegerType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "struct Point\n"
        "x = record 1\n"
        "label = \"initial\"\n"
        "init value:int\n"
        "assert this.x == 1\nassert this.y == 0\nassert this.label == \"initial\"\n"
        "this.x = value\nthis.y = 18\nthis.label = \"integer\"\nrecord 2\n"
        "end\n"
        "init value:string\n"
        "assert this.x == 1\nassert this.y == 0\nassert this.label == \"initial\"\n"
        "this.x = 4\nthis.y = 5\nthis.label = value\nrecord 3\n"
        "end\nend\n"
        "first = new Point 17\nsecond = new Point \"string\"\n"
        "assert second.x == 4\nassert second.label == \"string\"\n"
        "second = first\nsecond.x = 99\nsecond.label = \"changed\"\n"
        "assert first.x == 17\nassert first.label == \"integer\"\n"
        "return first.x + second.y");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const std::vector<NomadInteger> expectedCalls{1, 2, 1, 3};
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls.clear();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 35);
        BOOST_TEST(calls == expectedCalls, boost::test_tools::per_element());
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(compiled_parameters_returns_and_methods_preserve_independent_values) {
    nomad::test::LanguageTestFixture fixture;
    fixture.addFunction("main",
        "struct Counter\n"
        "value = 0\nlabel = \"start\"\n"
        "init value:int\nthis.value = value\nend\n"
        "fun add amount:int\nthis.value = this.value + amount\nreturn this.value\nend\n"
        "fun copied\nreturn this\nend\n"
        "end\n"
        "fun adjust input:Counter early:bool\n"
        "input.value = input.value + 10\ninput.label = \"adjusted\"\n"
        "if early\nreturn input\nend\n"
        "input.add 5\nreturn input\nend\n"
        "fun relay input:Counter\nreturn adjust input false\nend\n"
        "original = new Counter 2\n"
        "first = adjust original true\nsecond = relay original\n"
        "copied = original.copied\ncopied.add 3\n"
        "assert original.value == 2\nassert original.label == \"start\"\n"
        "assert first.value == 12\nassert first.label == \"adjusted\"\n"
        "assert second.value == 17\nassert copied.value == 5\n"
        "first = second\nfirst.add 1\n"
        "assert second.value == 17\n"
        "return original.value + first.value + second.value + copied.value");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 42);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(struct_receivers_shadow_runtime_this_contexts_in_members_callbacks_and_formats) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto contextId = runtime.registerVariableContext(
        "entity", "this.", std::make_unique<SimpleVariableContext>());
    BOOST_REQUIRE(contextId.isValid());
    const auto contextVariable = runtime.registerContextVariable(contextId, "this.x", runtime.getIntegerType());
    BOOST_REQUIRE(contextVariable.isValid());
    const auto invokeId = runtime.registerNativeFunction(
        "invoke",
        [&](VirtualMachine* machine) {
            const auto closure = machine->createClosure(&runtime, machine->getCallableIdParameter(0));
            const auto result = Interpreter(*machine).runFunction(*closure, {});
            machine->setFloatResult(result.getValue().getFloatValue());
        },
        {defParameter("callback", runtime.getCallbackType({}, runtime.getFloatType()), "")},
        runtime.getFloatType(),
        ""
    );
    BOOST_REQUIRE(invokeId.isValid());
    fixture.addFunction("main",
        "struct Point\nx = 0.0\ny = 0.0\n"
        "init x:float y:float\nthis.x = x\nthis.y = y\nend\n"
        "fun move dx:float dy:float\nthis.x = this.x + dx\nthis.y = this.y + dy\nend\n"
        "fun describe\nreturn $\"{this.x} {this.x + 1.0}\"\nend\n"
        "fun fromCallback\nreturn invoke fun\nthis.x = this.x + 1.0\nreturn this.x\nend\nend\n"
        "end\n"
        "fun moved value:Point dx:float dy:float\nvalue.move dx dy\nreturn value\nend\n"
        "this.x = 100\n"
        "point = new Point 1.0 2.0\ncopiedPoint = point\ncopiedPoint.move 3.0 4.0\n"
        "movedPoint = moved point 10.0 20.0\n"
        "assert point.x == 1.0\nassert point.y == 2.0\n"
        "assert copiedPoint.x == 4.0\nassert copiedPoint.y == 6.0\n"
        "assert movedPoint.x == 11.0\nassert movedPoint.y == 22.0\n"
        "assert point.describe == $\"{point.x} {point.x + 1.0}\"\n"
        "assert point.fromCallback == 2.0\nassert point.x == 1.0\n"
        "assert this.x == 100\nreturn movedPoint.x");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    BOOST_TEST(runtime.getContextVariableId("this.y").isInvalid());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.floatValue == 11.0f);
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(compiled_nested_structs_copy_fields_but_share_contained_class_references) {
    nomad::test::LanguageTestFixture fixture;
    fixture.addFunction("main",
        "class Label\ntext = \"shared\"\nend\n"
        "struct Point\n"
        "x = 1\nlabel = new Label\n"
        "init value:int\nthis.x = value\nend\n"
        "fun move amount:int\nthis.x = this.x + amount\nend\n"
        "end\n"
        "struct Transform\nlocation = new Point 3\nname = \"transform\"\nend\n"
        "class Player\ntransform = new Transform\nname = \"player\"\nend\n"
        "player = new Player\nvalue = player.transform\n"
        "value.location.move 4\n"
        "assert value.location.x == 7\nassert player.transform.location.x == 3\n"
        "value.location.label.text = \"shared edit\"\n"
        "assert player.transform.location.label.text == \"shared edit\"\n"
        "player.transform.location.move 2\nassert player.transform.location.x == 5\n"
        "player.transform = value\nvalue.location.x = 9\n"
        "assert player.transform.location.x == 7\n"
        "other = new Player\nother.transform = player.transform\n"
        "other.transform.location.x = 11\n"
        "assert player.transform.location.x == 7\n"
        "assert other.transform.location.label.text == \"shared edit\"\n"
        "assert player.name == \"player\"\nassert value.name == \"transform\"\n"
        "return value.location.x + player.transform.location.x + other.transform.location.x");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 27);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(compiled_cross_file_hub_structs_preserve_zero_width_side_effects) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<NomadInteger> calls;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "note",
        [&](VirtualMachine* machine) {
            calls.push_back(machine->getIntegerParameter(0));
        },
        {defParameter("value", runtime.getIntegerType(), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "fun echo value:model.Empty\nvalue.touch\nreturn value\nend\n"
        "empty = new model.Empty\ncopied = empty\nreturned = echo copied\n"
        "returned.touch\nbox = new model.Box\nbox.empty.touch\nreturn box.value");
    fixture.addFunction("types",
        "hub model\n"
        "struct Empty\ninit\nnote 1\nend\nfun touch\nnote 2\nend\nend\n"
        "struct Box\nempty = new model.Empty\nvalue = 17\nend\n"
        "end");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto* emptyType = runtime.getTypeByName("model.Empty");
    BOOST_REQUIRE(emptyType != nullptr);
    BOOST_TEST(emptyType->isStruct());
    BOOST_TEST(!emptyType->isVoid());
    BOOST_TEST(emptyType->getSize() == 0U);
    const std::vector<NomadInteger> expectedCalls{1, 2, 2, 1, 2};
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls.clear();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(calls == expectedCalls, boost::test_tools::per_element());
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(struct_declarations_have_specialized_ast_members_and_source_spans) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_REQUIRE(compileSources({{{"main", {"main.nomad", "return"}}}, {}},
        *compiler, CompilationTarget::Analysis).succeeded());
    BOOST_REQUIRE(runtime.registerStruct("Point").isValid());
    auto* function = runtime.getScriptCallable(runtime.getFunctionId("main"));
    CompilerContext context(compiler.get());
    std::unique_ptr<Statement> statement;
    {
        Tokenizer tokens(&context, "point.nomad",
            "struct Point\n"
            "    label = \"point\"\n"
            "    init value:int\n"
            "        this.x = value\n"
            "    end\n"
            "    fun copy value:Point\n"
            "        return value\n"
            "    end\n"
            "end\n"
            "return 42");
        BOOST_REQUIRE(tokens.nextLine());
        BOOST_REQUIRE(tokens.expect("struct"));
        BOOST_REQUIRE(preParseStructStatement(&context, function, &tokens, PreParsePhase::Signatures));
        tokens.reset();
        BOOST_REQUIRE(tokens.nextLine());
        statement = parser::parseLine(&context, function, &tokens);
        BOOST_REQUIRE_MESSAGE(statement != nullptr,
            (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Missing struct AST"));
        BOOST_TEST(tokens.getLineIndex() == 9U);
        BOOST_REQUIRE(tokens.nextLine());
        BOOST_TEST(tokens.is("return"));
    }
    const auto* declaration = dynamic_cast<const StructDeclaration*>(statement.get());
    BOOST_REQUIRE(declaration != nullptr);
    BOOST_TEST(dynamic_cast<const ClassDeclaration*>(statement.get()) == nullptr);
    BOOST_TEST(!context.hasError());
    BOOST_TEST(declaration->isDeclaration());
    BOOST_TEST(declaration->getType() == runtime.getTypeByName("Point")->asStructType());
    BOOST_TEST(declaration->getLine() == 1U);
    BOOST_TEST(declaration->getEndLine() == 9U);
    const auto& members = declaration->getMembers();
    BOOST_REQUIRE(members.size() == 3U);
    BOOST_TEST((members[0]->getKind() == StructMemberKind::Field));
    BOOST_TEST(members[0]->getName() == "label");
    BOOST_TEST(members[0]->getLine() == 2U);
    BOOST_TEST(members[0]->getColumn() == 5U);
    BOOST_REQUIRE(members[0]->getSourceLines().size() == 1U);
    BOOST_TEST(members[0]->getSourceLines()[0].tokens[0].textValue == "point");
    BOOST_REQUIRE(members[0]->getInitializer() != nullptr);
    BOOST_TEST((members[1]->getKind() == StructMemberKind::Constructor));
    BOOST_TEST(members[1]->getCallableId().isValid());
    BOOST_REQUIRE(members[1]->getParameters().size() == 1U);
    BOOST_TEST(members[1]->getParameters()[0].type == runtime.getIntegerType());
    BOOST_TEST(members[1]->getEndLine() == 5U);
    BOOST_TEST((members[2]->getKind() == StructMemberKind::Method));
    BOOST_REQUIRE(members[2]->getParameters().size() == 1U);
    BOOST_TEST(members[2]->getParameters()[0].type == declaration->getType());
    BOOST_TEST(declaration->getType()->getConstructors().size() == 1U);
    BOOST_TEST(declaration->getType()->getMethods().size() == 2U);
}

BOOST_AUTO_TEST_CASE(struct_member_scanning_balances_blocks_callbacks_and_loop_closers) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto* callbackType = runtime.getCallbackType({runtime.getIntegerType()}, runtime.getIntegerType());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "invoke",
        [](VirtualMachine*) {},
        {defParameter("callback", callbackType, "")},
        runtime.getIntegerType(),
        ""
    ).isValid());
    BOOST_REQUIRE(compileSources({{{"main", {"main.nomad", "return"}}}, {}},
        *compiler, CompilationTarget::Analysis).succeeded());
    BOOST_REQUIRE(runtime.registerStruct("Point").isValid());
    auto* function = runtime.getScriptCallable(runtime.getFunctionId("main"));
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "blocks.nomad",
        "struct Point\n"
        "label = \"fun\"\n"
        "init\n"
        "do\n"
        "if true break\n"
        "end until true\n"
        "end\n"
        "fun process\n"
        "while true\n"
        "if false continue\n"
        "if true\n"
        "value = $\"fun {label}\"\n"
        "end\n"
        "end\n"
        "invoke fun item\n"
        "return item\n"
        "end\n"
        "invoke then item\n"
        "return item\n"
        "end\n"
        "last = 42\n"
        "end\n"
        "return 7");
    BOOST_REQUIRE(tokens.nextLine());
    BOOST_REQUIRE(tokens.expect("struct"));
    BOOST_REQUIRE(preParseStructStatement(&context, function, &tokens, PreParsePhase::Signatures));
    tokens.reset();
    BOOST_REQUIRE(tokens.nextLine());
    const auto statement = parser::parseLine(&context, function, &tokens);
    const auto* declaration = dynamic_cast<const StructDeclaration*>(statement.get());
    BOOST_REQUIRE_MESSAGE(declaration != nullptr,
        (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Missing struct AST"));
    BOOST_TEST(!context.hasError());
    const auto& members = declaration->getMembers();
    BOOST_REQUIRE(members.size() == 4U);
    BOOST_TEST((members[1]->getKind() == StructMemberKind::Constructor));
    BOOST_REQUIRE(members[1]->getSourceLines().size() == 3U);
    BOOST_TEST(members[1]->getSourceLines().back().tokens[0].textValue == "end");
    BOOST_TEST((members[2]->getKind() == StructMemberKind::Method));
    BOOST_TEST(members[2]->getSourceLines().front().tokens[0].textValue == "while");
    BOOST_TEST(members[2]->getSourceLines().back().tokens[0].textValue == "return");
    BOOST_TEST(members[3]->getName() == "last");
    BOOST_REQUIRE(tokens.nextLine());
    BOOST_TEST(tokens.is("return"));
}

BOOST_AUTO_TEST_CASE(symbols_precede_cross_file_signatures_and_analysis_does_not_emit) {
    for (const auto reversed : {false, true}) {
        Runtime runtime;
        auto compiler = runtime.createCompiler();
        const auto instructionCount = runtime.getInstructions().size();
        const SourceBatch sources{{
            {reversed ? "z" : "a", {"signature.nomad",
                "fun identity value:model.Empty\nreturn value\nend"}},
            {reversed ? "a" : "z", {"type.nomad", "hub model\nstruct Empty\nend\nend"}}
        }, {}};
        const auto result = compileSources(sources, *compiler, CompilationTarget::Analysis);
        for (const auto& diagnostic : result.diagnostics) BOOST_TEST_MESSAGE(formatDiagnostic(diagnostic));
        BOOST_REQUIRE(result.succeeded());
        const auto* type = runtime.getTypeByName("model.Empty")->asStructType();
        BOOST_REQUIRE(type != nullptr);
        BOOST_TEST(!type->isOpen());
        BOOST_TEST(!type->isVoid());
        BOOST_TEST(type->getSize() == 0U);
        BOOST_TEST(type->isFieldLayoutFinalized());
        const auto* identity = runtime.getScriptFunction(runtime.getFunctionId("identity"));
        BOOST_REQUIRE(identity != nullptr);
        BOOST_TEST(identity->getParameterType(0) == type);
        BOOST_TEST(identity->getReturnType() == type);
        BOOST_TEST(identity->getParameterStorageSize() == 0U);
        BOOST_TEST(identity->getFunctionStart() == NOMAD_INVALID_INDEX);
        BOOST_TEST(runtime.getInstructions().size() == instructionCount);
        const auto* hub = runtime.getHub(runtime.getHubId("model"));
        BOOST_REQUIRE(hub != nullptr);
        BOOST_REQUIRE(hub->getMember("Empty") != nullptr);
        BOOST_TEST(std::get<const Type*>(*hub->getMember("Empty")) == type);
        BOOST_REQUIRE(type->getInitializer() != nullptr);
        BOOST_TEST(type->getInitializer()->getName() == "model.Empty$init");
        BOOST_REQUIRE(type->getConstructors().size() == 1U);
        BOOST_TEST(type->getConstructors()[0]->getReturnType() == runtime.getVoidType());
        BOOST_TEST(type->getConstructors()[0]->getConstructedType() == type);
    }
}

BOOST_AUTO_TEST_CASE(fields_methods_overloads_and_nested_layouts_resolve_after_inference) {
    Runtime runtime;
    registerArrayType(&runtime);
    auto compiler = runtime.createCompiler();
    const auto result = compileSources({{{"main", {"members.nomad",
        "struct Outer\n"
        "    empty = new Empty\n"
        "    point = new Point 1.0 2.0\n"
        "    label = \"outer\"\n"
        "    samples = new Array1<int> 2\n"
        "    fun horizontal\nreturn this.point.x\nend\n"
        "end\n"
        "struct Point\n"
        "    x = 0.0\n"
        "    z = 0.0\n"
        "    init x:float y:float\nthis.x = x\nthis.y = y\nend\n"
        "    init value:int\nthis.x = 0.0\nthis.y = 0.0\nend\n"
        "    fun move amount:float\nthis.x = this.x + amount\nend\n"
        "    fun move amount:int\nthis.x = this.x + 1.0\nend\n"
        "end\n"
        "struct Empty\nend\n"
        "class Owner\nlocation = new Outer\nend\n"
        "class Player\nname = \"player\"\nlocation = new Point 1.0 2.0\nend\n"
        "fun printPoint point:Point\nreturn point.x\nend\n"
        "fun updatePoint point:Point x:float y:float\npoint.x = x\npoint.y = y\nend"
    }}}, {}}, *compiler, CompilationTarget::Analysis);
    for (const auto& diagnostic : result.diagnostics) BOOST_TEST_MESSAGE(formatDiagnostic(diagnostic));
    BOOST_REQUIRE(result.succeeded());
    const auto* point = runtime.getTypeByName("Point")->asStructType();
    const auto* outer = runtime.getTypeByName("Outer")->asStructType();
    BOOST_REQUIRE(point != nullptr && outer != nullptr);
    BOOST_TEST(point->getSize() == 3U);
    BOOST_TEST(point->getPropertyCount() == 3U);
    BOOST_TEST(point->getProperty("x")->hasExplicitInitializer());
    BOOST_TEST(!point->getProperty("y")->hasExplicitInitializer());
    BOOST_TEST(point->getConstructors().size() == 2U);
    BOOST_TEST(point->getMethods().size() == 3U);
    BOOST_TEST(outer->getSize() == 5U);
    BOOST_TEST(outer->getPropertyCount() == 4U);
    BOOST_TEST(outer->getFieldOffset(outer->getProperty("empty")->getId()) == 0U);
    BOOST_TEST(outer->getFieldOffset(outer->getProperty("point")->getId()) == 0U);
    BOOST_TEST(outer->getFieldOffset(outer->getProperty("label")->getId()) == 3U);
    BOOST_TEST(outer->getFieldOffset(outer->getProperty("samples")->getId()) == 4U);
    BOOST_TEST(runtime.getTypeByName("Owner")->asCompositeType()->getFieldStorageSize() == 5U);
    const auto* player = runtime.getTypeByName("Player")->asCompositeType();
    BOOST_TEST(player->getPropertyCount() == 2U);
    BOOST_TEST(player->getFieldStorageSize() == 4U);
    const auto* printPoint = runtime.getScriptFunction(runtime.getFunctionId("printPoint"));
    const auto* updatePoint = runtime.getScriptFunction(runtime.getFunctionId("updatePoint"));
    BOOST_REQUIRE(printPoint != nullptr && updatePoint != nullptr);
    BOOST_TEST(printPoint->getParameterCount() == 1U);
    BOOST_TEST(printPoint->getParameterStorageSize() == 3U);
    BOOST_TEST(updatePoint->getParameterCount() == 3U);
    BOOST_TEST(updatePoint->getParameterStorageSize() == 5U);
}

BOOST_AUTO_TEST_CASE(original_layout_and_recursive_struct_frames_preserve_copies_references_and_empty_arguments) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    fixture.addFunction("main",
        "struct Point\nx = 0.0\ny = 0.0\nz = 0.0\n"
        "init x:float y:float z:float\nthis.x = x\nthis.y = y\nthis.z = z\nend\nend\n"
        "class Player\nname = \"player\"\nlocation = new Point 0.0 0.0 0.0\nend\n"
        "class SharedVisits\ncount = 0\nend\n"
        "struct Empty\nfun touch\nreturn 1\nend\nend\n"
        "struct Payload\npoint = new Point 0.0 0.0 0.0\ntext = \"root\"\nowner = new SharedVisits\nend\n"
        "fun printPoint point:Point\nreturn point.x + point.y + point.z\nend\n"
        "fun updatePoint point:Point x:float y:float\npoint.x = x\npoint.y = y\nend\n"
        "fun walk value:Payload empty:Empty depth:int\n"
        "assert empty.touch == 1\n"
        "if depth == 0\nreturn value\nend\n"
        "value.point.x = value.point.x + 1.0\nvalue.text = $\"{depth}\"\n"
        "value.owner.count = value.owner.count + 1\n"
        "parentX = value.point.x\nparentText = value.text\n"
        "result = walk value empty (depth - 1)\n"
        "assert value.point.x == parentX\nassert value.text == parentText\n"
        "assert depth > 0\nreturn result\nend\n"
        "original = new Point 1.0 2.0 3.0\nupdatePoint original 20.0 30.0\n"
        "assert original.x == 1.0\nassert original.y == 2.0\nassert original.z == 3.0\n"
        "assert (printPoint original) == 6.0\n"
        "player = new Player\nplayer.location = original\ncopy = player.location\ncopy.x = 99.0\n"
        "assert player.location.x == 1.0\n"
        "source = new Payload\nsource.point = original\n"
        "returned = walk source (new Empty) 4\n"
        "assert source.point.x == 1.0\nassert source.text == \"root\"\n"
        "assert returned.point.x == 5.0\nassert returned.point.y == 2.0\nassert returned.point.z == 3.0\n"
        "assert returned.text == \"1\"\nassert source.owner.count == 4\n"
        "assert returned.owner.count == 4\nassert (printPoint original) == 6.0\n"
        "return returned.point.x");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto* point = runtime.getTypeByName("Point")->asStructType();
    const auto* player = runtime.getTypeByName("Player")->asCompositeType();
    BOOST_REQUIRE(point != nullptr);
    BOOST_REQUIRE(player != nullptr);
    BOOST_TEST(point->getPropertyCount() == 3U);
    BOOST_TEST(point->getSize() == 3U);
    BOOST_TEST(player->getPropertyCount() == 2U);
    BOOST_TEST(player->getFieldStorageSize() == 4U);
    const auto* printPoint = runtime.getScriptFunction(runtime.getFunctionId("printPoint"));
    const auto* updatePoint = runtime.getScriptFunction(runtime.getFunctionId("updatePoint"));
    BOOST_REQUIRE(printPoint != nullptr);
    BOOST_REQUIRE(updatePoint != nullptr);
    BOOST_TEST(printPoint->getParameterCount() == 1U);
    BOOST_TEST(printPoint->getParameterStorageSize() == 3U);
    BOOST_TEST(updatePoint->getParameterCount() == 3U);
    BOOST_TEST(updatePoint->getParameterStorageSize() == 5U);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.floatValue == 5.0f);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(struct_equality_rejects_empty_one_slot_and_multi_slot_values) {
    for (const auto* fields : {"", "number = 0\n", "text = \"owned\"\nnumber = 0\n"}) {
        for (const auto* operation : {"==", "!="}) {
            BOOST_TEST_CONTEXT("fields: " << fields << ", operator: " << operation) {
                Runtime runtime;
                auto compiler = runtime.createCompiler();
                const auto source = NomadString{"struct Value\n"} + fields +
                    "end\nfirst = new Value\nsecond = new Value\nreturn first " + operation + " second";
                const auto result = compileSources({{{"main", {"equality.nomad", source}}}, {}},
                    *compiler, CompilationTarget::Analysis);
                BOOST_TEST(!result.succeeded());
                BOOST_TEST(std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
                    return diagnostic.sourceName == "equality.nomad" &&
                        diagnostic.message.find(NomadString{"Invalid binary operator '"} + operation + "'") !=
                            NomadString::npos &&
                        diagnostic.message.find("Value") != NomadString::npos;
                }));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(inline_cycles_are_source_diagnostics_but_class_references_break_cycles) {
    for (const auto* source : {
        "struct Loop\nvalue = new Loop\nend",
        "struct First\nvalue = new Second\nend\nstruct Second\nvalue = new First\nend"
    }) {
        Runtime runtime;
        auto compiler = runtime.createCompiler();
        const auto result = compileSources({{{"main", {"cycle.nomad", source}}}, {}},
            *compiler, CompilationTarget::Analysis);
        BOOST_TEST(!result.succeeded());
        const auto diagnostic = std::ranges::find_if(result.diagnostics, [](const auto& value) {
            return value.message.find("Inline struct field layout cycle") != NomadString::npos;
        });
        BOOST_REQUIRE(diagnostic != result.diagnostics.end());
        BOOST_TEST(diagnostic->sourceName == "cycle.nomad");
        BOOST_TEST(diagnostic->line != NOMAD_INVALID_INDEX);
        BOOST_TEST(diagnostic->line > 0U);
    }
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto result = compileSources({{{"main", {"reference.nomad",
        "struct Value\nowner = new Owner\nend\nclass Owner\nvalue = new Value\nend"
    }}}, {}}, *compiler, CompilationTarget::Analysis);
    for (const auto& diagnostic : result.diagnostics) BOOST_TEST_MESSAGE(formatDiagnostic(diagnostic));
    BOOST_REQUIRE(result.succeeded());
    BOOST_TEST(runtime.getTypeByName("Value")->getSize() == 1U);
}

BOOST_AUTO_TEST_CASE(invalid_declarations_conflicts_and_closed_field_discovery_are_diagnosed) {
    for (const auto* source : {
        "struct", "struct Point", "struct 123\nend", "struct int\nend",
        "struct Point trailing\nend", "struct Point<T>\nend",
        "struct Point\nend\nstruct Point\nend", "class Point\nend\nstruct Point\nend",
        "struct Point\nend\nhub Point\nend", "fun Point\nend\nstruct Point\nend",
        "const Point = 1\nstruct Point\nend",
        "struct Point\nx = 1\nx = 2\nend",
        "struct Point\nx = 1\nfun x\nend\nend",
        "struct Point\ninit value:int\nend\ninit other:int\nend\nend",
        "struct Point\nfun move value:int\nend\nfun move other:int\nend\nend",
        "struct Point\nfun move this:int\nend\nend",
        "struct Point\nfun init\nend\nend", "struct Point\ninit = 1\nend",
        "struct Point\ninit\nthis.x = 1\nend\nfun change\nthis.x = \"bad\"\nend\nend",
        "struct Point\nfun change\nthis.missing = 1\nend\nend",
        "struct Point\nend\nvalue = new Point\nvalue.extra = 1",
        "struct Point\nx = 1\nend\nvalue = new Point\nvalue.x = \"bad\"",
        "fun nested\nstruct Point\nend\nend",
        "if true\nstruct Point\nend\nend",
        "struct Outer\nstruct Inner\nend\nend",
        "open struct Point\nend"
    }) {
        BOOST_TEST_CONTEXT(source) {
            Runtime runtime;
            auto compiler = runtime.createCompiler();
            const auto result = compileSources({{{"main", {"invalid.nomad", source}}}, {}},
                *compiler, CompilationTarget::Analysis);
            BOOST_TEST(!result.succeeded());
            BOOST_TEST(result.errorCount > 0U);
            BOOST_TEST(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
                return diagnostic.sourceName == "invalid.nomad";
            }));
        }
    }
}

BOOST_AUTO_TEST_CASE(struct_generic_arguments_are_rejected_before_specialization) {
    for (const auto* source : {
        "fun use value:List<Point>\nend",
        "fun use value:Map<string List<Point>>\nend",
        "fun use value:Array1<Point>\nend",
        "values = new List<Point>",
        "values = new List<Map<string Point>>",
        "values = new Array1<Point> 2",
        "values = new Array<Point> 2",
        "value = new KeyValue<string Point> \"key\" (new Point)",
        "value = new KeyValue \"key\" (new Point)"
    }) {
        BOOST_TEST_CONTEXT(source) {
            Runtime runtime;
            registerArrayType(&runtime);
            registerKeyValueType(&runtime);
            registerListType(&runtime);
            registerMapType(&runtime);
            auto compiler = runtime.createCompiler();
            const auto result = compileSources({{{"main", {"generic.nomad",
                NomadString("struct Point\nend\n") + source}}}, {}},
                *compiler, CompilationTarget::Analysis);
            BOOST_TEST(!result.succeeded());
            BOOST_TEST(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
                return diagnostic.sourceName == "generic.nomad" &&
                    diagnostic.message.find("type argument") != NomadString::npos &&
                    diagnostic.message.find("Point") != NomadString::npos;
            }));
            BOOST_TEST(runtime.getTypeByName("List<Point>") == nullptr);
            BOOST_TEST(runtime.getTypeByName("Array1<Point>") == nullptr);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
