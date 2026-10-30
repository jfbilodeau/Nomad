// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/generated/log/Bindings.hpp>

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>

#include <nomad/modules/ScriptBindings.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <BindingTestFixture.hpp>

#include <boost/test/unit_test.hpp>

#include <limits>
#include <type_traits>
#include <utility>

using namespace nomad;
using namespace nomad::test;
using namespace nomad::generated::log;

BOOST_AUTO_TEST_SUITE(script_bindings)

static_assert(std::is_base_of_v<ScriptBindingObject, log_SinkConfiguration>);
static_assert(std::is_base_of_v<ScriptBindingObject, log_FileSinkConfiguration>);
static_assert(std::is_enum_v<log_Level>);
static_assert(std::is_enum_v<log_FileRotationMode>);
static_assert(std::is_enum_v<log_DeliveryMode>);
static_assert(log_LevelToInteger(log_Level::Info) == 2);
static_assert(log_FileRotationModeToInteger(log_FileRotationMode::Size) == 2);
static_assert(log_DeliveryModeToInteger(log_DeliveryMode::Buffered) == 1);
static_assert(log_LevelToInteger(log_LevelFromInteger(std::numeric_limits<NomadInteger>::min())) ==
    std::numeric_limits<NomadInteger>::min());
static_assert(log_FileRotationModeToInteger(
    log_FileRotationModeFromInteger(std::numeric_limits<NomadInteger>::max())) ==
    std::numeric_limits<NomadInteger>::max());
static_assert(log_DeliveryModeToInteger(log_DeliveryModeFromInteger(-1)) == -1);
static_assert(noexcept(log_LevelToInteger(log_Level::Info)));
static_assert(noexcept(log_LevelFromInteger(0)));

BOOST_AUTO_TEST_CASE(type_references_are_available_without_a_runtime_and_own_their_names) {
    const auto sinkReference = log_SinkConfiguration::typeReference();
    const auto fileReference = log_FileSinkConfiguration::typeReference();
    const auto levelReference = log_LevelBinding::typeReference();
    const auto rotationReference = log_FileRotationModeBinding::typeReference();
    const auto deliveryReference = log_DeliveryModeBinding::typeReference();
    BOOST_TEST(sinkReference.name == "log.SinkConfiguration");
    BOOST_TEST(fileReference.name == "log.FileSinkConfiguration");
    BOOST_TEST(levelReference.name == "log.Level");
    BOOST_TEST(rotationReference.name == "log.FileRotationMode");
    BOOST_TEST(deliveryReference.name == "log.DeliveryMode");
    auto copy = fileReference;
    BOOST_CHECK(copy == fileReference);
    copy.name = "different.Type";
    BOOST_TEST(fileReference.name == "log.FileSinkConfiguration");
    BOOST_TEST(log_FileSinkConfiguration::typeReference().name == fileReference.name);
    BOOST_CHECK(copy != fileReference);
}

BOOST_AUTO_TEST_CASE(generated_deferred_types_are_compiler_local_reusable_and_cover_all_binding_types) {
    Runtime first;
    Runtime second;
    auto firstCompiler = first.createCompiler();
    auto secondCompiler = second.createCompiler();
    const auto types = deferTypes(*firstCompiler);
    const auto repeated = deferTypes(*firstCompiler);
    const auto other = deferTypes(*secondCompiler);
    const std::pair<const Type*, NomadString> declarations[]{
        {types.deliveryMode, "log.DeliveryMode"},
        {types.fileRotationMode, "log.FileRotationMode"},
        {types.level, "log.Level"},
        {types.fileSinkConfiguration, "log.FileSinkConfiguration"},
        {types.sinkConfiguration, "log.SinkConfiguration"}
    };
    for (const auto& [type, name] : declarations) {
        BOOST_REQUIRE(type != nullptr);
        BOOST_TEST(type->getTypeName() == name);
        BOOST_TEST(type->getRuntime() == &first);
        BOOST_TEST(type->getId().isInvalid());
        BOOST_TEST(first.getTypeByName(name) == nullptr);
        BOOST_TEST(type == firstCompiler->deferType({name}));
    }
    BOOST_TEST(types.deliveryMode == repeated.deliveryMode);
    BOOST_TEST(types.fileRotationMode == repeated.fileRotationMode);
    BOOST_TEST(types.level == repeated.level);
    BOOST_TEST(types.fileSinkConfiguration == repeated.fileSinkConfiguration);
    BOOST_TEST(types.sinkConfiguration == repeated.sinkConfiguration);
    BOOST_TEST(types.level != other.level);
    BOOST_TEST(types.sinkConfiguration != other.sinkConfiguration);
    BOOST_TEST(other.level->getRuntime() == &second);
    const auto result = compileSources({{{"log.configuration", {"configuration.nomad", getLogBindingSource()}}}, {}},
        *firstCompiler, CompilationTarget::Analysis);
    BOOST_REQUIRE(result.succeeded());
}

BOOST_AUTO_TEST_CASE(actual_generated_log_cpp_constructs_and_reads_and_writes_stored_fields) {
    Runtime runtime;
    ScriptBindings bindings(runtime);
    compileLogBindingSource(runtime);
    VirtualMachine machine(&runtime);
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto configuration = log_FileSinkConfiguration::create(bindings, machine);
        BOOST_TEST(configuration.getActive());
        BOOST_TEST(configuration.getDirectory() == "logs");
        BOOST_TEST(configuration.getName() == "application");
        BOOST_CHECK(configuration.getLevel() == log_Level::Info);
        BOOST_CHECK(configuration.getRotationMode() == log_FileRotationMode::Size);
        BOOST_TEST(configuration.getRotateAtStartup());
        BOOST_TEST(configuration.getMaxFiles() == 10);
        BOOST_TEST(configuration.getMaxSizeBytes() == 10485760);
        configuration.setActive(false);
        configuration.setDirectory("other");
        configuration.setName(NomadString("embedded\0name", 13));
        configuration.setLevel(log_Level::Warning);
        configuration.setRotationMode(log_FileRotationMode::None);
        configuration.setRotateAtStartup(false);
        configuration.setMaxFiles(4);
        configuration.setMaxSizeBytes(120);
        BOOST_TEST(!configuration.getActive());
        BOOST_TEST(configuration.getDirectory() == "other");
        BOOST_TEST(configuration.getName() == NomadString("embedded\0name", 13));
        BOOST_CHECK(configuration.getLevel() == log_Level::Warning);
        BOOST_CHECK(configuration.getRotationMode() == log_FileRotationMode::None);
        BOOST_TEST(!configuration.getRotateAtStartup());
        BOOST_TEST(configuration.getMaxFiles() == 4);
        BOOST_TEST(configuration.getMaxSizeBytes() == 120);
        auto* object = dynamic_cast<ScriptObject*>(configuration.getObject());
        BOOST_REQUIRE(object != nullptr);
        const auto* field = object->getClassType()->getProperty("maxFiles");
        BOOST_TEST(object->getField(object->getClassType()->getFieldIndex(field->getId())).getIntegerValue() == 4);
        object->setField(object->getClassType()->getFieldIndex(field->getId()), RuntimeValue(NomadInteger{7}));
        BOOST_TEST(configuration.getMaxFiles() == 7);
        auto sink = log_SinkConfiguration::create(bindings, machine);
        BOOST_TEST(sink.getActive());
        BOOST_CHECK(sink.getLevel() == log_Level::Info);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(handles_retain_receivers_copy_and_move_without_transferring_runtime_ownership) {
    Runtime runtime;
    ScriptBindings bindings(runtime);
    compileLogBindingSource(runtime);
    VirtualMachine machine(&runtime);
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto first = log_SinkConfiguration::create(bindings, machine);
        BOOST_TEST(first.getObject()->getReferenceCount() == 1U);
        {
            auto copy = first;
            BOOST_TEST(first.getObject()->getReferenceCount() == 2U);
            copy.setActive(false);
            BOOST_TEST(!first.getActive());
            auto moved = std::move(copy);
            BOOST_CHECK_THROW((void)copy.getActive(), NomadException);
            BOOST_TEST(moved.getObject()->getReferenceCount() == 2U);
            auto wrapped = log_SinkConfiguration(bindings, first.getObject());
            BOOST_TEST(wrapped.getObject()->getReferenceCount() == 3U);
            wrapped = first;
            moved = std::move(wrapped);
            BOOST_TEST(moved.getObject()->getReferenceCount() == 2U);
        }
        BOOST_TEST(first.getObject()->getReferenceCount() == 1U);
        BOOST_TEST(&first.getRuntime() == &runtime);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(runtime_local_ids_and_wrong_runtime_receiver_and_machine_are_checked) {
    Runtime first;
    Runtime second;
    ScriptBindings firstBindings(first);
    ScriptBindings secondBindings(second);
    BOOST_REQUIRE(second.createEnum("Padding", {{"Value", 1}}).isValid());
    auto secondCompiler = second.createCompiler();
    BOOST_REQUIRE(compileSources({{{"padding", {"padding.nomad",
        "class PaddingClass\nvalue = 1\nend\n"}}}, {}}, *secondCompiler).succeeded());
    compileLogBindingSource(first);
    auto reordered = getLogBindingSource();
    const auto original = NomadString("        active = true\n        level = log.Level.Info");
    const auto position = reordered.find(original);
    BOOST_REQUIRE(position != NomadString::npos);
    reordered.replace(position, original.size(), "        level = log.Level.Info\n        active = true");
    compileLogBindingSource(second, reordered);
    BOOST_CHECK(first.getTypeByName("log.SinkConfiguration")->getId() !=
        second.getTypeByName("log.SinkConfiguration")->getId());
    VirtualMachine firstMachine(&first);
    VirtualMachine secondMachine(&second);
    auto firstObject = log_SinkConfiguration::create(firstBindings, firstMachine);
    auto secondObject = log_SinkConfiguration::create(secondBindings, secondMachine);
    BOOST_TEST(&firstObject.getBinding() == &log_SinkConfiguration::bind(firstBindings));
    BOOST_TEST(&secondObject.getBinding() == &log_SinkConfiguration::bind(secondBindings));
    BOOST_TEST(&firstObject.getBinding() != &secondObject.getBinding());
    BOOST_CHECK(firstObject.getBinding().getPropertyId(0) != secondObject.getBinding().getPropertyId(0));
    BOOST_TEST(firstObject.getBinding().getFieldIndex(0, BindingValueKind::Boolean) !=
        secondObject.getBinding().getFieldIndex(0, BindingValueKind::Boolean));
    BOOST_CHECK(firstObject.getLevel() == secondObject.getLevel());
    secondObject.setActive(false);
    secondObject.setLevel(log_Level::Critical);
    BOOST_TEST(firstObject.getActive());
    BOOST_TEST(!secondObject.getActive());
    BOOST_CHECK(secondObject.getLevel() == log_Level::Critical);
    BOOST_CHECK_THROW(log_SinkConfiguration(secondBindings, firstObject.getObject()), NomadException);
    BOOST_CHECK_THROW((void)log_SinkConfiguration::create(firstBindings, secondMachine), NomadException);
    BOOST_CHECK_THROW(log_FileSinkConfiguration(firstBindings, firstObject.getObject()), NomadException);
    BOOST_CHECK_THROW(log_SinkConfiguration(firstBindings, nullptr), NomadException);
    auto* text = NomadStringObject::create("wrong receiver");
    BOOST_CHECK_THROW(log_SinkConfiguration(firstBindings, text), NomadException);
    text->releaseRef();
}

BOOST_AUTO_TEST_CASE(contract_rejects_missing_classes_wrong_field_types_and_enum_mappings) {
    Runtime empty;
    ScriptBindings emptyBindings(empty);
    VirtualMachine emptyMachine(&empty);
    BOOST_CHECK_THROW((void)log_SinkConfiguration::create(emptyBindings, emptyMachine), NomadException);
    {
        Runtime runtime;
        ScriptBindings bindings(runtime);
        auto source = getLogBindingSource();
        const auto position = source.find("active = true");
        BOOST_REQUIRE(position != NomadString::npos);
        source.replace(position, NomadString("active = true").size(), "active = 1");
        compileLogBindingSource(runtime, source);
        VirtualMachine machine(&runtime);
        BOOST_CHECK_THROW((void)log_SinkConfiguration::create(bindings, machine), NomadException);
    }
    {
        Runtime runtime;
        ScriptBindings bindings(runtime);
        auto source = getLogBindingSource();
        const auto position = source.find("        Debug\n        Info");
        BOOST_REQUIRE(position != NomadString::npos);
        source.replace(position, NomadString("        Debug\n        Info").size(), "        Info\n        Debug");
        compileLogBindingSource(runtime, source);
        VirtualMachine machine(&runtime);
        BOOST_CHECK_THROW((void)log_SinkConfiguration::create(bindings, machine), NomadException);
    }
    {
        Runtime runtime;
        ScriptBindings bindings(runtime);
        auto source = getLogBindingSource();
        const auto declaration = NomadString("    class SinkConfiguration\n");
        const auto position = source.find(declaration);
        BOOST_REQUIRE(position != NomadString::npos);
        source.insert(position + declaration.size(), "        fun bindingMethod\nreturn 1\nend\n");
        compileLogBindingSource(runtime, source);
        const auto* type = runtime.getTypeByName("log.SinkConfiguration")->asClassType();
        BOOST_TEST(!isScriptBindingClass(*type));
        VirtualMachine machine(&runtime);
        BOOST_CHECK_THROW((void)log_SinkConfiguration::create(bindings, machine), NomadException);
    }
}

BOOST_AUTO_TEST_CASE(enum_accessors_round_trip_named_and_unnamed_values_without_validation) {
    Runtime runtime;
    ScriptBindings bindings(runtime);
    compileLogBindingSource(runtime);
    VirtualMachine machine(&runtime);
    auto configuration = log_SinkConfiguration::create(bindings, machine);
    auto fileConfiguration = log_FileSinkConfiguration::create(bindings, machine);
    for (const auto value : {
        std::numeric_limits<NomadInteger>::min(), NomadInteger{-1}, NomadInteger{0},
        NomadInteger{2}, NomadInteger{999}, std::numeric_limits<NomadInteger>::max()
    }) {
        BOOST_TEST(log_LevelToInteger(log_LevelFromInteger(value)) == value);
        BOOST_TEST(log_FileRotationModeToInteger(log_FileRotationModeFromInteger(value)) == value);
        BOOST_TEST(log_DeliveryModeToInteger(log_DeliveryModeFromInteger(value)) == value);
        configuration.setLevel(value);
        BOOST_TEST(configuration.getLevelAsInteger() == value);
        BOOST_CHECK(configuration.getLevel() == log_LevelFromInteger(value));
        configuration.setLevel(log_LevelFromInteger(value));
        BOOST_TEST(configuration.getLevelAsInteger() == value);
        fileConfiguration.setLevel(value);
        BOOST_TEST(fileConfiguration.getLevelAsInteger() == value);
        BOOST_CHECK(fileConfiguration.getLevel() == log_LevelFromInteger(value));
        fileConfiguration.setLevel(log_LevelFromInteger(value));
        BOOST_TEST(fileConfiguration.getLevelAsInteger() == value);
        fileConfiguration.setRotationMode(value);
        BOOST_TEST(fileConfiguration.getRotationModeAsInteger() == value);
        BOOST_CHECK(fileConfiguration.getRotationMode() == log_FileRotationModeFromInteger(value));
        fileConfiguration.setRotationMode(log_FileRotationModeFromInteger(value));
        BOOST_TEST(fileConfiguration.getRotationModeAsInteger() == value);
    }
    auto* object = dynamic_cast<ScriptObject*>(configuration.getObject());
    const auto* field = object->getClassType()->getProperty("level");
    object->setField(object->getClassType()->getFieldIndex(field->getId()), RuntimeValue(NomadInteger{999}));
    BOOST_TEST(configuration.getLevelAsInteger() == 999);
    BOOST_CHECK(configuration.getLevel() == log_LevelFromInteger(999));
}

BOOST_AUTO_TEST_CASE(normal_class_initializers_run_once_per_construction_not_during_compilation_or_wrapping) {
    Runtime runtime;
    ScriptBindings bindings(runtime);
    auto calls = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction("bindingDefault", [&calls](VirtualMachine* machine) {
        ++calls;
        machine->setBooleanResult(true);
    }, {}, runtime.getBooleanType(), "").isValid());
    auto source = getLogBindingSource();
    const auto position = source.find("active = true");
    BOOST_REQUIRE(position != NomadString::npos);
    source.replace(position, NomadString("active = true").size(), "active = bindingDefault");
    compileLogBindingSource(runtime, source);
    BOOST_TEST(calls == 0);
    VirtualMachine machine(&runtime);
    auto first = log_SinkConfiguration::create(bindings, machine);
    BOOST_TEST(calls == 1);
    auto wrapped = log_SinkConfiguration(bindings, first.getObject());
    auto copied = wrapped;
    BOOST_TEST(copied.getActive());
    BOOST_TEST(calls == 1);
    auto second = log_SinkConfiguration::create(bindings, machine);
    BOOST_TEST(second.getActive());
    BOOST_TEST(calls == 2);
}

BOOST_AUTO_TEST_CASE(reusable_support_checks_access_types_and_float_storage) {
    Runtime runtime;
    ScriptBindings bindings(runtime);
    auto compiler = runtime.createCompiler();
    const auto result = compileSources({{{"settings", {"settings.nomad",
        "class Settings\nratio = 1.5\nname = \"hello\"\nend\n"}}}, {}}, *compiler);
    BOOST_REQUIRE(result.succeeded());
    VirtualMachine machine(&runtime);
    const BindingClass contract{"Settings", {{"ratio", "float"}, {"name", "string"}}, {}};
    auto settings = ScriptBindingObject::construct(bindings, machine, contract);
    BOOST_TEST(settings.getFloat(0) == 1.5F);
    settings.setFloat(0, 2.5F);
    BOOST_TEST(settings.getFloat(0) == 2.5F);
    BOOST_CHECK_THROW((void)settings.getInteger(0), NomadException);
    BOOST_CHECK_THROW(settings.setBoolean(1, true), NomadException);
    BOOST_CHECK_THROW(settings.setFloat(2, 3.0F), NomadException);
}

BOOST_AUTO_TEST_CASE(resolved_bindings_are_shared_and_keep_stable_slots_without_freezing_classes) {
    Runtime runtime;
    ScriptBindings bindings(runtime);
    compileLogBindingSource(runtime);
    VirtualMachine machine(&runtime);
    const auto& binding = log_SinkConfiguration::bind(bindings);
    BOOST_TEST(&log_SinkConfiguration::bind(bindings) == &binding);
    const auto activeId = binding.getPropertyId(0);
    const auto activeIndex = binding.getFieldIndex(0, BindingValueKind::Boolean);
    auto first = log_SinkConfiguration::create(bindings, machine);
    auto second = log_SinkConfiguration::create(bindings, machine);
    auto wrapped = log_SinkConfiguration(bindings, first.getObject());
    BOOST_TEST(&first.getBinding() == &binding);
    BOOST_TEST(&second.getBinding() == &binding);
    BOOST_TEST(&wrapped.getBinding() == &binding);

    BOOST_REQUIRE(runtime.registerProperty(
        binding.getClassType()->getId(), "extension", runtime.getIntegerType()
    ).isValid());
    BOOST_TEST(binding.getPropertyId(0) == activeId);
    BOOST_TEST(binding.getFieldIndex(0, BindingValueKind::Boolean) == activeIndex);
    for (auto index = 0; index < 10; ++index) {
        first.setActive(index % 2 == 0);
        BOOST_TEST(wrapped.getActive() == (index % 2 == 0));
        BOOST_CHECK(second.getLevel() == log_Level::Info);
    }
    auto wrappedAfterExtension = log_SinkConfiguration(bindings, first.getObject());
    BOOST_TEST(&wrappedAfterExtension.getBinding() == &binding);
    BOOST_TEST(wrappedAfterExtension.getActive() == first.getActive());
}

BOOST_AUTO_TEST_CASE(helper_owns_contract_snapshots_and_failed_bindings_do_not_poison_the_cache) {
    Runtime runtime;
    ScriptBindings bindings(runtime);
    BOOST_TEST(&bindings.getRuntime() == &runtime);
    auto compiler = runtime.createCompiler();
    BOOST_REQUIRE(compileSources({{{"settings", {"settings.nomad",
        "class Settings\ncount = 3\nend\n"}}}, {}}, *compiler).succeeded());
    const auto& binding = bindings.getClassBinding(BindingClass{"Settings", {{"count", "int"}}, {}});
    const BindingClass sameContract{"Settings", {{"count", "int"}}, {}};
    BOOST_TEST(&bindings.getClassBinding(sameContract) == &binding);
    BOOST_CHECK_THROW((void)bindings.getClassBinding(
        BindingClass{"Settings", {{"count", "bool"}}, {}}
    ), NomadException);
    BOOST_TEST(&bindings.getClassBinding(sameContract) == &binding);
    VirtualMachine machine(&runtime);
    auto settings = ScriptBindingObject::construct(binding, machine);
    BOOST_TEST(settings.getInteger(0) == 3);
    settings.setInteger(0, 7);
    BOOST_TEST(settings.getInteger(0) == 7);
}

BOOST_AUTO_TEST_CASE(helpers_keep_independent_caches_without_owning_the_runtime) {
    Runtime runtime;
    ScriptBindings bindings(runtime);
    compileLogBindingSource(runtime);
    VirtualMachine machine(&runtime);
    const auto& binding = log_SinkConfiguration::bind(bindings);
    auto settings = log_SinkConfiguration::create(bindings, machine);
    {
        ScriptBindings otherBindings(runtime);
        const auto& otherBinding = log_SinkConfiguration::bind(otherBindings);
        BOOST_TEST(&otherBinding != &binding);
        BOOST_TEST(&otherBinding.getRuntime() == &runtime);
        auto otherSettings = log_SinkConfiguration::create(otherBindings, machine);
        otherSettings.setActive(false);
        BOOST_TEST(settings.getActive());
        BOOST_TEST(!otherSettings.getActive());
        auto fileSettings = log_FileSinkConfiguration::create(otherBindings, machine);
        BOOST_TEST(fileSettings.getDirectory() == "logs");
        BOOST_TEST(&log_SinkConfiguration::bind(otherBindings) == &otherBinding);
        BOOST_TEST(!otherSettings.getActive());
    }
    BOOST_TEST(&log_SinkConfiguration::bind(bindings) == &binding);
    settings.setActive(false);
    BOOST_TEST(!settings.getActive());
    {
        ScriptBindings replacementBindings(runtime);
        auto wrapped = log_SinkConfiguration(replacementBindings, settings.getObject());
        wrapped.setActive(true);
    }
    BOOST_TEST(settings.getActive());
}

BOOST_AUTO_TEST_SUITE_END()
