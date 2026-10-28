#include <nomad/collections/ListObject.hpp>
#include <nomad/collections/RegisterArrayType.hpp>
#include <nomad/collections/RegisterKeyValueType.hpp>
#include <nomad/collections/RegisterListType.hpp>
#include <nomad/collections/RegisterMapType.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;

namespace {

void registerCollections(Runtime& runtime) {
    registerArrayType(&runtime);
    registerKeyValueType(&runtime);
    registerListType(&runtime);
    registerMapType(&runtime);
}

bool compile(Runtime& runtime, const NomadString& source) {
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad", source}}}, {}});
    const auto compiled = compiler->compileFunctions(&context);
    if (!compiled && context.getFirstError() != nullptr) {
        BOOST_TEST_MESSAGE(formatDiagnostic(*context.getFirstError()));
    }
    return compiled;
}

void execute(Runtime& runtime, const NomadString& source) {
    BOOST_REQUIRE(compile(runtime, source));
    runtime.executeFunction(runtime.getFunctionId("main"));
}

void reject(const NomadString& setup, const NomadString& expression) {
    BOOST_TEST_CONTEXT(expression) {
        Runtime runtime;
        registerCollections(runtime);
        BOOST_TEST(!compile(runtime, setup + "\n" + expression));
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        if (!setup.empty()) {
            BOOST_REQUIRE(Interpreter{}.execute(setup, context));
        }
        BOOST_TEST(!Interpreter{}.execute(expression, context));
        BOOST_TEST(context.hasError());
    }
}

} // namespace

BOOST_AUTO_TEST_SUITE(read_only_collections)

BOOST_AUTO_TEST_CASE(collection_iterable_factories_belong_to_their_specialized_types) {
    Runtime runtime;
    registerCollections(runtime);
    const auto* integerType = runtime.getIntegerType();
    for (const auto* name : {"Array1", "ReadOnlyArray1", "List", "ReadOnlyList"}) {
        BOOST_TEST_CONTEXT(name) {
            const auto* type = runtime.getParameterizedType(name, {integerType});
            BOOST_REQUIRE(type != nullptr);
            const auto* classType = type->asClassType();
            BOOST_REQUIRE(classType != nullptr);
            const auto owned = classType->getIterableInfo();
            const auto forwarded = runtime.getIterableInfo(type);
            BOOST_REQUIRE(owned.has_value());
            BOOST_REQUIRE(forwarded.has_value());
            BOOST_TEST(owned->valueType == integerType);
            BOOST_TEST(owned->iteratorType == runtime.getIteratorType(integerType));
            BOOST_TEST(forwarded->factoryId == owned->factoryId);
            const auto* factory = runtime.getMethod(owned->factoryId);
            BOOST_REQUIRE(factory != nullptr);
            BOOST_TEST(factory->getClassType() == classType);
            BOOST_TEST(factory->getName() == "iterator");
        }
    }
}

BOOST_AUTO_TEST_CASE(list_snapshots_copy_containers_share_nested_values_and_release_ownership) {
    // Include the process-wide empty string in the lifetime baseline.
    NomadStringObject::createEmpty()->releaseRef();
    const auto before = NomadObject::getLiveObjectCount();
    {
        Runtime runtime;
        registerCollections(runtime);
        execute(runtime,
            "items = new List<string>\n"
            "items.append \"first\"\nitems.append \"second\"\n"
            "snapshot = items.toReadOnly\n"
            "items[0] = \"changed\"\nitems.clear\n"
            "assert snapshot.count == 2\nassert snapshot[0] == \"first\"\n"
            "length = 0\nfor value in snapshot\nlength = length + value.length\nend\n"
            "assert length == 11\n"
            "nested = new List<int>\nnested.append 10\n"
            "outer = new List<List<int>>\nouter.append nested\n"
            "shallow = outer.toReadOnly\nouter.clear\nnested.append 20\n"
            "assert shallow[0].count == 2\n"
            "defaults = new Array<ReadOnlyList<int>> 1\nempty = defaults[0]\nassert empty.count == 0\n"
            "emptyCopy = (new List<int>).toReadOnly\nassert emptyCopy.count == 0\n"
            "for emptyValue in empty\nassert false\nend\n"
            "fun count values:ReadOnlyList<string>\nreturn values.count\nend\n"
            "assert (count snapshot) == 2\n");
        const auto* type = runtime.getParameterizedType("ReadOnlyList", {runtime.getIntegerType()});
        BOOST_REQUIRE(type != nullptr);
        BOOST_TEST(runtime.getConstructors(type).empty());
        const auto* mutableType = runtime.getParameterizedType("List", {runtime.getIntegerType()});
        BOOST_TEST(!type->acceptsArgument(mutableType));
        BOOST_TEST(!mutableType->acceptsArgument(type));
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(list_mutators_and_constructors_are_unavailable_in_both_execution_modes) {
    for (const auto* expression : {
        "snapshot[0] = 20", "snapshot.append 20", "snapshot.prepend 20", "snapshot.insert 0 20",
        "snapshot.remove 0", "snapshot.removeFirst", "snapshot.removeLast", "snapshot.clear",
        "snapshot.count = 0"
    }) {
        reject("items = new List<int>\nitems.append 10\nsnapshot = items.toReadOnly", expression);
    }
    reject("", "snapshot = new ReadOnlyList<int>");
}

BOOST_AUTO_TEST_CASE(native_lists_can_be_exposed_directly_as_read_only) {
    Runtime runtime;
    registerCollections(runtime);
    const auto* type = runtime.getParameterizedType("ReadOnlyList", {runtime.getIntegerType()});
    auto list = std::make_unique<ListObject>(runtime.getIntegerType());
    (void)list->append(RuntimeValue(NomadInteger{42}));
    RuntimeValue value;
    value.setObjectValue(list.get());
    BOOST_REQUIRE(runtime.registerConstant("snapshot", value, type).isValid());
    list.release()->releaseRef();
    execute(runtime, "assert snapshot.count == 1\nassert snapshot[0] == 42");
}

BOOST_AUTO_TEST_CASE(map_snapshots_preserve_readers_and_shallow_copy_entries) {
    // Include the process-wide empty string in the lifetime baseline.
    NomadStringObject::createEmpty()->releaseRef();
    const auto before = NomadObject::getLiveObjectCount();
    {
        Runtime runtime;
        registerCollections(runtime);
        execute(runtime,
            "items = new Map<string string>\nitems[\"key\"] = \"value\"\n"
            "snapshot = items.toReadOnly\nitems[\"key\"] = \"changed\"\nitems.clear\n"
            "assert snapshot.count == 1\nassert snapshot.contains \"key\"\n"
            "assert snapshot[\"key\"] == \"value\"\n"
            "keys = snapshot.keys\nvalues = snapshot.values\nentries = snapshot.entries\n"
            "assert keys[0] == \"key\"\nassert values[0] == \"value\"\n"
            "assert entries[0].key == \"key\"\nassert entries[0].value == \"value\"\n"
            "keys[0] = \"changed\"\nvalues[0] = \"changed\"\n"
            "assert snapshot[\"key\"] == \"value\"\n"
            "nested = new List<int>\nnested.append 10\n"
            "outer = new Map<string List<int>>\nouter[\"key\"] = nested\n"
            "shallow = outer.toReadOnly\nouter.clear\nnested.append 20\n"
            "assert shallow[\"key\"].count == 2\n"
            "defaults = new Array<ReadOnlyMap<string int>> 1\nempty = defaults[0]\n"
            "assert empty.count == 0\nassert empty[\"missing\"] == 0\nassert empty.count == 0\n"
            "emptyCopy = (new Map<string int>).toReadOnly\nassert emptyCopy.count == 0\n");
        const auto* type = runtime.getParameterizedType("ReadOnlyMap", {runtime.getStringType(), runtime.getIntegerType()});
        BOOST_REQUIRE(type != nullptr);
        BOOST_TEST(runtime.getConstructors(type).empty());
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(map_mutators_and_constructors_are_unavailable_in_both_execution_modes) {
    for (const auto* expression : {"snapshot[\"key\"] = 20", "snapshot.remove \"key\"", "snapshot.clear"}) {
        reject("items = new Map<string int>\nitems[\"key\"] = 10\nsnapshot = items.toReadOnly", expression);
    }
    reject("", "snapshot = new ReadOnlyMap<string int>");
}

BOOST_AUTO_TEST_CASE(array_snapshots_preserve_rank_dimensions_and_shallow_values) {
    // Include the process-wide empty string in the lifetime baseline.
    NomadStringObject::createEmpty()->releaseRef();
    const auto before = NomadObject::getLiveObjectCount();
    {
        Runtime runtime;
        registerCollections(runtime);
        execute(runtime,
            "line = new Array<string> 2\nline[0] = \"first\"\nline[1] = \"second\"\n"
            "lineCopy = line.toReadOnly\nline[0] = \"changed\"\n"
            "assert lineCopy.count == 2\nassert lineCopy[0] == \"first\"\n"
            "length = 0\nfor text in lineCopy\nlength = length + text.length\nend\nassert length == 11\n"
            "grid = new Array<int> 2 3\ngrid[1 2] = 42\ngridCopy = grid.toReadOnly\ngrid[1 2] = 7\n"
            "assert gridCopy.count == 6\nassert gridCopy[1 2] == 42\nassert gridCopy[0 2] == 0\n"
            "volume = new Array<int> 2 3 4\nvolume[1 2 3] = 99\n"
            "volumeCopy = volume.toReadOnly\nvolume[1 2 3] = 7\n"
            "assert volumeCopy.count == 24\nassert volumeCopy[1 2 3] == 99\n"
            "nested = new List<int>\nnested.append 10\n"
            "outer = new Array<List<int>> 1\nouter[0] = nested\nshallow = outer.toReadOnly\n"
            "outer[0] = new List<int>\nnested.append 20\nassert shallow[0].count == 2\n"
            "lineDefaults = new Array<Array1<int>> 1\nemptyLineCopy = lineDefaults[0].toReadOnly\n"
            "gridDefaults = new Array<Array2<int>> 1\nemptyGridCopy = gridDefaults[0].toReadOnly\n"
            "volumeDefaults = new Array<Array3<int>> 1\nemptyVolumeCopy = volumeDefaults[0].toReadOnly\n"
            "assert emptyLineCopy.count == 0\nassert emptyGridCopy.count == 0\nassert emptyVolumeCopy.count == 0\n"
            "assert emptyLineCopy[0] == 0\nassert emptyGridCopy[0 0] == 0\nassert emptyVolumeCopy[0 0 0] == 0\n"
            "defaults = new Array<ReadOnlyArray3<int>> 1\nassert defaults[0].count == 0\n");
        for (const auto* family : {"ReadOnlyArray1", "ReadOnlyArray2", "ReadOnlyArray3"}) {
            const auto* type = runtime.getParameterizedType(family, {runtime.getIntegerType()});
            BOOST_REQUIRE(type != nullptr);
            BOOST_TEST(runtime.getConstructors(type).empty());
        }
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(array_writes_and_constructors_are_unavailable_in_both_execution_modes) {
    reject("snapshot = (new Array<int> 2).toReadOnly", "snapshot[0] = 20");
    reject("snapshot = (new Array<int> 2 3).toReadOnly", "snapshot[0 0] = 20");
    reject("snapshot = (new Array<int> 2 3 4).toReadOnly", "snapshot[0 0 0] = 20");
    reject("", "snapshot = new ReadOnlyArray1<int> 2");
    reject("", "snapshot = new ReadOnlyArray2<int> 2 3");
    reject("", "snapshot = new ReadOnlyArray3<int> 2 3 4");
}

BOOST_AUTO_TEST_SUITE_END()