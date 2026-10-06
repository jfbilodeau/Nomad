// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/system/String.hpp"
#include "nomad/system/TempHeap.hpp"
#include "nomad/system/Memory.hpp"

#include <cstdint>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

using namespace nomad;

BOOST_AUTO_TEST_CASE(string_to_string_and_basic_conversions)
{
    BOOST_TEST(toString(true) == "true");
    BOOST_TEST(toString(false) == "false");

    BOOST_TEST(toString(static_cast<NomadId>(123)) == "123");
    BOOST_TEST(toString(static_cast<NomadInteger>(-45)) == "-45");

    const NomadString fstr = toString(static_cast<NomadFloat>(1.5f));
    // parse back the float to verify numeric value rather than exact textual formatting
    float parsed = std::stof(fstr);
    BOOST_TEST(parsed == 1.5f);

    BOOST_TEST(toString(static_cast<NomadIndex>(42)) == "42");
}

BOOST_AUTO_TEST_CASE(string_trim_and_comparisons)
{
    NomadString s1 = "   hello world";
    stringLeftTrim(s1);
    BOOST_TEST(s1 == "hello world");

    NomadString s2 = "goodbye world   ";
    stringRightTrim(s2);
    BOOST_TEST(s2 == "goodbye world");

    NomadString s3 = "   surround   ";
    stringTrim(s3);
    BOOST_TEST(s3 == "surround");

    NomadString c1 = "a";
    NomadString c2 = "b";
    BOOST_TEST(stringLessThan(c1, c2));
    BOOST_TEST(stringGreaterThan(c2, c1));

    BOOST_TEST(stringEqualTo("abc", "abc"));
    BOOST_TEST(stringEqualTo(std::string("abc"), std::string("abc")));

    BOOST_TEST(stringConcatenate(std::string("x"), std::string("y")) == "xy");
}

BOOST_AUTO_TEST_CASE(string_views_do_not_require_null_termination)
{
    const NomadString backing = "xxalphabetayy";
    const NomadStringView alpha(backing.data() + 2, 5);
    const NomadStringView beta(backing.data() + 7, 4);

    BOOST_TEST(stringEqualTo(alpha, "alpha"));
    BOOST_TEST(stringLessThan(alpha, beta));
    BOOST_TEST(stringConcatenate(alpha, beta) == "alphabeta");

    std::unordered_map<NomadString, NomadInteger, NomadStringHash, NomadStringEqual> values;
    values.emplace("alpha", 1);
    BOOST_TEST(values.contains(alpha));
}

BOOST_AUTO_TEST_CASE(temp_heap_split_and_create_temp_strings_and_vectors)
{
    // Ensure we start from a clean temporary heap
    resetTempHeap();

    // Test splitLines with NomadString
    NomadString text = "line1\nline2\r\nline3";
    std::vector<NomadString> out;
    splitLines(text, out);
    BOOST_TEST(out.size() == 3);
    BOOST_TEST(out[0] == "line1");
    BOOST_TEST(out[1] == "line2");
    BOOST_TEST(out[2] == "line3");

    // Test TempString overload of splitLines
    TempString t = createTempString("alpha\r\nbeta\ncharlie");
    TempStringVector tempOut;
    splitLines(t, tempOut);
    BOOST_TEST(tempOut.size() == 3);
    BOOST_TEST(tempOut[0] == "alpha");
    BOOST_TEST(tempOut[1] == "beta");
    BOOST_TEST(tempOut[2] == "charlie");

    // Test split with custom separator
    TempString csv = createTempString("a,b,c");
    TempStringVector csvOut;
    split(csv, ",", csvOut);
    BOOST_TEST(csvOut.size() == 3);
    BOOST_TEST(csvOut[0] == "a");
    BOOST_TEST(csvOut[1] == "b");
    BOOST_TEST(csvOut[2] == "c");

    const NomadString splitBacking = "xxleft::rightyy";
    const NomadStringView splitText(splitBacking.data() + 2, 11);
    TempStringVector viewOut;
    split(splitText, "::", viewOut);
    BOOST_TEST(viewOut.size() == 2);
    BOOST_TEST(viewOut[0] == "left");
    BOOST_TEST(viewOut[1] == "right");

    // Creating temporary containers should allocate from the temp heap
    auto tv = createTempVector<int>(3, 7);
    BOOST_TEST(tv.size() == 3);
    BOOST_TEST(tv[0] == 7);

    auto svec = createTempStringVector(std::vector<NomadString>{"x", "y"});
    BOOST_TEST(svec.size() == 2);
    BOOST_TEST(svec[0] == "x");

    // The temp heap should report allocated size > 0
    BOOST_TEST(getTempHeapSize() > 0);

    // Reset and ensure it goes back to 0
    resetTempHeap();
    BOOST_TEST(getTempHeapSize() == 0);
}

BOOST_AUTO_TEST_CASE(default_allocator_basic)
{
    Allocator* alloc = getDefaultAllocator();
    BOOST_TEST(alloc != nullptr);

    void* p = alloc->allocate(16);
    BOOST_TEST(p != nullptr);
    alloc->deallocate(p);

    resetTempHeap();
}

BOOST_AUTO_TEST_CASE(temp_heap_reuses_storage_after_reset)
{
    resetTempHeap();
    auto* resource = getTempBuffer();
    auto* first = resource->allocate(64, 64);
    BOOST_TEST(reinterpret_cast<std::uintptr_t>(first) % 64 == 0);

    const auto used = getTempHeapSize();
    BOOST_TEST(used >= 64);
    resource->deallocate(first, 64, 64);
    BOOST_TEST(getTempHeapSize() == used);

    resetTempHeap();
    BOOST_TEST(getTempHeapSize() == 0);
    const auto* reused = resource->allocate(64, 64);
    BOOST_TEST(reused == first);
    resetTempHeap();

    BOOST_CHECK_THROW(resource->allocate(getTempHeapMaxSize() + 1), std::bad_alloc);
    BOOST_TEST(getTempHeapSize() == 0);
}
