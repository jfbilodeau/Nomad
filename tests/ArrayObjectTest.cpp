// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1Type.hpp>
#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/Array2Type.hpp>
#include <nomad/collections/Array2dObject.hpp>
#include <nomad/collections/Array3Type.hpp>
#include <nomad/collections/Array3dObject.hpp>
#include <nomad/collections/ArrayType.hpp>
#include <nomad/collections/ListObject.hpp>
#include <nomad/collections/ListType.hpp>

#include <nomad/log/MemorySink.hpp>

#include <nomad/script/Type.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <limits>
#include <memory>
#include <type_traits>

using namespace nomad;

namespace {

class ArrayValue final : public RuntimeValue {
public:
    template<typename Array, typename... Indices>
    ArrayValue(const Array& array, Indices... indices):
        m_type(array.getElementType())
    {
        try {
            array.getValue(indices..., *this);
        } catch (...) {
            if (m_type->needsFree()) {
                m_type->freeValue(*this);
            }
            throw;
        }
    }

    ~ArrayValue() {
        if (m_type->needsFree()) {
            m_type->freeValue(*this);
        }
    }

    ArrayValue(const ArrayValue&) = delete;
    ArrayValue& operator=(const ArrayValue&) = delete;

private:
    const Type* m_type;
};

class TrackedArrayElement final : public NomadObject {
public:
    explicit TrackedArrayElement(NomadIndex& destroyed);
    ~TrackedArrayElement() override;

private:
    NomadIndex& m_destroyed;
};

TrackedArrayElement::TrackedArrayElement(NomadIndex& destroyed):
    m_destroyed(destroyed)
{}

TrackedArrayElement::~TrackedArrayElement() {
    ++m_destroyed;
}

class FailingElementType final : public ObjectTypeBase {
public:
    [[nodiscard]] NomadString getTypeName() const override;
    void toString(const RuntimeValue& value, NomadString& text) const override;
    void initValue(RuntimeValue& value) const override;
    void freeValue(RuntimeValue& value) const override;
    void copyValue(const RuntimeValue& source, RuntimeValue& destination) const override;

    mutable NomadIndex initialized = 0;
    mutable NomadIndex destroyed = 0;
    mutable NomadIndex freed = 0;
    NomadIndex failInitialization = 0;
    bool failCopy = false;
};

NomadString FailingElementType::getTypeName() const {
    return "FailingElement";
}

void FailingElementType::toString(const RuntimeValue&, NomadString& text) const {
    text = getTypeName();
}

void FailingElementType::initValue(RuntimeValue& value) const {
    value.setObjectValue(new TrackedArrayElement(destroyed));
    if (++initialized == failInitialization) {
        throw NomadException("Element initialization failed");
    }
}

void FailingElementType::freeValue(RuntimeValue& value) const {
    ++freed;
    ObjectTypeBase::freeValue(value);
}

void FailingElementType::copyValue(const RuntimeValue& source, RuntimeValue& destination) const {
    ObjectTypeBase::copyValue(source, destination);
    if (failCopy) {
        throw NomadException("Element copy failed");
    }
}

} // namespace

BOOST_AUTO_TEST_SUITE(array_objects)

BOOST_AUTO_TEST_CASE(invalid_indices_log_errors_and_return_fresh_owned_defaults) {
    struct LogCapture {
        MemorySink sink;
        LogCapture() {
            log::flush();
            log::addSink(&sink);
        }
        ~LogCapture() { log::removeSink(&sink); }
    } capture;
    IntegerType integers;
    ListType lists(&integers);
    const auto before = NomadObject::getLiveObjectCount();
    {
        Array1dObject array(&lists, 1);
        const ArrayValue first(array, -1);
        first.asObject<ListObject>()->push(RuntimeValue(NomadInteger{7}));
        const ArrayValue second(array, -1);
        BOOST_TEST(second.asObject<ListObject>()->getCount() == 0u);
        BOOST_TEST(first.asObject<ListObject>()->getCount() == 1u);
        array.setValue(1, RuntimeValue{});
        BOOST_TEST(ArrayValue(array, 0).asObject<ListObject>()->getCount() == 0u);
        BOOST_TEST(array.getCount() == 1u);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    log::flush();
    const auto& entries = capture.sink.getEntries();
    BOOST_REQUIRE(entries.size() == 3u);
    for (const auto& entry : entries) {
        BOOST_CHECK(entry.level == LogLevel::Error);
        BOOST_TEST(entry.message.find("out of bounds for dimension 1 of size 1") != NomadString::npos);
    }
}

BOOST_AUTO_TEST_CASE(invalid_read_default_initialization_failure_releases_partial_values) {
    FailingElementType elements;
    {
        Array1dObject array(&elements);
        elements.failInitialization = 1;
        BOOST_CHECK_THROW(ArrayValue(array, 0), NomadException);
        BOOST_TEST(elements.destroyed == 1u);
        elements.failInitialization = 0;
        BOOST_TEST(ArrayValue(array, 0).getObjectValue() != nullptr);
    }
    BOOST_TEST(elements.destroyed == 2u);
}

BOOST_AUTO_TEST_CASE(read_results_own_values_after_array_destruction_for_every_rank) {
    IntegerType integers;
    ListType lists(&integers);
    const auto before = NomadObject::getLiveObjectCount();
    std::array<RuntimeValue, 6> results;
    {
        Array1dObject line(&lists, 1);
        Array2dObject grid(&lists, 1, 1);
        Array3dObject volume(&lists, 1, 1, 1);
        line.getValue(0, results[0]);
        grid.getValue(0, 0, results[1]);
        volume.getValue(0, 0, 0, results[2]);
        line.getValue(-1, results[3]);
        grid.getValue(0, -1, results[4]);
        volume.getValue(0, 0, -1, results[5]);
        BOOST_CHECK_THROW(static_cast<void>(grid.getDimensionSize(2)), NomadException);
        BOOST_CHECK_THROW(static_cast<void>(volume.getDimensionSize(3)), NomadException);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before + results.size());
    for (auto& result : results) {
        BOOST_TEST(result.asObject<ListObject>()->getCount() == 0u);
        result.asObject<ListObject>()->push(RuntimeValue(NomadInteger{42}));
        BOOST_TEST(result.asObject<ListObject>()->getCount() == 1u);
        lists.freeValue(result);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(one_dimension_initializes_and_replaces_values) {
    IntegerType integers;
    Array1dObject array(&integers, 4);
    BOOST_TEST(array.getElementType() == &integers);
    BOOST_TEST(array.getCount() == 4u);
    BOOST_TEST(array.getDimensionCount() == 1u);
    BOOST_TEST(array.getDimensionSize(0) == 4u);
    for (auto index = NomadInteger{0}; index < 4; ++index) {
        BOOST_TEST(ArrayValue(array, index).getIntegerValue() == 0);
        array.setValue(index, RuntimeValue(index + 10));
    }
    for (auto index = NomadInteger{0}; index < 4; ++index) {
        BOOST_TEST(ArrayValue(array, index).getIntegerValue() == index + 10);
    }
    BOOST_TEST(ArrayValue(array, -1).getIntegerValue() == 0);
    BOOST_TEST(ArrayValue(array, 4).getIntegerValue() == 0);
    BOOST_CHECK_NO_THROW(array.setValue(4, RuntimeValue(NomadInteger{1})));
    BOOST_CHECK_THROW(array.getDimensionSize(1), NomadException);
}

BOOST_AUTO_TEST_CASE(multidimensional_elements_have_distinct_storage) {
    IntegerType integers;
    Array2dObject grid(&integers, 3, 4);
    Array3dObject volume(&integers, 3, 4, 2);
    BOOST_TEST(grid.getCount() == 12u);
    BOOST_TEST(volume.getCount() == 24u);
    BOOST_TEST(grid.getDimensionCount() == 2u);
    BOOST_TEST(volume.getDimensionCount() == 3u);
    BOOST_TEST(grid.getDimensionSize(0) == 3u);
    BOOST_TEST(grid.getDimensionSize(1) == 4u);
    BOOST_TEST(volume.getDimensionSize(2) == 2u);
    for (auto index1 = NomadInteger{0}; index1 < 3; ++index1) {
        for (auto index2 = NomadInteger{0}; index2 < 4; ++index2) {
            BOOST_TEST(ArrayValue(grid, index1, index2).getIntegerValue() == 0);
            grid.setValue(index1, index2, RuntimeValue(index1 * 10 + index2));
            for (auto index3 = NomadInteger{0}; index3 < 2; ++index3) {
                BOOST_TEST(ArrayValue(volume, index1, index2, index3).getIntegerValue() == 0);
                volume.setValue(index1, index2, index3, RuntimeValue(index1 * 100 + index2 * 10 + index3));
            }
        }
    }
    for (auto index1 = NomadInteger{0}; index1 < 3; ++index1) {
        for (auto index2 = NomadInteger{0}; index2 < 4; ++index2) {
            BOOST_TEST(ArrayValue(grid, index1, index2).getIntegerValue() == index1 * 10 + index2);
            for (auto index3 = NomadInteger{0}; index3 < 2; ++index3) {
                BOOST_TEST(ArrayValue(volume, index1, index2, index3).getIntegerValue() == index1 * 100 + index2 * 10 + index3);
            }
        }
    }
    BOOST_TEST(ArrayValue(grid, -1, 0).getIntegerValue() == 0);
    BOOST_TEST(ArrayValue(grid, 3, 0).getIntegerValue() == 0);
    BOOST_TEST(ArrayValue(grid, 0, -1).getIntegerValue() == 0);
    BOOST_TEST(ArrayValue(grid, 0, 4).getIntegerValue() == 0);
    BOOST_TEST(ArrayValue(volume, 3, 0, 0).getIntegerValue() == 0);
    BOOST_TEST(ArrayValue(volume, 0, 4, 0).getIntegerValue() == 0);
    BOOST_TEST(ArrayValue(volume, 0, 0, -1).getIntegerValue() == 0);
    BOOST_TEST(ArrayValue(volume, 0, 0, 2).getIntegerValue() == 0);
}

BOOST_AUTO_TEST_CASE(specialized_offsets_reject_each_invalid_coordinate_without_mutation) {
    IntegerType integers;
    Array1dObject line(&integers, 2);
    Array2dObject grid(&integers, 2, 3);
    Array3dObject volume(&integers, 2, 3, 4);
    const RuntimeValue value(NomadInteger{42});
    line.setValue(1, value);
    grid.setValue(1, 2, value);
    volume.setValue(1, 2, 3, value);
    for (const auto invalid : {NomadInteger{-1}, NomadInteger{2}, std::numeric_limits<NomadInteger>::max()}) {
        BOOST_TEST(ArrayValue(line, invalid).getIntegerValue() == 0);
        BOOST_CHECK_NO_THROW(line.setValue(invalid, value));
        BOOST_TEST(ArrayValue(grid, invalid, 0).getIntegerValue() == 0);
        BOOST_CHECK_NO_THROW(grid.setValue(invalid, 0, value));
        BOOST_TEST(ArrayValue(volume, invalid, 0, 0).getIntegerValue() == 0);
        BOOST_CHECK_NO_THROW(volume.setValue(invalid, 0, 0, value));
    }
    for (const auto invalid : {NomadInteger{-1}, NomadInteger{3}, std::numeric_limits<NomadInteger>::max()}) {
        BOOST_TEST(ArrayValue(grid, 0, invalid).getIntegerValue() == 0);
        BOOST_CHECK_NO_THROW(grid.setValue(0, invalid, value));
        BOOST_TEST(ArrayValue(volume, 0, invalid, 0).getIntegerValue() == 0);
        BOOST_CHECK_NO_THROW(volume.setValue(0, invalid, 0, value));
    }
    for (const auto invalid : {NomadInteger{-1}, NomadInteger{4}, std::numeric_limits<NomadInteger>::max()}) {
        BOOST_TEST(ArrayValue(volume, 0, 0, invalid).getIntegerValue() == 0);
        BOOST_CHECK_NO_THROW(volume.setValue(0, 0, invalid, value));
    }
    BOOST_TEST(ArrayValue(line, 1).getIntegerValue() == 42);
    BOOST_TEST(ArrayValue(grid, 1, 2).getIntegerValue() == 42);
    BOOST_TEST(ArrayValue(volume, 1, 2, 3).getIntegerValue() == 42);
    BOOST_TEST(ArrayValue(grid, 0, 0).getIntegerValue() == 0);
    BOOST_TEST(ArrayValue(volume, 0, 0, 0).getIntegerValue() == 0);
}

BOOST_AUTO_TEST_CASE(dimensions_and_element_types_are_validated_before_allocation) {
    IntegerType integers;
    VoidType voidType;
    FileNameType internalType;
    for (const auto size : {NomadInteger{0}, NomadInteger{-1}, std::numeric_limits<NomadInteger>::min()}) {
        BOOST_CHECK_THROW(Array1dObject(&integers, size), NomadException);
        BOOST_CHECK_THROW(Array2dObject(&integers, 2, size), NomadException);
        BOOST_CHECK_THROW(Array3dObject(&integers, 2, size, 3), NomadException);
    }
    const auto maximum = std::numeric_limits<NomadInteger>::max();
    BOOST_CHECK_THROW(Array1dObject(&integers, maximum), NomadException);
    BOOST_CHECK_THROW(Array2dObject(&integers, maximum, maximum), NomadException);
    BOOST_CHECK_THROW(Array3dObject(&integers, 1024, maximum, 1024), NomadException);
    BOOST_CHECK_THROW(Array1dObject(nullptr, 1), NomadException);
    BOOST_CHECK_THROW(Array2dObject(&voidType, 1, 1), NomadException);
    BOOST_CHECK_THROW(Array3dObject(&internalType, 1, 1, 1), NomadException);
    BOOST_CHECK_THROW(Array1Type(nullptr), NomadException);
    BOOST_CHECK_THROW(static_cast<void>(Array2Type(&voidType)), NomadException);
    BOOST_CHECK_THROW(static_cast<void>(Array3Type(&internalType)), NomadException);
}

BOOST_AUTO_TEST_CASE(default_values_include_zero_primitives_and_empty_strings) {
    IntegerType integers;
    FloatType floats;
    BooleanType booleans;
    StringType strings;
    RuntimeValue value(NomadInteger{123});
    integers.initValue(value);
    BOOST_TEST(value.getIntegerValue() == 0);
    Array1dObject floatArray(&floats, 2);
    Array1dObject booleanArray(&booleans, 2);
    RuntimeValue emptyString;
    strings.initValue(emptyString);
    strings.freeValue(emptyString);
    const auto before = NomadObject::getLiveObjectCount();
    {
        Array1dObject stringArray(&strings, 2);
        for (auto index = NomadInteger{0}; index < 2; ++index) {
            BOOST_TEST(ArrayValue(floatArray, index).getFloatValue() == 0.0f);
            BOOST_TEST(!ArrayValue(booleanArray, index).getBooleanValue());
            BOOST_TEST(strings.toString(ArrayValue(stringArray, index)) == "");
        }
        RuntimeValue text(NomadString{"hello"});
        stringArray.setValue(0, text);
        BOOST_TEST(text.getObjectValue()->getReferenceCount() == 2u);
        strings.freeValue(text);
        stringArray.setValue(0, ArrayValue(stringArray, 0));
        BOOST_TEST(strings.toString(ArrayValue(stringArray, 0)) == "hello");
        BOOST_TEST(strings.toString(ArrayValue(stringArray, 1)) == "");
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(array_defaults_are_lazy_shared_and_separate_by_descriptor) {
    IntegerType integers;
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue survivor;
    {
        Array2Type type(&integers);
        Array2Type otherType(&integers);
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        BOOST_TEST(type.getTypeName() == "Array2<int>");
        BOOST_TEST(type.getDimensionCount() == 2u);
        BOOST_TEST(type.getElementType() == &integers);
        RuntimeValue first;
        RuntimeValue second;
        RuntimeValue other;
        type.initValue(first);
        type.initValue(second);
        otherType.initValue(other);
        BOOST_TEST(first.getObjectValue() == second.getObjectValue());
        BOOST_TEST(first.getObjectValue() != other.getObjectValue());
        BOOST_TEST(first.getObjectValue()->getReferenceCount() == 3u);
        const auto* empty = first.asObject<Array2dObject>();
        BOOST_TEST(empty->getCount() == 0u);
        BOOST_TEST(empty->getDimensionSize(0) == 0u);
        BOOST_TEST(empty->getDimensionSize(1) == 0u);
        BOOST_TEST(ArrayValue(*empty, 0, 0).getIntegerValue() == 0);
        type.copyValue(first, survivor);
        type.freeValue(first);
        type.freeValue(second);
        otherType.freeValue(other);
    }
    BOOST_TEST(survivor.getObjectValue()->getReferenceCount() == 1u);
    survivor.freeObjectValue();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(each_array_rank_has_an_empty_default_that_rejects_writes) {
    IntegerType integers;
    const auto before = NomadObject::getLiveObjectCount();
    {
        Array1Type lineType(&integers);
        Array2Type gridType(&integers);
        Array3Type volumeType(&integers);
        for (auto* type : std::array<ArrayType*, 3>{&lineType, &gridType, &volumeType}) {
            const auto dimensions = type->getDimensionCount();
            RuntimeValue value;
            type->initValue(value);
            const auto* array = value.asObject<ArrayObject>();
            BOOST_TEST(array->getDimensionCount() == dimensions);
            BOOST_TEST(array->getCount() == 0u);
            for (auto dimension = NomadIndex{0}; dimension < dimensions; ++dimension) {
                BOOST_TEST(array->getDimensionSize(dimension) == 0u);
            }
            switch (dimensions) {
            case 1:
                BOOST_CHECK_NO_THROW(value.asObject<Array1dObject>()->setValue(0, RuntimeValue{}));
                break;
            case 2:
                BOOST_CHECK_NO_THROW(value.asObject<Array2dObject>()->setValue(0, 0, RuntimeValue{}));
                break;
            case 3:
                BOOST_CHECK_NO_THROW(value.asObject<Array3dObject>()->setValue(0, 0, 0, RuntimeValue{}));
                break;
            }
            type->freeValue(value);
        }
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(rank_specific_typed_factories) {
    static_assert(std::is_abstract_v<ArrayType>);
    IntegerType integers;
    const auto before = NomadObject::getLiveObjectCount();
    {
        Array1Type lineType(&integers);
        Array2Type gridType(&integers);
        Array3Type volumeType(&integers);
        const std::unique_ptr<Array1dObject> line(lineType.createObject(NomadInteger{2}));
        const std::unique_ptr<Array2dObject> grid(gridType.createObject(NomadInteger{2}, NomadInteger{3}));
        const std::unique_ptr<Array3dObject> volume(
            volumeType.createObject(NomadInteger{2}, NomadInteger{3}, NomadInteger{4})
        );
        BOOST_TEST(line->getCount() == 2u);
        BOOST_TEST(grid->getCount() == 6u);
        BOOST_TEST(volume->getCount() == 24u);
        BOOST_CHECK_THROW(lineType.createObject(NomadInteger{0}), NomadException);
        BOOST_CHECK_THROW(gridType.createObject(NomadInteger{2}, NomadInteger{-1}), NomadException);
        BOOST_CHECK_THROW(
            volumeType.createObject(NomadInteger{2}, NomadInteger{3}, NomadInteger{0}), NomadException
        );
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(nested_defaults_share_arrays_but_not_mutable_lists) {
    IntegerType integers;
    const auto before = NomadObject::getLiveObjectCount();
    {
        ListType listType(&integers);
        Array1dObject lists(&listType, 2);
        const ArrayValue first(lists, 0);
        const ArrayValue second(lists, 1);
        auto* firstList = first.asObject<ListObject>();
        auto* secondList = second.asObject<ListObject>();
        BOOST_TEST(firstList != secondList);
        firstList->push(RuntimeValue(NomadInteger{42}));
        BOOST_TEST(firstList->getCount() == 1u);
        BOOST_TEST(secondList->getCount() == 0u);

        Array3Type arrayType(&integers);
        Array1dObject arrays(&arrayType, 2);
        BOOST_TEST(ArrayValue(arrays, 0).getObjectValue() == ArrayValue(arrays, 1).getObjectValue());
        BOOST_TEST(ArrayValue(arrays, 0).asObject<Array3dObject>()->getCount() == 0u);
        RuntimeValue replacement;
        replacement.setObjectValue(new Array3dObject(&integers, 1, 2, 3));
        arrays.setValue(0, replacement);
        arrayType.freeValue(replacement);
        BOOST_TEST(ArrayValue(arrays, 0).asObject<Array3dObject>()->getCount() == 6u);
        BOOST_TEST(ArrayValue(arrays, 1).asObject<Array3dObject>()->getCount() == 0u);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(partial_initialization_and_failed_replacement_release_owned_values) {
    const auto before = NomadObject::getLiveObjectCount();
    FailingElementType type;
    type.failInitialization = 3;
    BOOST_CHECK_THROW(Array1dObject(&type, 5), NomadException);
    BOOST_TEST(type.initialized == 3u);
    BOOST_TEST(type.freed == 3u);
    BOOST_TEST(type.destroyed == 3u);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);

    type.failInitialization = 0;
    {
        Array1dObject array(&type, 1);
        RuntimeValue value;
        type.initValue(value);
        const auto* original = ArrayValue(array, 0).getObjectValue();
        type.failCopy = true;
        BOOST_CHECK_THROW(array.setValue(0, value), NomadException);
        BOOST_TEST(type.initialized == 5u);
        BOOST_CHECK_THROW(ArrayValue(array, 0), NomadException);
        type.failCopy = false;
        BOOST_TEST(ArrayValue(array, 0).getObjectValue() == original);
        BOOST_TEST(value.getObjectValue()->getReferenceCount() == 1u);
        type.freeValue(value);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(copying_arrays_does_not_construct_unused_defaults) {
    IntegerType integers;
    const auto before = NomadObject::getLiveObjectCount();
    {
        Array1Type type(&integers);
        RuntimeValue source;
        RuntimeValue destination;
        source.setObjectValue(new Array1dObject(&integers, 2));
        type.assignValue(source, destination);
        BOOST_TEST(source.getObjectValue() == destination.getObjectValue());
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2u);
        BOOST_TEST(NomadObject::getLiveObjectCount() == before + 1);
        type.freeValue(source);
        type.freeValue(destination);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(empty_caches_do_not_access_destroyed_element_descriptors) {
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto elementType = std::make_unique<IntegerType>();
        auto type = std::make_unique<Array1Type>(elementType.get());
        RuntimeValue value;
        type->initValue(value);
        type->freeValue(value);
        elementType.reset();
        type.reset();
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_SUITE_END()
