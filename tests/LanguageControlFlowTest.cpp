// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/compiler/Parser.hpp>

#include <nomad/script/IteratorObject.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using namespace nomad;
using namespace nomad::test;

static_assert(!std::is_convertible_v<NomadId, KeywordId>);
static_assert(!std::is_convertible_v<KeywordId, NomadId>);
static_assert(!std::is_convertible_v<HubId, KeywordId>);
static_assert(!std::is_convertible_v<KeywordId, HubId>);
static_assert(!std::is_convertible_v<CallableId, KeywordId>);
static_assert(!std::is_convertible_v<KeywordId, CallableId>);

namespace {

FunctionOutcome runWithInteger(const std::string& source, NomadInteger argument) {
    LanguageTestFixture fixture;
    fixture.addFunction("main", source);

    if (!fixture.compile()) {
        FunctionOutcome outcome;
        outcome.diagnostics = fixture.getDiagnostics();

        return outcome;
    }

    return fixture.execute("main", {RuntimeValue(argument)});
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_control_flow)

BOOST_AUTO_TEST_CASE(keyword_ids_support_registration_lookup_and_catalog_growth)
{
    Runtime runtime;
    BOOST_TEST(runtime.getKeywordId("missing") == KeywordId::INVALID);
    const auto builtinId = runtime.getKeywordId("new");
    BOOST_REQUIRE(builtinId.isValid());
    std::vector<KeywordDefinition> keywords;
    runtime.getKeywords(keywords);
    const auto originalCount = keywords.size();

    const auto keywordId = runtime.registerKeyword("reservedword", NomadDoc("Test keyword."));
    BOOST_REQUIRE(keywordId.isValid());
    BOOST_TEST(runtime.getKeywordId("reservedword") == keywordId);
    const auto duplicateId = runtime.registerKeyword("reservedword", NomadDoc("Duplicate keyword."));
    BOOST_TEST(duplicateId == keywordId);
    BOOST_TEST(runtime.getKeywordId("reservedword") == keywordId);
    BOOST_TEST(runtime.registerKeyword("new", NomadDoc("Replacement documentation.")) == builtinId);
    runtime.getKeywords(keywords);
    BOOST_REQUIRE(keywords.size() == originalCount + 1);

    for (auto index = 0; index < 64; ++index) {
        BOOST_REQUIRE(runtime.registerKeyword("reserved" + std::to_string(index), NomadDoc("")).isValid());
    }
    BOOST_TEST(runtime.getKeywordId("new") == builtinId);
    BOOST_TEST(runtime.getKeywordId("reservedword") == keywordId);
    BOOST_TEST(runtime.getKeywordId("missing") == KeywordId::INVALID);
    BOOST_TEST(runtime.registerKeyword("reservedword", NomadDoc("After growth.")) == keywordId);
    runtime.getKeywords(keywords);
    BOOST_REQUIRE(keywords.size() == originalCount + 65);
    BOOST_TEST(keywords[originalCount].keyword == "reservedword");
#if defined(NOMAD_FUNCTION_DOC)
    BOOST_TEST(keywords[originalCount].doc == "Test keyword.");
#endif

    const auto compiler = runtime.createCompiler();
    BOOST_TEST((compiler->getIdentifierType("reservedword", nullptr) == IdentifierType::Keyword));
    BOOST_TEST((parser::getIdentifierType(compiler.get(), "reservedword") == IdentifierType::Keyword));
    BOOST_TEST(runtime.registerHub("reservedword").isValid());
    BOOST_TEST(runtime.registerHub("example.reservedword").isValid());
    BOOST_TEST(runtime.getHubId("example").isValid());
}

BOOST_AUTO_TEST_CASE(iterator_types_are_specialized_and_interned)
{
    Runtime runtime;
    const auto* integers = runtime.getIteratorType(runtime.getIntegerType());
    const auto* strings = runtime.getIteratorType(runtime.getStringType());
    BOOST_TEST(integers->getTypeName() == "Iterator<int>");
    BOOST_TEST(strings->getTypeName() == "Iterator<string>");
    BOOST_TEST(integers != strings);
    BOOST_TEST(runtime.getIteratorType(runtime.getIntegerType()) == integers);
    BOOST_TEST(runtime.getParameterizedType("Iterator", {runtime.getStringType()}) == strings);
    BOOST_CHECK_THROW(static_cast<void>(runtime.getIteratorType(runtime.getVoidType())), NomadException);
}

BOOST_AUTO_TEST_CASE(iterable_metadata_belongs_to_the_registered_object_type)
{
    Runtime runtime;
    Runtime other;
    const auto descriptorId = runtime.createObjectType("SequenceMetadata", ObjectTypeMode::Closed);
    BOOST_REQUIRE(descriptorId.isValid());
    auto* type = const_cast<ObjectType*>(runtime.getType(descriptorId).value()->asObjectType());
    const auto* integer = runtime.getIntegerType();
    const auto* iterator = runtime.getIteratorType(integer);
    const auto factoryId = runtime.addMethod(type->getId(), {
        .name = "iterator",
        .parameters = {},
        .returnTypeId = iterator->getId(),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    });
    BOOST_REQUIRE(factoryId.isValid());
    const auto* factory = runtime.getMethod(factoryId);
    BOOST_TEST(!type->getIterableInfo().has_value());
    BOOST_TEST(!runtime.getIterableInfo(type).has_value());
    BOOST_TEST(!runtime.getIterableInfo(nullptr).has_value());
    BOOST_TEST(!runtime.getIterableInfo(integer).has_value());
    BOOST_TEST(!other.getIterableInfo(type).has_value());
    BOOST_TEST(!runtime.getIterableInfo(other.getStringType()).has_value());
    ObjectType unregistered(&runtime, TypeId::INVALID, "SequenceMetadata", ObjectTypeMode::Closed);
    BOOST_TEST(!runtime.getIterableInfo(&unregistered).has_value());
    BOOST_CHECK_THROW(unregistered.registerIterableType(runtime, integer, factory), NomadException);
    BOOST_CHECK_THROW(type->registerIterableType(other, other.getIntegerType(), factory), NomadException);
    BOOST_CHECK_THROW(runtime.registerIterableType(nullptr, integer, factory), NomadException);
    BOOST_CHECK_THROW(runtime.registerIterableType(integer, integer, factory), NomadException);
    BOOST_CHECK_THROW(other.registerIterableType(type, other.getIntegerType(), factory), NomadException);
    for (const auto* invalid : {static_cast<const Type*>(nullptr), runtime.getVoidType(),
        runtime.getFileNameType(), other.getIntegerType()}) {
        BOOST_CHECK_THROW(type->registerIterableType(runtime, invalid, factory), NomadException);
        BOOST_CHECK_THROW(runtime.registerIterableType(type, invalid, factory), NomadException);
    }
    BOOST_CHECK_THROW(type->registerIterableType(runtime, integer, nullptr), NomadException);
    const auto wrongOwnerId = runtime.addMethod(runtime.getTypeId("string"), {
        .name = "metadataIterator",
        .parameters = {},
        .returnTypeId = iterator->getId(),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    });
    const auto wrongReturnId = runtime.addMethod(type->getId(), {
        .name = "wrongReturn",
        .parameters = {},
        .returnTypeId = integer->getId(),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    });
    const auto wrongArgumentsId = runtime.addMethod(type->getId(), {
        .name = "wrongArguments",
        .parameters = {{integer->getId(), "value", ""}},
        .returnTypeId = iterator->getId(),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    });
    BOOST_REQUIRE(wrongOwnerId.isValid());
    BOOST_REQUIRE(wrongReturnId.isValid());
    BOOST_REQUIRE(wrongArgumentsId.isValid());
    for (const auto invalidId : {wrongOwnerId, wrongReturnId, wrongArgumentsId}) {
        BOOST_CHECK_THROW(type->registerIterableType(runtime, integer, runtime.getMethod(invalidId)), NomadException);
        BOOST_CHECK_THROW(runtime.registerIterableType(type, integer, runtime.getMethod(invalidId)), NomadException);
    }
    BOOST_TEST(!type->getIterableInfo().has_value());
    BOOST_TEST(!runtime.getIterableInfo(type).has_value());

    runtime.registerIterableType(type, integer, factory);
    const auto verifyAssociation = [&]() {
        const auto owned = type->getIterableInfo();
        const auto forwarded = runtime.getIterableInfo(type);
        BOOST_REQUIRE(owned.has_value());
        BOOST_REQUIRE(forwarded.has_value());
        BOOST_TEST(owned->valueType == integer);
        BOOST_TEST(owned->iteratorType == iterator);
        BOOST_TEST(owned->factoryId == factoryId);
        BOOST_TEST(forwarded->valueType == owned->valueType);
        BOOST_TEST(forwarded->iteratorType == owned->iteratorType);
        BOOST_TEST(forwarded->factoryId == owned->factoryId);
        BOOST_TEST(runtime.getMethod(owned->factoryId) == factory);
    };
    verifyAssociation();
    BOOST_CHECK_THROW(runtime.registerIterableType(type, integer, factory), NomadException);
    BOOST_CHECK_THROW(type->registerIterableType(runtime, integer, factory), NomadException);
    BOOST_CHECK_THROW(type->registerIterableType(runtime, runtime.getStringType(), factory), NomadException);
    auto snapshot = type->getIterableInfo();
    snapshot->valueType = runtime.getStringType();
    snapshot->factoryId = CallableId::INVALID;
    for (auto index = 0; index < 32; ++index) {
        const auto additionalId = runtime.createObjectType("AdditionalSequence" + std::to_string(index), ObjectTypeMode::Closed);
    BOOST_REQUIRE(additionalId.isValid());
    auto* additionalType = const_cast<ObjectType*>(runtime.getType(additionalId).value()->asObjectType());
        const auto additionalFactoryId = runtime.addMethod(additionalType->getId(), {
            .name = "iterator",
            .parameters = {},
            .returnTypeId = iterator->getId(),
            .function = [](VirtualMachine*) {},
            .documentation = ""
        });
        BOOST_REQUIRE(additionalFactoryId.isValid());
        additionalType->registerIterableType(runtime, integer, runtime.getMethod(additionalFactoryId));
        BOOST_REQUIRE(runtime.getIterableInfo(additionalType).has_value());
        BOOST_TEST(runtime.getIterableInfo(additionalType)->factoryId == additionalFactoryId);
    }
    verifyAssociation();
}

BOOST_AUTO_TEST_CASE(range_iterable_metadata_is_exposed_by_its_owner)
{
    Runtime runtime;
    const auto* range = dynamic_cast<const ObjectType*>(runtime.getTypeByName("Range"));
    BOOST_REQUIRE(range != nullptr);
    const auto owned = range->getIterableInfo();
    const auto forwarded = runtime.getIterableInfo(range);
    BOOST_REQUIRE(owned.has_value());
    BOOST_REQUIRE(forwarded.has_value());
    BOOST_TEST(owned->valueType == runtime.getIntegerType());
    BOOST_TEST(owned->iteratorType == runtime.getIteratorType(runtime.getIntegerType()));
    BOOST_TEST(forwarded->factoryId == owned->factoryId);
    BOOST_REQUIRE(runtime.getMethod(owned->factoryId) != nullptr);
    BOOST_TEST(runtime.getMethod(owned->factoryId)->getObjectType() == range);
}

BOOST_AUTO_TEST_CASE(range_evaluates_bounds_once)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<NomadInteger> calls;
    for (auto index = NomadInteger{1}; index <= 3; ++index) {
        runtime.registerNativeFunction(
            "test.bound" + std::to_string(index),
            [&calls, index](VirtualMachine* machine) {
                calls.push_back(index);
                machine->setIntegerResult(index == 2 ? 3 : 1);
            },
            {}, runtime.getIntegerType(), ""
        );
    }
    fixture.addFunction("main",
        "total = 0\n"
        "for value in range test.bound1 test.bound2 test.bound3\n"
        "    total = total + value\n"
        "end\n"
        "return total"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 3);
    std::ranges::sort(calls);
    BOOST_TEST(calls == std::vector<NomadInteger>({1, 2, 3}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(range_loops_lower_to_generic_calls_and_jumps)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "for value in range 3\nif value == 1 continue\nend\nreturn 0");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    BOOST_TEST(runtime.getInstructionId("op_numeric_for_test").isInvalid());
    BOOST_TEST(runtime.getInstructionId("op_numeric_for_advance").isInvalid());
    std::ostringstream instructions;
    runtime.dumpInstructions(instructions, runtime.getScriptFunction(runtime.getFunctionId("main")));
    const auto text = instructions.str();
    BOOST_TEST(text.find("op_jump_if_false") != NomadString::npos);
    BOOST_TEST(text.find("op_jump") != NomadString::npos);
    BOOST_TEST(text.find("numeric_for") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(for_uses_registered_iteration_not_collection_names)
{
    class SequenceIterator final : public IteratorObject {
    public:
        explicit SequenceIterator(const Type* type): IteratorObject(type) {}
    protected:
        bool onMoveNext(RuntimeValue& value) override {
            if (m_next > 3) {
                return false;
            }
            value.setIntegerValue(m_next++);
            return true;
        }
    private:
        NomadInteger m_next = 1;
    };
    class SequenceObject final : public NomadObject {};

    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto typeId = runtime.createObjectType("Sequence", ObjectTypeMode::Closed);
    BOOST_REQUIRE(typeId.isValid());
    auto* sourceType = const_cast<ObjectType*>(runtime.getType(typeId).value()->asObjectType());
    BOOST_REQUIRE(runtime.addConstructor(sourceType->getId(), {
        .parameters = {},
        .factory = [](std::span<const RuntimeValue>) {
            return new SequenceObject;
        },
        .documentation = ""
    }).isValid());
    auto factoryCalls = 0;
    const auto factoryId = runtime.addMethod(sourceType->getId(), {
        .name = "startTraversal",
        .parameters = {},
        .returnTypeId = runtime.getIteratorType(runtime.getIntegerType())->getId(),
        .function = [&factoryCalls](VirtualMachine* machine) {
            ++factoryCalls;
            machine->getResult().setObjectValue(new SequenceIterator(machine->getRuntime()->getIntegerType()));
        },
        .documentation = ""
    });
    sourceType->registerIterableType(runtime, runtime.getIntegerType(), runtime.getNativeMemberMethod(factoryId));
    fixture.addFunction("main",
        "total = 0\n"
        "for value in new Sequence\n"
        "    total = total + value\n"
        "end\n"
        "return total"
    );
    fixture.addFunction("breakEarly",
        "total = 0\n"
        "for value in new Sequence\n"
        "    total = value\n"
        "    break\n"
        "end\n"
        "return total"
    );
    fixture.addFunction("returnEarly",
        "for value in new Sequence\n"
        "    return value\n"
        "end\n"
        "return 0"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto before = NomadObject::getLiveObjectCount();
    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 6);
    BOOST_TEST(factoryCalls == 1);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    for (const auto* function : {"breakEarly", "returnEarly"}) {
        const auto earlyOutcome = fixture.execute(function);
        BOOST_TEST(!earlyOutcome.fault.has_value());
        BOOST_TEST(earlyOutcome.integerValue == 1);
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    }
    BOOST_TEST(factoryCalls == 3);
}

BOOST_AUTO_TEST_CASE(range_is_exclusive_and_directional)
{
    const auto outcome = runSource(
        "total = 0\n"
        "for value in range 1 6 2\n"
        "    if value == 3 continue\n"
        "    total = total + value\n"
        "end\n"
        "for value in range 3 0 (-1)\n"
        "    total = total + value\n"
        "end\n"
        "for value in range 5 1\n"
        "    total = 1000\n"
        "end\n"
        "return total"
    );
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 12);
}

BOOST_AUTO_TEST_CASE(range_stops_without_integer_overflow)
{
    for (const auto* range : {
        "9223372036854775806 9223372036854775807 2",
        "(-9223372036854775807) (-9223372036854775808) (-2)",
        "0 (-9223372036854775808) (-9223372036854775808)"
    }) {
        const auto outcome = runSource(
            "count = 0\nfor value in range " + NomadString{range} +
            "\ncount = count + 1\nend\nreturn count"
        );
        BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
        BOOST_TEST(!outcome.fault.has_value());
        BOOST_TEST(outcome.integerValue == 1);
    }
}

BOOST_AUTO_TEST_CASE(iterable_for_checks_source_and_binding)
{
    for (const auto* source : {
        "for value in true\nend",
        "for value in range 1 3 1.0\nend",
        "value = \"wrong type\"\nfor value in range 1 3\nend",
        "params value:string\nfor value in range 1 3\nend",
        "for qualified.value in range 1 3\nend",
        "const value = 1\nfor value in range 1 3\nend"
    }) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
    LanguageTestFixture fixture;
    fixture.addFunction("main", "for value in range 3 1 0\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_CHECK_EXCEPTION((void)fixture.execute("main"), NomadException, [](const NomadException& error) {
        return NomadString(error.what()).find("Range step must not be zero") != NomadString::npos;
    });
}

BOOST_AUTO_TEST_CASE(iterable_for_supports_format_placeholders)
{
    const auto outcome = runSource("for value in range 2 3\nreturn $\"{value} {value + 1}\"\nend\nreturn \"\"");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "2 3");
    BOOST_TEST(outcome.leakedStrings == 0);
    const auto dynamic = runSource(
        "for value in range 2 3\nreturn $(\"{value} {value + 1}\")\nend\nreturn \"\""
    );
    BOOST_REQUIRE_MESSAGE(dynamic.compiled, dynamic.diagnostics);
    BOOST_TEST(dynamic.text == "2 3");
    BOOST_TEST(dynamic.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(loop_bindings_are_mutable_persistent_and_independent_of_iterator_position)
{
    const auto outcome = runSource(
        "value = 20\n"
        "for value in range 0\nend\n"
        "total = value\n"
        "for value in range 3\n"
        "    total = total + value\n"
        "    value = 100\n"
        "end\n"
        "total = total + value\n"
        "for value in range 5\n"
        "    value = 7\n"
        "    break\n"
        "end\n"
        "for unused in range 0\nend\n"
        "return total + value + unused");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 130);
}

BOOST_AUTO_TEST_CASE(nested_loops_reusing_a_binding_have_independent_iterators)
{
    const auto outcome = runSource(
        "total = 0\n"
        "for value in range 2\n"
        "    total = total + value\n"
        "    for value in range 3\n"
        "        total = total + value\n"
        "    end\n"
        "end\n"
        "return total * 10 + value");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 72);
}

BOOST_AUTO_TEST_CASE(parameters_can_be_used_as_loop_bindings_without_changing_callers)
{
    const auto outcome = runSource(
        "fun iterate value:int\n"
        "    for value in range 3\n"
        "        value = value + 10\n"
        "    end\n"
        "    return value\n"
        "end\n"
        "value = 20\n"
        "result = iterate value\n"
        "return result * 100 + value");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 1220);
}

BOOST_AUTO_TEST_CASE(range_values_and_active_iterators_are_independent_of_later_assignments)
{
    const auto outcome = runSource(
        "start = 1\nstop = 4\nincrement = 1\n"
        "sequence = range start stop increment\n"
        "start = 100\nstop = 100\nincrement = 0\n"
        "total = 0\n"
        "for value in sequence\n"
        "    sequence = range 0\n"
        "    total = total + value\n"
        "end\n"
        "for value in sequence\n"
        "    total = 1000\n"
        "end\n"
        "return total * 10 + value");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 63);
}

BOOST_AUTO_TEST_CASE(equal_range_bounds_are_empty_for_both_step_directions)
{
    const auto outcome = runSource(
        "value = 42\n"
        "for value in range 3 3\nvalue = 1000\nend\n"
        "for value in range 3 3 (-1)\nvalue = 2000\nend\n"
        "return value");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 42);
}

BOOST_AUTO_TEST_CASE(conditional_loops_and_inline_control)
{
    const auto outcome = runSource(
        "value = 0\n"
        "total = 0\n"
        "while value < 10\n"
        "    value = value + 1\n"
        "    if value == 3 continue\n"
        "    if value == 6 break\n"
        "    total = total + value\n"
        "end\n"
        "until value == 8\n"
        "    value = value + 1\n"
        "end\n"
        "do\n"
        "    value = value + 1\n"
        "end while false\n"
        "do\n"
        "    value = value + 1\n"
        "    continue\n"
        "end until value == 10\n"
        "return total * 100 + value"
    );
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 1210);
}

BOOST_AUTO_TEST_CASE(loop_control_is_local_to_the_nearest_loop)
{
    const auto outcome = runSource(
        "outer = 0\n"
        "total = 0\n"
        "while outer < 3\n"
        "    outer = outer + 1\n"
        "    inner = 0\n"
        "    do\n"
        "        inner = inner + 1\n"
        "        if inner == 2 break\n"
        "        total = total + 1\n"
        "    end until false\n"
        "end\n"
        "return total"
    );
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 3);
}

BOOST_AUTO_TEST_CASE(loops_reject_invalid_conditions_and_control)
{
    for (const auto* source : {
        "break", "continue", "if true break",
        "while 1\nend", "until \"yes\"\nend",
        "do\nend", "while true\nend until true",
        "while true\nif true break extra\nbreak\nend",
        "while true\nfun nested\nbreak\nend\nbreak\nend"
    }) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(loop_return_releases_owned_locals)
{
    const auto outcome = runSource(
        "text = $\"hello{1}world\"\n"
        "while true\n"
        "    return text\n"
        "end\n"
        "return \"unreachable\""
    );
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "hello1world");
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(if_executes_body_only_when_true)
{
    const auto source =
        "params value:int\n"
        "result = 0\n"
        "if value > 10\n"
        "    result = 1\n"
        "end\n"
        "return result";

    BOOST_TEST(runWithInteger(source, 11).integerValue == 1);
    BOOST_TEST(runWithInteger(source, 10).integerValue == 0);
}

BOOST_AUTO_TEST_CASE(if_else)
{
    const auto source =
        "params value:int\n"
        "if value == 0\n"
        "    result = \"zero\"\n"
        "else\n"
        "    result = \"other\"\n"
        "end\n"
        "return result";

    const auto zero = runWithInteger(source, 0);
    BOOST_REQUIRE_MESSAGE(zero.compiled, zero.diagnostics);
    BOOST_TEST(zero.text == "zero");
    BOOST_TEST(runWithInteger(source, 5).text == "other");
}

BOOST_AUTO_TEST_CASE(else_if_chain_selects_first_matching_branch)
{
    const auto source =
        "params value:int\n"
        "if value < 0\n"
        "    result = 1\n"
        "else if value == 0\n"
        "    result = 2\n"
        "else if value < 10\n"
        "    result = 3\n"
        "else if value < 100\n"
        "    result = 4\n"
        "else\n"
        "    result = 5\n"
        "end\n"
        "return result";

    const std::vector<std::pair<NomadInteger, NomadInteger>> cases = {
        {-5, 1},
        {0, 2},
        {5, 3},
        {9, 3},
        {50, 4},
        {100, 5},
    };

    for (const auto& [argument, expected]: cases) {
        BOOST_TEST_CONTEXT("value = " << argument) {
            const auto outcome = runWithInteger(source, argument);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.integerValue == expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(else_if_without_else)
{
    const auto source =
        "params value:int\n"
        "result = 0\n"
        "if value == 1\n"
        "    result = 10\n"
        "else if value == 2\n"
        "    result = 20\n"
        "end\n"
        "return result";

    BOOST_TEST(runWithInteger(source, 1).integerValue == 10);
    BOOST_TEST(runWithInteger(source, 2).integerValue == 20);
    BOOST_TEST(runWithInteger(source, 3).integerValue == 0);
}

BOOST_AUTO_TEST_CASE(nested_if)
{
    const auto source =
        "params value:int\n"
        "result = 0\n"
        "if value > 0\n"
        "    if value > 10\n"
        "        result = 2\n"
        "    else\n"
        "        result = 1\n"
        "    end\n"
        "else\n"
        "    if value < -10\n"
        "        result = -2\n"
        "    end\n"
        "end\n"
        "return result";

    BOOST_TEST(runWithInteger(source, 20).integerValue == 2);
    BOOST_TEST(runWithInteger(source, 5).integerValue == 1);
    BOOST_TEST(runWithInteger(source, -5).integerValue == 0);
    BOOST_TEST(runWithInteger(source, -20).integerValue == -2);
}

BOOST_AUTO_TEST_CASE(empty_if_blocks_are_allowed)
{
    const auto outcome = runWithInteger(
        "params value:int\n"
        "if value > 0\n"
        "else\n"
        "end\n"
        "return value",
        3
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 3);
}

BOOST_AUTO_TEST_CASE(early_return_exits_function)
{
    const auto source =
        "params value:int\n"
        "if value > 0\n"
        "    return 1\n"
        "end\n"
        "return 0";

    BOOST_TEST(runWithInteger(source, 5).integerValue == 1);
    BOOST_TEST(runWithInteger(source, -5).integerValue == 0);
}

BOOST_AUTO_TEST_CASE(void_return_exits_function)
{
    LanguageTestFixture fixture;
    fixture.addFunction(
        "main",
        "params value:int\n"
        "global.reached = false\n"
        "if value > 0\n"
        "    return\n"
        "end\n"
        "global.reached = true"
    );
    fixture.addFunction("reached", "return global.reached");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto skipped = fixture.execute("main", {RuntimeValue(NomadInteger{1})});
    BOOST_TEST(skipped.returnTypeName == "void");
    BOOST_TEST(fixture.execute("reached").booleanValue == false);

    BOOST_TEST(!fixture.execute("main", {RuntimeValue(NomadInteger{-1})}).fault.has_value());
    BOOST_TEST(fixture.execute("reached").booleanValue == true);
}

BOOST_AUTO_TEST_CASE(malformed_if_statements_are_rejected)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"if true\n    a = 1", ""},
        {"if\n    a = 1\nend", ""},
        {"if true false\n    a = 1\nend", ""},
        {"if true\n    a = 1\nelse 1\n    a = 2\nend", ""},
        {"if true\nelse\nelse\nend", ""},
        {"if 1\nend", "must be a boolean"},
        {"if true\nelse if 1\nend", "must be a boolean"},
        {"else\nend", ""},
        {"end", ""},
    };

    for (const auto& [source, fragment]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto errors = compileErrors(source);

            BOOST_TEST(!errors.empty());
            BOOST_TEST(errors.find(fragment) != std::string::npos, errors);
        }
    }
}

BOOST_AUTO_TEST_CASE(legacy_numeric_for_is_rejected_and_old_keywords_are_identifiers)
{
    BOOST_TEST(!compileErrors("for i = 1\nend").empty());
    BOOST_TEST(!compileErrors("for i = 1 to 3\nend").empty());
    const auto outcome = runSource("to = 2\nstep = 3\nreturn to + step");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 5);
}

BOOST_AUTO_TEST_CASE(return_types_must_be_consistent)
{
    const std::vector<std::string> sources = {
        "params value:int\nif value > 0\n    return 1\nend\nreturn",
        "params value:int\nif value > 0\n    return\nend\nreturn 1",
        "params value:int\nif value > 0\n    return 1\nend\nreturn 1.0",
        "params value:int\nif value > 0\n    return \"a\"\nend\nreturn true",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(value_returning_functions_must_return_on_every_path)
{
    const std::vector<std::string> sources = {
        // The example from the language guide.
        "fun f x:int\n    if x > 10\n        return 1\n    end\nend\nreturn f 1",
        // Top-level function.
        "params value:int\nif value > 0\n    return 1\nend",
        // `if` without `else`, even when every branch returns.
        "params value:int\nif value > 0\n    return 1\nelse if value < 0\n    return -1\nend",
        // One branch of an `if`/`else` falls through.
        "params value:int\nif value > 0\n    return 1\nelse\n    value2 = 2\nend",
        "params value:int\nif value > 0\n    return 1\nelse if value < 0\n    x = 1\nelse\n    return 0\nend",
        // Nested `if` that falls through.
        "params value:int\nif value > 0\n    if value > 10\n        return 2\n    end\nelse\n    return 0\nend",
        // String return type.
        "fun f x:int\n    if x > 10\n        return \"big\"\n    end\nend\nreturn f 1",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            const auto errors = compileErrors(source);

            BOOST_TEST(errors.find("Missing `return`") != std::string::npos, errors);
        }
    }
}

BOOST_AUTO_TEST_CASE(missing_return_is_reported_at_the_end_of_the_function)
{
    const auto funErrors = compileErrors("fun f x:int\n    if x > 10\n        return 1\n    end\nend\nreturn f 1");
    BOOST_TEST(funErrors.find("main.nomad:5:") != std::string::npos, funErrors);
    BOOST_TEST(funErrors.find("'f'") != std::string::npos, funErrors);
    BOOST_TEST(funErrors.find("`int`") != std::string::npos, funErrors);

    const auto functionErrors = compileErrors("params value:int\nif value > 0\n    return 1\nend");
    BOOST_TEST(functionErrors.find("main.nomad:4:") != std::string::npos, functionErrors);
}

BOOST_AUTO_TEST_CASE(functions_returning_on_every_path_are_accepted)
{
    const std::vector<std::pair<std::string, NomadInteger>> cases = {
        {"params value:int\nif value > 0\n    return 1\nend\nreturn 0", 1},
        {"params value:int\nif value > 0\n    return 1\nelse\n    return 0\nend", 1},
        {"params value:int\nif value < 0\n    return -1\nelse if value == 0\n    return 0\nelse\n    return 1\nend", 1},
        {"params value:int\nif value > 0\n    if value > 10\n        return 2\n    else\n        return 1\n    end\nelse\n    return 0\nend", 1},
        {"params value:int\nfun f x:int\n    if x > 10\n        return 1\n    end\n    return 0\nend\nreturn f (value + 6)", 1},
        // An `if`/`else` that returns on every branch ends a `fun` without a final `return`.
        {"params value:int\nfun sign x:int\n    if x > 0\n        return 1\n    else\n        return 0\n    end\nend\nreturn sign value", 1},
    };

    for (const auto& [source, expected]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto outcome = runWithInteger(source, 5);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.integerValue == expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(void_functions_may_fall_through)
{
    BOOST_TEST(compileErrors("params value:int\nif value > 0\n    return\nend\nx = 1").empty());
    BOOST_TEST(compileErrors("fun f x:int\n    if x > 10\n        return\n    end\nend\nf 1").empty());
}

BOOST_AUTO_TEST_CASE(unreachable_code_is_rejected)
{
    const std::vector<std::string> sources = {
        // The example from the language guide.
        "fun oops\n    return 1\n    x = 2\nend\nreturn oops",
        // Void `return`.
        "fun f\n    return\n    x = 2\nend\nf",
        "x = 1\nreturn x\nassert x == 1",
        // After an `if`/`else` that returns on every branch.
        "params value:int\nif value > 0\n    return 1\nelse\n    return 0\nend\nx = 1",
        // Inside a branch.
        "params value:int\nif value > 0\n    return 1\n    x = 2\nend\nreturn 0",
        "params value:int\nif value > 0\n    x = 1\nelse\n    return 0\n    x = 2\nend\nreturn 1",
        // An unreachable `if`.
        "params value:int\nreturn 1\nif value > 0\n    x = 1\nend",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            const auto errors = compileErrors(source);

            BOOST_TEST(errors.find("Unreachable code") != std::string::npos, errors);
        }
    }
}

BOOST_AUTO_TEST_CASE(unreachable_code_is_reported_at_the_first_unreachable_statement)
{
    const auto funErrors = compileErrors("fun oops\n    return 1\n    x = 2\n    x = 3\nend\nreturn oops");
    BOOST_TEST(funErrors.find("main.nomad:3:") != std::string::npos, funErrors);
    BOOST_TEST(funErrors.find("main.nomad:4:") == std::string::npos, funErrors);

    const auto ifErrors = compileErrors("params value:int\nreturn 1\nif value > 0\n    x = 1\nend");
    BOOST_TEST(ifErrors.find("main.nomad:3:") != std::string::npos, ifErrors);
}

BOOST_AUTO_TEST_CASE(declarations_after_return_are_reachable)
{
    const auto outcome = runWithInteger(
        "params value:int\n"
        "return twice value\n"
        "fun twice x:int\n"
        "    return x * 2\n"
        "end\n"
        "const limit = 10",
        4
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 8);
}

BOOST_AUTO_TEST_CASE(assert_passes_when_true)
{
    const auto outcome = runSource("score = 10\nassert score >= 0\nreturn score");

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 10);
}

BOOST_AUTO_TEST_CASE(assert_fails_when_false)
{
    const auto outcome = runSource("score = -1\nassert score >= 0\nreturn score");

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_REQUIRE(outcome.fault.has_value());
    BOOST_TEST(outcome.fault->find("Assertion failed") != std::string::npos, *outcome.fault);
    BOOST_TEST(outcome.fault->find("main") != std::string::npos, *outcome.fault);
    BOOST_TEST(outcome.fault->find("[2]") != std::string::npos, *outcome.fault);
    BOOST_TEST(outcome.fault->find("assert score >= 0") != std::string::npos, *outcome.fault);
}

BOOST_AUTO_TEST_CASE(assert_stops_execution)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "global.after = false\nassert false\nglobal.after = true");
    fixture.addFunction("after", "return global.after");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").fault.has_value());
    BOOST_TEST(fixture.execute("after").booleanValue == false);
}

BOOST_AUTO_TEST_CASE(assert_evaluates_runtime_values)
{
    const auto source = "params value:int\nassert value % 2 == 0\nreturn value";

    const auto even = runWithInteger(source, 4);
    BOOST_REQUIRE_MESSAGE(even.compiled, even.diagnostics);
    BOOST_TEST(!even.fault.has_value());

    const auto odd = runWithInteger(source, 3);
    BOOST_TEST(odd.fault.has_value());
}

BOOST_AUTO_TEST_CASE(assert_requires_boolean_expression)
{
    for (const auto* source: {"assert 1", "assert 1.0", "assert \"text\"", "assert", "assert missing"}) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
