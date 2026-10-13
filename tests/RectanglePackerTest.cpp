// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/RectanglePacker.hpp>

#include <boost/test/unit_test.hpp>

#include <vector>

using namespace nomad;

namespace {

bool overlaps(const Rectangle& left, const Rectangle& right, const NomadInteger padding) {
    return !(
        left.getRight() + padding <= right.getLeft() ||
        right.getRight() + padding <= left.getLeft() ||
        left.getBottom() + padding <= right.getTop() ||
        right.getBottom() + padding <= left.getTop()
    );
}

} // namespace

BOOST_AUTO_TEST_SUITE(rectangle_packer)

BOOST_AUTO_TEST_CASE(returns_no_pages_for_no_rectangles)
{
    BOOST_TEST(packRectangles({}, 2048, 1).empty());
}

BOOST_AUTO_TEST_CASE(packs_an_exact_maximum_size_rectangle)
{
    const auto pages = packRectangles({{7, 8, 8}}, 8, 1);

    BOOST_REQUIRE(pages.size() == 1U);
    BOOST_TEST(pages[0].width == 8);
    BOOST_TEST(pages[0].height == 8);
    BOOST_REQUIRE(pages[0].rectangles.size() == 1U);
    BOOST_TEST(pages[0].rectangles[0].id == 7U);
    BOOST_TEST(pages[0].rectangles[0].frame.getLeft() == 0);
    BOOST_TEST(pages[0].rectangles[0].frame.getTop() == 0);
}

BOOST_AUTO_TEST_CASE(applies_exact_inter_sprite_padding_and_shrinks_the_page)
{
    const auto pages = packRectangles({{0, 3, 2}, {1, 3, 2}}, 7, 1);

    BOOST_REQUIRE(pages.size() == 1U);
    BOOST_TEST(pages[0].width == 7);
    BOOST_TEST(pages[0].height == 2);
    BOOST_REQUIRE(pages[0].rectangles.size() == 2U);
    BOOST_TEST(!overlaps(pages[0].rectangles[0].frame, pages[0].rectangles[1].frame, 1));
}

BOOST_AUTO_TEST_CASE(splits_rectangles_across_pages)
{
    const auto pages = packRectangles({{0, 4, 4}, {1, 4, 4}}, 4, 1);

    BOOST_REQUIRE(pages.size() == 2U);
    BOOST_TEST(pages[0].width == 4);
    BOOST_TEST(pages[0].height == 4);
    BOOST_TEST(pages[1].width == 4);
    BOOST_TEST(pages[1].height == 4);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_inputs)
{
    BOOST_CHECK_THROW((void)packRectangles({{0, 1, 1}}, 0, 1), RectanglePackerError);
    BOOST_CHECK_THROW((void)packRectangles({{0, 1, 1}}, 8, -1), RectanglePackerError);
    BOOST_CHECK_THROW((void)packRectangles({{0, 0, 1}}, 8, 1), RectanglePackerError);
    BOOST_CHECK_THROW((void)packRectangles({{0, 1, 0}}, 8, 1), RectanglePackerError);
    BOOST_CHECK_THROW((void)packRectangles({{0, 9, 1}}, 8, 1), RectanglePackerError);
    BOOST_CHECK_THROW((void)packRectangles({{0, 1, 1}, {0, 1, 1}}, 8, 1), RectanglePackerError);
}

BOOST_AUTO_TEST_CASE(produces_the_same_layout_for_shuffled_inputs)
{
    const std::vector<RectanglePackingInput> inputs{
        {0, 7, 3},
        {1, 4, 8},
        {2, 2, 2},
        {3, 6, 5}
    };
    const std::vector<RectanglePackingInput> shuffled{
        inputs[2],
        inputs[0],
        inputs[3],
        inputs[1]
    };

    const auto first = packRectangles(inputs, 16, 1);
    const auto second = packRectangles(shuffled, 16, 1);

    BOOST_REQUIRE(first.size() == second.size());

    for (NomadIndex page = 0; page < first.size(); ++page) {
        BOOST_TEST(first[page].width == second[page].width);
        BOOST_TEST(first[page].height == second[page].height);
        BOOST_REQUIRE(first[page].rectangles.size() == second[page].rectangles.size());

        for (NomadIndex rectangle = 0; rectangle < first[page].rectangles.size(); ++rectangle) {
            const auto& left = first[page].rectangles[rectangle];
            const auto& right = second[page].rectangles[rectangle];
            BOOST_TEST(left.id == right.id);
            BOOST_TEST(left.frame.getLeft() == right.frame.getLeft());
            BOOST_TEST(left.frame.getTop() == right.frame.getTop());
            BOOST_TEST(left.frame.getWidth() == right.frame.getWidth());
            BOOST_TEST(left.frame.getHeight() == right.frame.getHeight());
        }
    }
}

BOOST_AUTO_TEST_CASE(keeps_all_rectangles_inside_pages_without_overlap)
{
    const auto pages = packRectangles(
        {
            {0, 10, 7},
            {1, 8, 9},
            {2, 5, 5},
            {3, 3, 11},
            {4, 6, 4},
            {5, 2, 2}
        },
        16,
        2
    );

    for (const auto& page : pages) {
        BOOST_TEST(page.width <= 16);
        BOOST_TEST(page.height <= 16);

        for (NomadIndex left = 0; left < page.rectangles.size(); ++left) {
            const auto& frame = page.rectangles[left].frame;
            BOOST_TEST(frame.getLeft() >= 0);
            BOOST_TEST(frame.getTop() >= 0);
            BOOST_TEST(frame.getRight() <= page.width);
            BOOST_TEST(frame.getBottom() <= page.height);

            for (auto right = left + 1U; right < page.rectangles.size(); ++right) {
                BOOST_TEST(!overlaps(frame, page.rectangles[right].frame, 2));
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
