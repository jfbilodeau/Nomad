// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1Type.hpp>
#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/Array2Type.hpp>
#include <nomad/collections/Array2dObject.hpp>
#include <nomad/collections/Array3Type.hpp>
#include <nomad/collections/Array3dObject.hpp>
#include <nomad/collections/ArrayType.hpp>
#include <nomad/collections/ListObject.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <fstream>
#include <iterator>
#include <sstream>

using namespace nomad;
using namespace nomad::test;

namespace {

std::vector<ModuleManifest> fixtureModules() {
    auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-system"});
    modules.push_back({"fixture", NOMAD_TEST_MODULE_DIRECTORY, {"nomad-system"}});
    return modules;
}

NomadString events(const TestDirectory& directory) {
    std::ifstream input(directory.getPath() / "module-events.txt");
    return {std::istreambuf_iterator<char>(input), {}};
}

bool compileFixture(ModuleHost& host, const CompilationTarget target) {
    auto compiler = host.getRuntime()->createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"init", {"init.nomad", "fixture.exit"}}}, {}});
    return compiler->compileScripts(&context, target);
}

} // namespace

BOOST_AUTO_TEST_SUITE(module_host)

BOOST_AUTO_TEST_CASE(collections_constructs_arrays_from_nomad_and_interpreter) {
    TestDirectory directory("nomad_host_array_construction");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    BOOST_REQUIRE(dynamic_cast<const Array1Type*>(
        runtime.getParameterizedType("Array1", {runtime.getIntegerType()})
    ) != nullptr);
    BOOST_REQUIRE(dynamic_cast<const Array2Type*>(
        runtime.getParameterizedType("Array2", {runtime.getStringType()})
    ) != nullptr);
    BOOST_REQUIRE(dynamic_cast<const Array3Type*>(
        runtime.getParameterizedType("Array3", {runtime.getFloatType()})
    ) != nullptr);
    NomadString calls;
    runtime.registerNativeFunction(
        "size",
        [&calls](VirtualMachine* machine) {
            const auto value = machine->getIntegerParameter(0);
            calls += std::to_string(value);
            machine->setIntegerResult(value);
        },
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    );
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad",
        "line = new Array1<int> 10\n"
        "grid = new Array2<string> (size 2) (size 3)\n"
        "volume = new Array3<float> 2 3 4\n"
        "alias = grid\n"
        "assert alias.count == 6\n"
        "assert line.count == 10\n"
        "assert volume.count == 24\n"
        "assert (new Array1<int> (2 + 3)).count == 5\n"
        "arrays = new List<Array1<int>>\n"
        "arrays.push line\n"
        "assert arrays[0].count == 10\n"
        "return new Array1<Array2<int>> 2"
    }}}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled, (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    const auto* nestedType = runtime.getParameterizedType("Array2", {runtime.getIntegerType()});
    RuntimeValue empty;
    nestedType->initValue(empty);
    nestedType->freeValue(empty);
    // The process-wide empty string is retained after the runtime's arrays are released.
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(calls == "32");
    const auto* outer = result.asObject<Array1dObject>();
    BOOST_TEST(outer->getCount() == 2u);
    BOOST_TEST(outer->getValue(0).getObjectValue() == outer->getValue(1).getObjectValue());
    BOOST_TEST(outer->getValue(0).asObject<Array2dObject>()->getCount() == 0u);
    result.freeObjectValue();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    calls.clear();
    {
        VirtualMachine machine(&runtime);
        InterpreterContext evaluation(machine);
        for (const auto* source : {
            "line = new Array1<int> 10", "grid = new Array2<string> (size 2) (size 3)",
            "volume = new Array3<float> 2 3 4", "grid.count"
        }) {
            const auto executed = Interpreter{}.execute(source, evaluation);
            BOOST_REQUIRE_MESSAGE(executed, (evaluation.getError() ? *evaluation.getError() : source));
        }
        BOOST_TEST(calls == "23");
        BOOST_TEST(evaluation.getResult()->getValue().getIntegerValue() == 6);
        const auto* grid = evaluation.getVariableValue("grid")->asObject<Array2dObject>();
        BOOST_TEST(grid->getDimensionSize(0) == 2u);
        BOOST_TEST(grid->getDimensionSize(1) == 3u);
        BOOST_TEST(runtime.getStringType()->toString(grid->getValue(1, 2)) == "");
        BOOST_TEST(evaluation.getVariableValue("volume")->asObject<Array3dObject>()->getValue(1, 2, 3).getFloatValue() == 0.0f);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    std::ostringstream documentation;
    generateTypeDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("new Array2<string> size1:int size2:int") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(array_dimension_values_fail_at_runtime_not_compilation) {
    for (const auto* source : {
        "return new Array1<int> 0", "return new Array2<int> 1 0",
        "return new Array3<int> 1 0 1", "return new Array1<int> -1",
        "return new Array2<int> 9223372036854775807 2"
    }) {
        BOOST_TEST_CONTEXT(source) {
            TestDirectory directory("nomad_host_invalid_array_dimensions");
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
            ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
            auto& runtime = *host.getRuntime();
            auto compiler = runtime.createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"main", {"main.nomad", source}}}, {}});
            const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
            BOOST_REQUIRE_MESSAGE(compiled, (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
            const auto before = NomadObject::getLiveObjectCount();
            auto machine = runtime.createVirtualMachine();
            BOOST_CHECK_THROW(machine->run(runtime.getScriptFunction(runtime.getFunctionId("main")), {}), NomadException);
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
            InterpreterContext evaluation(*machine);
            BOOST_TEST(!Interpreter{}.execute(NomadString(source).substr(7), evaluation));
            BOOST_REQUIRE(evaluation.getError() != nullptr);
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        }
    }
}

BOOST_AUTO_TEST_CASE(array_constructor_overloads_return_rank_specific_types) {
    TestDirectory directory("nomad_host_array_overloads");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    NomadString calls;
    runtime.registerNativeFunction(
        "size",
        [&calls](VirtualMachine* machine) {
            const auto value = machine->getIntegerParameter(0);
            calls += std::to_string(value);
            machine->setIntegerResult(value);
        },
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    );
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{
        "main", {"main.nomad",
            "line = new Array<int> (size 2)\n"
            "grid = new Array<int> (size 3) (size 4)\n"
            "volume = new Array<int> (size 5) (size 6) (size 7)\n"
            "assert (lineCount line) == 2\n"
            "assert (gridCount grid) == 12\n"
            "assert (volumeCount volume) == 210\n"
            "return grid"
        }
    }, {
        "lineCount", {"lineCount.nomad", "params value:Array1<int>\nreturn value.count"}
    }, {
        "gridCount", {"gridCount.nomad", "params value:Array2<int>\nreturn value.count"}
    }, {
        "volumeCount", {"volumeCount.nomad", "params value:Array3<int>\nreturn value.count"}
    }}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled, (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.asObject<Array2dObject>()->getCount() == 12u);
    result.freeObjectValue();
    BOOST_TEST(calls == "243765");
    const auto constructorCount = runtime.getConstructors().size();
    BOOST_TEST(runtime.getParameterizedConstructorName("Array", {runtime.getIntegerType()}) == "Array<int>");
    BOOST_TEST(runtime.getConstructors().size() == constructorCount);
    {
        VirtualMachine machine(&runtime);
        InterpreterContext evaluation(machine);
        for (const auto& [source, rank] : std::vector<std::pair<NomadString, NomadIndex>>{
            {"new Array<int> 2", 1}, {"new Array<int> 3 4", 2}, {"new Array<int> 5 6 7", 3}
        }) {
            BOOST_REQUIRE(Interpreter{}.execute(source, evaluation));
            const auto* type = dynamic_cast<const ArrayType*>(evaluation.getResult()->getType());
            BOOST_REQUIRE(type != nullptr);
            BOOST_TEST(type->getDimensionCount() == rank);
        }
        for (const auto* source : {
            "new Array<int>", "new Array<int> 0", "new Array<int> 1 (-1)",
            "new Array<int> 1 2 0", "new Array<int> 1 \"two\"",
            "new Array<int> 1 2 true", "new Array<int> 1 2 3 4",
            "new Array<> 1", "new Array<int string> 1", "new Array<void> 1"
        }) {
            BOOST_TEST_CONTEXT(source) {
                BOOST_TEST(!Interpreter{}.execute(source, evaluation));
                BOOST_REQUIRE(evaluation.getError() != nullptr);
            }
        }
    }
    std::ostringstream documentation;
    generateTypeDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("new Array<int> size1:int size2:int` -> `Array2<int>") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(array_constructor_signatures_are_checked_by_compiler) {
    for (const auto* source : {
        "return new Array1<int>", "return new Array2<int> 2", "return new Array3<int> 2 3",
        "return new Array1<int> 1 2", "return new Array2<int> 1 \"two\"",
        "return new Array3<int> 1 2 true", "return new Array1<int> 1.0",
        "return new Array1<int string> 2"
    }) {
        BOOST_TEST_CONTEXT(source) {
            TestDirectory directory("nomad_host_array_constructor_types");
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
            ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
            auto compiler = host.getRuntime()->createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"main", {"main.nomad", source}}}, {}});
            BOOST_TEST(!compiler->compileScripts(&context, CompilationTarget::Analysis));
            BOOST_REQUIRE(context.getFirstError() != nullptr);
        }
    }
}

BOOST_AUTO_TEST_CASE(collections_lists_store_typed_values_and_share_mutations) {
    TestDirectory directory("nomad_host_list_values");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad",
        "fun append values:List<int>\n"
        "    values.push 30\n"
        "end\n"
        "values = new List<int>\n"
        "assert values.count == 0\n"
        "values.push 10\n"
        "alias = values\n"
        "alias.push 20\n"
        "append values\n"
        "assert alias.count == 3\n"
        "words = new List<string>\n"
        "words.push \"hello\"\n"
        "words.push \"world\".toUpper\n"
        "assert words.count == 2\n"
        "nested = new List<List<string>>\n"
        "nested.push words\n"
        "nested.push new List<string>\n"
        "assert nested.count == 2\n"
        "return values.count"
    }}}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 3);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);

    {
        VirtualMachine machine(&runtime);
        InterpreterContext interpreterContext(machine);
        for (const auto* source : {
            "values = new List<string>", "values.push \"hello\"", "alias = values",
            "alias.push \"world\".toUpper", "values.count"
        }) {
            BOOST_REQUIRE(Interpreter{}.execute(source, interpreterContext));
        }
        BOOST_TEST(interpreterContext.getResult()->getValue().getIntegerValue() == 2);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collections_lists_reject_invalid_member_operations) {
    for (const auto* source : {
        "values = new List<int>\nvalues.push \"wrong\"",
        "values = new List<string>\nvalues.push 1",
        "values = new List<List<int>>\nvalues.push new List<string>",
        "values = new List<int>\nvalues.count = 10",
        "values = new List<int>\nvalues.push",
        "values = new List<int>\nvalues[0] = \"wrong\"",
        "values = new List<int>\nreturn values[\"key\"]",
        "values = new List<int>\nreturn values[0 1]",
        "values = new List<int>\nreturn values.push 1",
        "params values:List<int>\nvalues = new List<int>"
    }) {
        BOOST_TEST_CONTEXT(source) {
            TestDirectory directory("nomad_host_bad_list_values");
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
            ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
            auto compiler = host.getRuntime()->createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"main", {"main.nomad", source}}}, {}});
            BOOST_TEST(!compiler->compileScripts(&context, CompilationTarget::Analysis));
            BOOST_TEST(context.hasError());
        }
    }
}

BOOST_AUTO_TEST_CASE(collections_indexers_read_replace_chain_and_preserve_evaluation_order) {
    TestDirectory directory("nomad_host_indexers");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad",
        "fun receiver values:List<int>\n"
        "    values.push 1\n"
        "    return values\n"
        "end\n"
        "fun index values:List<int>\n"
        "    values.push 2\n"
        "    return 0\n"
        "end\n"
        "fun assigned values:List<int>\n"
        "    values.push 3\n"
        "    return 9\n"
        "end\n"
        "fun replace values:List<string>\n"
        "    values[0] = \"changed\"\n"
        "end\n"
        "values = new List<int>\n"
        "values.push 0\n"
        "(receiver values)[index values] = assigned values\n"
        "assert values.count == 4\n"
        "assert values[0] == 9\n"
        "assert values[1] == 1\n"
        "assert values[2] == 2\n"
        "assert values[3] == 3\n"
        "words = new List<string>\n"
        "words.push \"hello\"\n"
        "words[0] = words[0]\n"
        "assert words[0] == \"hello\"\n"
        "replace words\n"
        "assert words[0].length == 7\n"
        "assert words[0][0] == \"c\"\n"
        "nested = new List<List<string>>\n"
        "nested.push words\n"
        "nested[0][0] = \"updated\"\n"
        "assert words[0] == \"updated\"\n"
        "nested[0] = new List<string>\n"
        "nested[0].push \"new\"\n"
        "assert nested[0][0] == \"new\"\n"
        "assert words[0] == \"updated\"\n"
        "return $\"{words[0]}:{values[0]}\""
    }}}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getStringObject()->getString() == "updated:9");
    runtime.getStringType()->freeValue(result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(array1_indexers_read_replace_and_preserve_references) {
    TestDirectory directory("nomad_host_array1_indexers");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad",
        "fun replace values:Array1<string>\n"
        "    values[0] = \"changed\"\n"
        "end\n"
        "values = new Array<int> 2\n"
        "assert values[0] == 0\n"
        "values[1] = 123\n"
        "alias = values\n"
        "alias[0] = values[1]\n"
        "assert values[0] == 123\n"
        "assert values.count == 2\n"
        "words = new Array1<string> 1\n"
        "words[0] = \"hello\"\n"
        "words[0] = words[0]\n"
        "replace words\n"
        "assert words[0][0] == \"c\"\n"
        "nested = new Array1<List<int>> 1\n"
        "nested[0].push 7\n"
        "saved = nested[0]\n"
        "nested[0] = new List<int>\n"
        "assert saved[0] == 7\n"
        "assert nested[0].count == 0\n"
        "return words[0].length + values[1]"
    }}}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 130);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(array1_indexers_work_in_interpreter_and_report_bounds_errors) {
    TestDirectory directory("nomad_host_array1_interpreter");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    VirtualMachine machine(&runtime);
    InterpreterContext context(machine);
    for (const auto* source : {
        "values = new Array<int> 2", "values[0] = 123", "alias = values",
        "alias[1] = values[0]", "values[1]"
    }) {
        BOOST_REQUIRE_MESSAGE(Interpreter{}.execute(source, context),
            (context.getError() ? *context.getError() : source));
    }
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 123);
    for (const auto* source : {"values[-1]", "values[2]", "values[-1] = 9", "values[2] = 9"}) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!Interpreter{}.execute(source, context));
            BOOST_REQUIRE(context.getError() != nullptr);
            BOOST_TEST(context.getError()->find("out of bounds") != NomadString::npos);
        }
    }
    for (const auto* source : {"values[\"key\"]", "values[0] = \"wrong\"", "values[0 1]"}) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!Interpreter{}.execute(source, context));
            BOOST_TEST(context.hasError());
        }
    }
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("values.count", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 2);
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("values[0] + values[1]", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 246);
}

BOOST_AUTO_TEST_CASE(collections_indexers_work_in_interpreter_and_fault_without_growing) {
    TestDirectory directory("nomad_host_interpreter_indexers");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    const auto before = NomadObject::getLiveObjectCount();
    {
        VirtualMachine machine(&runtime);
        InterpreterContext context(machine);
        for (const auto* source : {
            "words = new List<string>", "words.push \"first\"", "alias = words",
            "alias[0] = \"second\"", "words[0] = words[0]", "words[0][0]"
        }) {
            const auto executed = Interpreter{}.execute(source, context);
            BOOST_REQUIRE_MESSAGE(executed,
                (context.getError() ? *context.getError() : "Interpreter failed"));
        }
        BOOST_TEST(context.getResult()->getValue().getStringObject()->getString() == "s");
        for (const auto* source : {"words[-1]", "words[1]", "words[1] = \"third\""}) {
            BOOST_TEST(!Interpreter{}.execute(source, context));
            BOOST_REQUIRE(context.getError() != nullptr);
            BOOST_TEST(context.getError()->find("out of bounds for size 1") != NomadString::npos);
        }
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("words.count", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 1);
        BOOST_REQUIRE(Interpreter{}.execute("empty = new List<int>", context));
        BOOST_TEST(!Interpreter{}.execute("empty[0]", context));
        BOOST_REQUIRE(context.getError() != nullptr);
        BOOST_TEST(context.getError()->find("size 0") != NomadString::npos);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collections_constructs_distinct_reference_counted_lists) {
    TestDirectory directory("nomad_host_list_construction");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    BOOST_TEST(runtime.getKeywordId("new") != NOMAD_INVALID_ID);
    const auto* listType = runtime.getParameterizedType("List", {runtime.getIntegerType()});
    BOOST_REQUIRE(listType != nullptr);
    runtime.registerNativeProperty(
        static_cast<const ObjectTypeBase*>(listType),
        "testPresent",
        runtime.getBooleanType(),
        [](VirtualMachine* machine, RuntimeValue& result) {
            result.setBooleanValue(machine->getThis() != nullptr);
        },
        {},
        ""
    );
    runtime.registerNativeFunction(
        "test.distinct",
        [](VirtualMachine* machine) {
            machine->setBooleanResult(
                machine->getParameter(0).getObjectValue() != machine->getParameter(1).getObjectValue()
            );
        },
        {{"left", listType}, {"right", listType}},
        runtime.getBooleanType(),
        ""
    );
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"init", {"init.nomad",
        "fun identity value:List<int>\n"
        "    return value\n"
        "end\n"
        "fun build\n"
        "    first = new List<int>\n"
        "    alias = first\n"
        "    first = new List<int>\n"
        "    unused = new List<List<string>>\n"
        "    return identity alias\n"
        "end\n"
        "return build"
    }}, {"distinct", {"distinct.nomad", "return test.distinct (new List<int>) (new List<int>)"}},
        {"postfix", {"postfix.nomad", "return new List<int>.testPresent"}}}, {}});
    BOOST_REQUIRE(compiler->compileScripts(&context, CompilationTarget::Executable));
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue first;
    RuntimeValue second;
    runtime.executeFunction(runtime.getFunctionId("init"), {}, first);
    BOOST_REQUIRE(first.getObjectValue() != nullptr);
    BOOST_TEST(first.getObjectValue()->getReferenceCount() == 1u);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before + 1);
    runtime.executeFunction(runtime.getFunctionId("init"), {}, second);
    BOOST_TEST(first.getObjectValue() != second.getObjectValue());
    BOOST_TEST(second.getObjectValue()->getReferenceCount() == 1u);
    RuntimeValue distinct;
    runtime.executeFunction(runtime.getFunctionId("distinct"), {}, distinct);
    BOOST_TEST(distinct.getBooleanValue());
    RuntimeValue postfix;
    runtime.executeFunction(runtime.getFunctionId("postfix"), {}, postfix);
    BOOST_TEST(postfix.getBooleanValue());
    listType->freeValue(first);
    listType->freeValue(second);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);

    {
        VirtualMachine machine(&runtime);
        InterpreterContext interpreterContext(machine);
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("new List<int>.testPresent", interpreterContext));
        BOOST_TEST(interpreterContext.getResult()->getValue().getBooleanValue());
        BOOST_REQUIRE(Interpreter{}.execute("value = new List<int>", interpreterContext));
        const auto* original = interpreterContext.getVariableValue("value")->getObjectValue();
        BOOST_REQUIRE(Interpreter{}.execute("alias = value", interpreterContext));
        BOOST_REQUIRE(Interpreter{}.execute("value = new List<int>", interpreterContext));
        BOOST_TEST(original == interpreterContext.getVariableValue("alias")->getObjectValue());
        BOOST_TEST(original != interpreterContext.getVariableValue("value")->getObjectValue());
        BOOST_TEST(NomadObject::getLiveObjectCount() == before + 2);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(compiler_rejects_invalid_object_construction) {
    for (const auto* source : {
        "return new int", "return new string", "return new void", "return new",
        "return new Missing", "return new List", "return new List<>", "return new List<int> 1",
        "return new Unconstructible"
    }) {
        BOOST_TEST_CONTEXT(source) {
            TestDirectory directory("nomad_host_bad_construction");
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
            ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
            host.getRuntime()->registerType(std::make_unique<ObjectType>("Unconstructible"));
            auto compiler = host.getRuntime()->createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"init", {"init.nomad", source}}}, {}});
            BOOST_TEST(!compiler->compileScripts(&context, CompilationTarget::Analysis));
            BOOST_TEST(context.hasError());
        }
    }
}

BOOST_AUTO_TEST_CASE(object_construction_reports_factory_failures) {
    Runtime runtime;
    auto descriptor = std::make_unique<ObjectType>("Broken");
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    runtime.registerNativeConstructor(
        type, {}, [](std::span<const RuntimeValue>) -> NomadObject* { return nullptr; }, ""
    );
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad", "return new Broken"}}}, {}});
    BOOST_REQUIRE(compiler->compileScripts(&context, CompilationTarget::Executable));
    RuntimeValue result;
    BOOST_CHECK_EXCEPTION(
        runtime.executeFunction(runtime.getFunctionId("main"), {}, result),
        NomadException,
        [](const NomadException& exception) {
            return NomadString(exception.what()).find("returned null") != NomadString::npos;
        }
    );
    VirtualMachine machine(&runtime);
    InterpreterContext interpreterContext(machine);
    BOOST_CHECK_THROW(
        static_cast<void>(Interpreter{}.evaluateExpression("new Broken", interpreterContext)),
        NomadException
    );
    BOOST_TEST(interpreterContext.getResult() == nullptr);
    BOOST_TEST(!Interpreter{}.execute("new Broken", interpreterContext));
    BOOST_REQUIRE(interpreterContext.getError() != nullptr);
    BOOST_TEST(interpreterContext.getError()->find("returned null") != NomadString::npos);
    BOOST_TEST(interpreterContext.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(compiler_resolves_parameterized_annotations_from_collections) {
    for (const auto target : {CompilationTarget::Analysis, CompilationTarget::Executable}) {
        TestDirectory directory("nomad_host_type_annotations");
        const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
        ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
        auto& runtime = *host.getRuntime();
        BOOST_REQUIRE(runtime.registerParameterizedType("Pair", 2, [](const auto& arguments) {
            return std::make_unique<ObjectType>(
                "Pair<" + arguments[0]->getTypeName() + " " + arguments[1]->getTypeName() + ">",
                arguments
            );
        }));
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{"init", {"init.nomad",
            "params values:List<int> nested:List<List<string>> pair:Pair<string List<int>>\n"
            "event changed items:List<int>\n"
            "fun identity items:List<int>\n"
            "    return items\n"
            "end\n"
            "return identity values"
        }}}, {}});
        BOOST_REQUIRE(compiler->compileScripts(&context, target));
        const auto* function = runtime.getScriptFunction(runtime.getFunctionId("init"));
        BOOST_REQUIRE(function != nullptr);
        BOOST_TEST(function->getParameterType(0) == runtime.getParameterizedType("List", {runtime.getIntegerType()}));
        BOOST_TEST(function->getParameterType(1)->getTypeName() == "List<List<string>>");
        BOOST_TEST(function->getParameterType(2)->getTypeName() == "Pair<string List<int>>");
    }
}

BOOST_AUTO_TEST_CASE(compiler_reports_invalid_parameterized_annotations) {
    const std::vector<std::pair<NomadString, NomadString>> cases = {
        {"params value:List", "Type arguments required"},
        {"params value:List<>", "expects 1 type arguments, got 0"},
        {"params value:List<int string>", "expects 1 type arguments, got 2"},
        {"params value:List<int", "Expected '>'"},
        {"params value:Missing<int>", "Unknown parameterized type"},
        {"params value:List<Missing>", "Unknown type"},
        {"params value:List<void>", "Invalid type argument"},
        {"params value:List<$file>", "Expected '"},
        {"params value:List<int, string>", "Unrecognized token"},
        {"params value:List<List<int>", "Expected '>'"},
        {"event changed value:List<>", "expects 1 type arguments, got 0"},
        {"fun accept value:List<>\nend", "expects 1 type arguments, got 0"},
        {
            "params value:List<int>\n"
            "fun accept item:List<string>\n    return item\nend\n"
            "return accept value",
            "mismatch"
        },
    };
    for (const auto& [source, expected] : cases) {
        BOOST_TEST_CONTEXT(source) {
            TestDirectory directory("nomad_host_bad_type_annotations");
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
            ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
            auto compiler = host.getRuntime()->createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"init", {"init.nomad", source}}}, {}});
            BOOST_TEST(!compiler->compileScripts(&context, CompilationTarget::Analysis));
            bool found = false;
            for (const auto& diagnostic : context.getDiagnostics()) {
                found = found || diagnostic.message.find(expected) != NomadString::npos;
            }
            BOOST_TEST(found, expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(compiler_requires_registered_parameterized_family) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"init", {"init.nomad", "params value:List<int>"}}}, {}});
    BOOST_TEST(!compiler->compileScripts(&context, CompilationTarget::Analysis));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->message.find("Unknown parameterized type 'List'") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(compiler_limits_type_nesting) {
    for (const auto nesting : {63, 64}) {
        TestDirectory directory("nomad_host_type_nesting");
        const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
        ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
        auto compiler = host.getRuntime()->createCompiler();
        CompilerContext context(compiler.get());
        NomadString typeName = "int";
        for (auto index = 0; index < nesting; ++index) {
            typeName = "List<" + typeName + ">";
        }
        compiler->registerSources(&context, {{{"init", {"init.nomad", "params value:" + typeName}}}, {}});
        BOOST_TEST(compiler->compileScripts(&context, CompilationTarget::Analysis) == (nesting == 63));
        if (nesting == 64) {
            BOOST_REQUIRE(context.getFirstError() != nullptr);
            BOOST_TEST(context.getFirstError()->message.find("maximum depth of 64") != NomadString::npos);
        }
    }
}

BOOST_AUTO_TEST_CASE(collections_interns_list_specializations) {
    TestDirectory directory("nomad_host_list_types");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    BOOST_TEST(runtime.getTypeByName("List<int>") == nullptr);
    const auto* integers = runtime.getParameterizedType("List", {runtime.getIntegerType()});
    BOOST_REQUIRE(integers != nullptr);
    BOOST_TEST(integers == runtime.getParameterizedType("List", {runtime.getIntegerType()}));
    BOOST_TEST(integers == runtime.getTypeByName("List<int>"));
    const auto* descriptor = dynamic_cast<const ObjectType*>(integers);
    BOOST_REQUIRE(descriptor != nullptr);
    BOOST_REQUIRE_EQUAL(descriptor->getTypeArguments().size(), 1u);
    BOOST_TEST(descriptor->getTypeArguments()[0] == runtime.getIntegerType());
    const auto* strings = runtime.getParameterizedType("List", {runtime.getStringType()});
    BOOST_REQUIRE(strings != nullptr);
    BOOST_TEST(integers != strings);
    BOOST_TEST(!integers->sameType(strings));
    const auto* nested = runtime.getParameterizedType("List", {integers});
    BOOST_REQUIRE(nested != nullptr);
    BOOST_TEST(nested->getTypeName() == "List<List<int>>");
    BOOST_TEST(runtime.getParameterizedType("List", {}) == nullptr);
    BOOST_TEST(runtime.getParameterizedType("List", {runtime.getVoidType()}) == nullptr);
    BOOST_TEST(runtime.getParameterizedType("List", {nullptr}) == nullptr);
    Runtime other;
    BOOST_TEST(runtime.getParameterizedType("List", {other.getIntegerType()}) == nullptr);
    BOOST_TEST(runtime.getParameterizedType("Missing", {}) == nullptr);
    BOOST_TEST(!runtime.registerParameterizedType("List", 1, [](const auto&) {
        return std::make_unique<ObjectType>("Duplicate");
    }));
}

BOOST_AUTO_TEST_CASE(collections_list_defaults_are_distinct_empty_instances) {
    TestDirectory directory("nomad_host_list_defaults");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    const auto* type = runtime.getParameterizedType("List", {runtime.getIntegerType()});
    BOOST_REQUIRE(type != nullptr);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue first;
    RuntimeValue second;
    type->initValue(first);
    type->initValue(second);
    BOOST_REQUIRE(first.getObjectValue() != nullptr);
    BOOST_REQUIRE(second.getObjectValue() != nullptr);
    BOOST_TEST(first.getObjectValue() != second.getObjectValue());
    type->freeValue(first);
    type->freeValue(second);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collections_native_returns_do_not_allocate_unused_list_defaults) {
    TestDirectory directory("nomad_host_list_native_defaults");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    const auto* type = runtime.getParameterizedType("List", {runtime.getIntegerType()});
    BOOST_REQUIRE(type != nullptr);
    runtime.registerNativeFunction(
        "makeList",
        [&runtime](VirtualMachine* machine) {
            machine->getResult().setObjectValue(new ListObject(runtime.getIntegerType()));
        },
        {}, type, ""
    );
    const auto memberId = runtime.registerNativeMethod(
        static_cast<const ObjectTypeBase*>(type),
        "copyReference",
        [](VirtualMachine* machine) {
            addRef(machine->getThis());
            machine->getResult().setObjectValue(machine->getThis());
        },
        {}, type, ""
    );
    BOOST_REQUIRE(memberId != NOMAD_INVALID_ID);
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("list = makeList", context));
        BOOST_REQUIRE(Interpreter{}.execute("alias = list.copyReference", context));
        BOOST_REQUIRE(Interpreter{}.execute("alias.push 42", context));
        BOOST_REQUIRE(Interpreter{}.execute("list[0]", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 42);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(parameterized_types_validate_factories_and_cache_by_arguments) {
    Runtime runtime;
    NomadIndex calls = 0;
    BOOST_TEST(runtime.registerParameterizedType("Example", 1, [&calls](const auto& arguments) {
        ++calls;
        return std::make_unique<ObjectType>("Example<" + arguments[0]->getTypeName() + ">", arguments);
    }));
    const auto* first = runtime.getParameterizedType("Example", {runtime.getIntegerType()});
    BOOST_REQUIRE(first != nullptr);
    BOOST_TEST(first == runtime.getParameterizedType("Example", {runtime.getIntegerType()}));
    BOOST_TEST(calls == 1u);
    BOOST_TEST(!runtime.registerParameterizedType("Empty", 1, {}));
    BOOST_TEST(!runtime.registerParameterizedType("", 1, {}));
    BOOST_TEST(runtime.registerParameterizedType("Broken", 1, [](const auto&) {
        return std::unique_ptr<Type>{};
    }));
    BOOST_TEST(runtime.getParameterizedType("Broken", {runtime.getIntegerType()}) == nullptr);
    BOOST_TEST(runtime.registerParameterizedType("Collision", 1, [](const auto&) {
        return std::make_unique<ObjectType>("int");
    }));
    BOOST_TEST(runtime.getParameterizedType("Collision", {runtime.getIntegerType()}) == nullptr);
}

BOOST_AUTO_TEST_CASE(collections_module_loads_in_all_modes) {
    TestDirectory directory("nomad_host_collections");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    BOOST_REQUIRE_EQUAL(modules.size(), 1u);
    BOOST_TEST(modules.front().name == "nomad-collections");

    for (const auto mode : {ModuleMode::Application, ModuleMode::Analysis, ModuleMode::Packaging}) {
        ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, mode);
        BOOST_TEST((host.getRuntime() != nullptr) == (mode != ModuleMode::Packaging));
    }
}

BOOST_AUTO_TEST_CASE(loads_registers_compiles_and_shuts_down_in_lifetime_order) {
    TestDirectory directory("nomad_host_lifecycle");
    {
        ModuleHost host({}, directory.getPath(), directory.getPath(), fixtureModules(),
            {"--custom", "value"}, ModuleMode::Application);
        BOOST_REQUIRE(compileFixture(host, CompilationTarget::Executable));
        BOOST_TEST(host.run("init") == 7);
        BOOST_TEST(host.pollExit());
    }
    BOOST_TEST(events(directory) == "register\npreParse\nemit\ncompleted\nexit\nshutdown\ndestroy\n");
}

BOOST_AUTO_TEST_CASE(analysis_skips_emission_and_execution) {
    TestDirectory directory("nomad_host_analysis");
    {
        ModuleHost host({}, directory.getPath(), directory.getPath(), fixtureModules(), {}, ModuleMode::Analysis);
        BOOST_REQUIRE(compileFixture(host, CompilationTarget::Analysis));
    }
    BOOST_TEST(events(directory) == "register\npreParse\ncompleted\nshutdown\ndestroy\n");
}

BOOST_AUTO_TEST_CASE(hook_failure_prevents_later_hooks) {
    TestDirectory directory("nomad_host_hook_failure");
    {
        auto configuration = NomadConfiguration::parse("[fixture]\nfail-hook = true\n");
        ModuleHost host(configuration, directory.getPath(), directory.getPath(), fixtureModules(), {}, ModuleMode::Analysis);
        BOOST_TEST(!compileFixture(host, CompilationTarget::Executable));
    }
    BOOST_TEST(events(directory) == "register\npreParse\nshutdown\ndestroy\n");
}

BOOST_AUTO_TEST_CASE(packaging_uses_context_without_runtime_registration) {
    TestDirectory directory("nomad_host_package");
    auto source = NomadConfiguration::parse("[fixture]\ndevelopment = true\n");
    auto release = source;
    auto generated = NomadString{};
    ModuleHost host(source, directory.getPath(), directory.getPath(), fixtureModules(), {}, ModuleMode::Packaging);
    BOOST_TEST(host.getRuntime() == nullptr);
    ModulePackageContext context{};
    context.source = &source;
    context.release = &release;
    context.writeFile = [&](const NomadPath& path, NomadStringView value) {
        BOOST_TEST(path == NomadPath("generated.txt"));
        generated = value;
    };
    host.package(context);
    BOOST_TEST(source.contains("fixture.development"));
    BOOST_TEST(!release.contains("fixture.development"));
    BOOST_TEST(generated == "generated by fixture\n");
    BOOST_TEST(events(directory).empty());
}

BOOST_AUTO_TEST_CASE(missing_library_is_an_explicit_startup_failure) {
    TestDirectory directory("nomad_host_missing");
    BOOST_CHECK_THROW(
        ModuleHost({}, directory.getPath(), directory.getPath(),
            {{"missing", directory.getPath(), {}}}, {}, ModuleMode::Application),
        ModuleException
    );
}

BOOST_AUTO_TEST_CASE(registration_failure_is_not_reported_as_success) {
    TestDirectory directory("nomad_host_registration_failure");
    auto configuration = NomadConfiguration::parse("[fixture]\nfail-registration = true\n");
    BOOST_CHECK_EXCEPTION(
        ModuleHost(configuration, directory.getPath(), directory.getPath(),
            fixtureModules(), {}, ModuleMode::Analysis),
        NomadException,
        [](const auto& error) {
            return NomadString(error.what()).find("Requested registration failure") != NomadString::npos;
        }
    );
    BOOST_TEST(events(directory) == "register\n");
}

BOOST_AUTO_TEST_CASE(missing_registration_entry_is_an_explicit_startup_failure) {
    TestDirectory directory("nomad_host_missing_entry");
    BOOST_CHECK_EXCEPTION(
        ModuleHost({}, directory.getPath(), directory.getPath(),
            {{"nomad-language", NOMAD_TEST_CORE_DIRECTORY, {}}}, {}, ModuleMode::Analysis),
        ModuleException,
        [](const auto& error) {
            return NomadString(error.what()).find("does not export registerModule") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(module_arguments_are_provided_during_registration) {
    TestDirectory directory("nomad_host_arguments");
    ModuleHost host({}, directory.getPath(), directory.getPath(), fixtureModules(),
        {"--custom", "value"}, ModuleMode::Analysis);
    RuntimeValue value;
    host.getRuntime()->getConstantValue(host.getRuntime()->getConstantId("fixture.argumentCount"), value);
    BOOST_TEST(value.getIntegerValue() == 2);
}

BOOST_AUTO_TEST_SUITE_END()
