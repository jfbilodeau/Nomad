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

class ConstructorType final : public ObjectType {
public:
    using Constructor = std::function<NomadObject*(std::span<const RuntimeValue>)>;

    ConstructorType(std::vector<const Type*> parameters, Constructor constructor);
    [[nodiscard]] bool isConstructible() const override;
    [[nodiscard]] std::span<const Type* const> getConstructorParameterTypes() const override;
    [[nodiscard]] NomadObject* createObject(std::span<const RuntimeValue> arguments) const override;

private:
    std::vector<const Type*> m_parameters;
    Constructor m_constructor;
};

ConstructorType::ConstructorType(std::vector<const Type*> parameters, Constructor constructor):
    ObjectType("Example"),
    m_parameters(std::move(parameters)),
    m_constructor(std::move(constructor))
{}

bool ConstructorType::isConstructible() const {
    return true;
}

std::span<const Type* const> ConstructorType::getConstructorParameterTypes() const {
    return m_parameters;
}

NomadObject* ConstructorType::createObject(const std::span<const RuntimeValue> arguments) const {
    return m_constructor(arguments);
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
    runtime.registerType(std::make_unique<ConstructorType>(
        std::vector<const Type*>{runtime.getIntegerType(), runtime.getStringType(), runtime.getBooleanType()},
        [&calls](const std::span<const RuntimeValue> arguments) {
            BOOST_REQUIRE(arguments.size() == 3u);
            BOOST_TEST(arguments[0].getIntegerValue() == 0);
            BOOST_TEST(arguments[1].getStringObject()->getString() == "hello");
            BOOST_TEST(arguments[2].getBooleanValue());
            ++calls;
            return new ConstructedObject;
        }
    ));
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
        runtime.registerType(std::make_unique<ConstructorType>(
            std::vector<const Type*>{runtime.getStringType()},
            [returnsNull](std::span<const RuntimeValue>) -> NomadObject* {
                if (returnsNull) {
                    return nullptr;
                }
                throw NomadException("Constructor failed");
            }
        ));
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

BOOST_AUTO_TEST_CASE(parameterless_factories_keep_working_and_parameter_types_must_be_registered) {
    Runtime runtime;
    runtime.registerType(std::make_unique<ObjectType>(
        "Plain", std::vector<const Type*>{}, [] { return new ConstructedObject; }
    ));
    const auto id = compileConstruction(runtime, "return new Plain");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
    result.freeObjectValue();

    Runtime other;
    runtime.registerType(std::make_unique<ConstructorType>(
        std::vector<const Type*>{other.getIntegerType()},
        [](std::span<const RuntimeValue>) { return new ConstructedObject; }
    ));
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"invalid", {"invalid.nomad", "return new Example 0"}}}, {}});
    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->message.find("constructor parameter type") != NomadString::npos);
}

BOOST_AUTO_TEST_SUITE_END()
