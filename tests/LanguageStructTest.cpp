// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/RegisterArrayType.hpp>
#include <nomad/collections/RegisterKeyValueType.hpp>
#include <nomad/collections/RegisterListType.hpp>
#include <nomad/collections/RegisterMapType.hpp>

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/Source.hpp>

#include <nomad/script/Hub.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/StructType.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(language_structs)

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
