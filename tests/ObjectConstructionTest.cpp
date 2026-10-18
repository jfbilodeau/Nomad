// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;

namespace {

class ConstructedObject final : public NomadObject {
};

const ObjectType* registerObjectType(Runtime& runtime, const NomadString& name) {
    auto descriptor = std::make_unique<ObjectType>(name, std::vector<const Type*>{}, "A constructed object.");
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    return type;
}

NomadId compileConstruction(Runtime& runtime, const NomadString& source) {
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad", source}}}, {}});
    const auto compiled = compiler->compileFunctions(&context);
    BOOST_REQUIRE_MESSAGE(compiled, (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    return runtime.getFunctionId("main");
}

} // namespace

BOOST_AUTO_TEST_SUITE(object_construction)

BOOST_AUTO_TEST_CASE(constructors_accept_heterogeneous_arguments_and_type_specific_zero_values) {
    Runtime runtime;
    auto calls = NomadIndex{0};
    const auto* type = registerObjectType(runtime, "Example");
    BOOST_REQUIRE(runtime.registerConstructor(
        "Example",
        {
            defParameter("number", runtime.getIntegerType(), ""),
            defParameter("text", runtime.getStringType(), ""),
            defParameter("enabled", runtime.getBooleanType(), "")
        },
        type,
        [&calls](const std::span<const RuntimeValue> arguments) {
            BOOST_REQUIRE(arguments.size() == 3u);
            BOOST_TEST(arguments[0].getIntegerValue() == 0);
            BOOST_TEST(arguments[1].getStringObject()->getString() == "hello");
            BOOST_TEST(arguments[2].getBooleanValue());
            ++calls;
            return new ConstructedObject;
        }, ""
    ) != NOMAD_INVALID_ID);
    const auto id = compileConstruction(runtime, "return new Example 0 \"hello\" true");
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
    result.freeObjectValue();
    BOOST_TEST(calls == 1u);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("new Example 0 \"hello\" true", context));
        BOOST_TEST(calls == 2u);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(constructor_faults_and_null_factories_release_object_arguments) {
    for (const auto returnsNull : {false, true}) {
        Runtime runtime;
        const auto* type = registerObjectType(runtime, "Example");
        BOOST_REQUIRE(runtime.registerConstructor(
            "Example", {defParameter("text", runtime.getStringType(), "")}, type,
            [returnsNull](std::span<const RuntimeValue>) -> NomadObject* {
                if (returnsNull) {
                    return nullptr;
                }
                throw NomadException("Constructor failed");
            }, ""
        ) != NOMAD_INVALID_ID);
        const auto id = compileConstruction(runtime, "return new Example (\"hello\".toUpper)");
        const auto before = NomadObject::getLiveObjectCount();
        auto machine = runtime.createVirtualMachine();
        for (auto attempt = 0; attempt < 2; ++attempt) {
            BOOST_CHECK_THROW(machine->run(runtime.getFunction(id), {}), NomadException);
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        }
        {
            InterpreterContext context(*machine);
            BOOST_TEST(!Interpreter{}.execute("new Example (\"hello\".toUpper)", context));
            BOOST_REQUIRE(context.getError() != nullptr);
        }
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    }
}

BOOST_AUTO_TEST_CASE(parameterless_constructors_are_explicitly_registered) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "Plain");
    BOOST_REQUIRE(runtime.registerConstructor(
        "Plain", {}, type, [](std::span<const RuntimeValue>) { return new ConstructedObject; }, ""
    ) != NOMAD_INVALID_ID);
    const auto id = compileConstruction(runtime, "return new Plain");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
    result.freeObjectValue();

}

BOOST_AUTO_TEST_CASE(registration_rejects_collisions_invalid_types_and_missing_factories) {
    Runtime runtime;
    Runtime other;
    const auto* type = registerObjectType(runtime, "Example");
    const auto* otherType = registerObjectType(runtime, "Other");
    const auto factory = [](std::span<const RuntimeValue>) { return new ConstructedObject; };
    const auto parameters = std::vector{defParameter("value", runtime.getIntegerType(), "")};
    const auto id = runtime.registerConstructor("Example", parameters, type, factory, "");
    BOOST_REQUIRE(id != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerConstructor("Example", parameters, type, factory, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerConstructor("Example", parameters, otherType, factory, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerConstructor(
        "Example", {defParameter("renamed", runtime.getIntegerType(), "")}, type, factory, ""
    ) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerConstructor(
        "Example", {defParameter("value", other.getIntegerType(), "")}, type, factory, ""
    ) == NOMAD_INVALID_ID);
    for (const auto* invalid : {runtime.getVoidType(), runtime.getFileNameType(), static_cast<const Type*>(nullptr)}) {
        BOOST_TEST(runtime.registerConstructor(
            "Invalid", {defParameter("value", invalid, "")}, type, factory, ""
        ) == NOMAD_INVALID_ID);
    }
    BOOST_TEST(runtime.registerConstructor("Example", {}, nullptr, factory, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerConstructor(
        "Example", {}, registerObjectType(other, "Foreign"), factory, ""
    ) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerConstructor("", {}, type, factory, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerConstructor("Example", {}, type, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getConstructors().size() == 1u);
    ConstructorDefinition definition;
    BOOST_REQUIRE(runtime.getConstructorDefinition(id, definition));
    BOOST_TEST(definition.returnType == type);
    BOOST_TEST(!runtime.getConstructorDefinition(NOMAD_INVALID_ID, definition));
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"invalid", {"invalid.nomad", "return new Example true"}}}, {}});
    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->sourceName == "invalid.nomad");
    BOOST_TEST(context.getFirstError()->message.find("No matching constructor") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(overloads_select_by_argument_types_and_count_with_distinct_return_types) {
    Runtime runtime;
    const auto* singleType = registerObjectType(runtime, "Single");
    const auto* pairType = registerObjectType(runtime, "Pair");
    const auto* textType = registerObjectType(runtime, "Text");
    const auto* emptyType = registerObjectType(runtime, "Empty");
    NomadString calls;
    const auto registerFactory = [&](const std::vector<FunctionParameterDefinition>& parameters,
                                     const ObjectType* type, const NomadString& marker) {
        BOOST_REQUIRE(runtime.registerConstructor(
            "Example", parameters, type,
            [&calls, marker](std::span<const RuntimeValue>) {
                calls += marker;
                return new ConstructedObject;
            }, ""
        ) != NOMAD_INVALID_ID);
    };
    registerFactory({}, emptyType, "E");
    registerFactory({defParameter("value", runtime.getIntegerType(), "")}, singleType, "I");
    registerFactory(
        {defParameter("first", runtime.getIntegerType(), ""), defParameter("second", runtime.getIntegerType(), "")},
        pairType, "P"
    );
    registerFactory({defParameter("value", runtime.getStringType(), "")}, textType, "S");
    runtime.registerNativeFunction(
        "consume",
        [](VirtualMachine* machine) { machine->setIntegerResult(machine->getIntegerParameter(1)); },
        {defParameter("object", singleType, ""), defParameter("value", runtime.getIntegerType(), "")},
        runtime.getIntegerType(), ""
    );
    const auto id = compileConstruction(runtime,
        "empty = (new Example)\n"
        "single = (new Example 1)\n"
        "pair = new Example 1 2\n"
        "text = new Example \"hello\"\n"
        "assert (consume (new Example 1) 2) == 2\n"
        "return pair"
    );
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    result.freeObjectValue();
    BOOST_TEST(calls == "EIPSI");
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        for (const auto* source : {"new Example", "(new Example 1)", "new Example 1 2", "new Example \"hello\""}) {
            BOOST_REQUIRE(Interpreter{}.execute(source, context));
        }
        BOOST_TEST(context.getResult()->getType() == textType);
        BOOST_REQUIRE(Interpreter{}.execute("new Example 1 2", context));
        BOOST_TEST(context.getResult()->getType() == pairType);
        BOOST_REQUIRE(Interpreter{}.execute("(new Example 1)", context));
        BOOST_TEST(context.getResult()->getType() == singleType);
        BOOST_REQUIRE(Interpreter{}.execute("consume (new Example 1) 2", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 2);
        BOOST_TEST(!Interpreter{}.execute("new Example true", context));
        BOOST_TEST(!Interpreter{}.execute("new Example 1 true", context));
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(parameterized_constructor_registration_validates_and_reuses_specializations) {
    Runtime runtime;
    Runtime other;
    const auto* type = registerObjectType(runtime, "Example");
    auto calls = NomadIndex{0};
    const auto initializer = [&](const NomadString& name, const std::vector<const Type*>& arguments) {
        ++calls;
        BOOST_REQUIRE(runtime.registerConstructor(
            name, {defParameter("value", arguments[0], "")}, type,
            [](std::span<const RuntimeValue>) { return new ConstructedObject; }, ""
        ) != NOMAD_INVALID_ID);
    };
    BOOST_TEST(!runtime.registerParameterizedConstructor("", 1, initializer));
    BOOST_TEST(!runtime.registerParameterizedConstructor("Create", 0, initializer));
    BOOST_TEST(!runtime.registerParameterizedConstructor("Create", 1, {}));
    BOOST_TEST(!runtime.registerParameterizedConstructor("int", 1, initializer));
    BOOST_REQUIRE(runtime.registerParameterizedConstructor("Create", 1, initializer));
    BOOST_TEST(!runtime.registerParameterizedConstructor("Create", 1, initializer));
    BOOST_TEST(runtime.registerConstructor(
        "Create", {}, type, [](std::span<const RuntimeValue>) { return new ConstructedObject; }, ""
    ) == NOMAD_INVALID_ID);
    BOOST_TEST(!runtime.registerParameterizedType("Create", 1, [](const auto&) {
        return std::make_unique<ObjectType>("Collision");
    }));
    BOOST_TEST(!runtime.getParameterizedConstructorArgumentCount("Missing").has_value());
    BOOST_REQUIRE(runtime.getParameterizedConstructorArgumentCount("Create").has_value());
    BOOST_TEST(*runtime.getParameterizedConstructorArgumentCount("Create") == 1u);
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {runtime.getIntegerType()}) == "Create<int>");
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {runtime.getIntegerType()}) == "Create<int>");
    BOOST_TEST(calls == 1u);
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {other.getIntegerType()}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {runtime.getVoidType()}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {nullptr}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Missing", {runtime.getIntegerType()}).empty());
    BOOST_TEST(calls == 1u);
    const auto id = compileConstruction(runtime, "return new Create<int> 42");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    result.freeObjectValue();
    BOOST_TEST(calls == 1u);
}

BOOST_AUTO_TEST_CASE(failed_parameterized_initialization_is_not_cached_as_success) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "Example");
    auto attempts = 0;
    BOOST_REQUIRE(runtime.registerParameterizedConstructor(
        "Create", 1,
        [&](const NomadString& name, const std::vector<const Type*>&) {
            if (attempts++ == 0) {
                BOOST_REQUIRE(runtime.registerConstructor(
                    name, {}, type, [](std::span<const RuntimeValue>) { return new ConstructedObject; }, ""
                ) != NOMAD_INVALID_ID);
            }
            throw NomadException("Initialization failed");
        }
    ));
    for (auto attempt = 0; attempt < 2; ++attempt) {
        BOOST_CHECK_THROW(
            static_cast<void>(runtime.getParameterizedConstructorName("Create", {runtime.getIntegerType()})),
            NomadException
        );
    }
    BOOST_TEST(attempts == 2);
}

BOOST_AUTO_TEST_SUITE_END()
