// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad::test;

BOOST_AUTO_TEST_CASE(test_directories_with_the_same_name_have_independent_lifetimes) {
    TestDirectory first("nomad_test_directory_isolation");
    const auto firstFile = first.write("value.txt", "first");
    std::filesystem::path secondPath;
    {
        TestDirectory second("nomad_test_directory_isolation");
        secondPath = second.getPath();
        BOOST_TEST(first.getPath() != secondPath);
        BOOST_TEST(std::filesystem::is_regular_file(firstFile));
        second.write("value.txt", "second");
    }
    BOOST_TEST(!std::filesystem::exists(secondPath));
    BOOST_TEST(std::filesystem::is_regular_file(firstFile));
}
