// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/RectanglePacker.hpp>

#include <algorithm>
#include <limits>
#include <set>
#include <tuple>
#include <vector>

namespace nomad {

namespace {

struct PackingRectangle {
    NomadInteger x;
    NomadInteger y;
    NomadInteger width;
    NomadInteger height;
};

struct WorkingPage {
    std::vector<PackingRectangle> freeRectangles;
    RectanglePackingPage result;
};

struct PlacementCandidate {
    bool found = false;
    NomadInteger x = 0;
    NomadInteger y = 0;
    NomadInteger shortSide = 0;
    NomadInteger longSide = 0;
};

bool intersects(const PackingRectangle& left, const PackingRectangle& right) {
    return
        left.x < right.x + right.width &&
        left.x + left.width > right.x &&
        left.y < right.y + right.height &&
        left.y + left.height > right.y;
}

bool contains(const PackingRectangle& outer, const PackingRectangle& inner) {
    return
        inner.x >= outer.x &&
        inner.y >= outer.y &&
        inner.x + inner.width <= outer.x + outer.width &&
        inner.y + inner.height <= outer.y + outer.height;
}

PlacementCandidate findPlacement(
    const WorkingPage& page,
    const NomadInteger width,
    const NomadInteger height
) {
    PlacementCandidate best;

    for (const auto& freeRectangle : page.freeRectangles) {
        if (width > freeRectangle.width || height > freeRectangle.height) {
            continue;
        }

        const auto remainingWidth = freeRectangle.width - width;
        const auto remainingHeight = freeRectangle.height - height;
        const auto shortSide = std::min(remainingWidth, remainingHeight);
        const auto longSide = std::max(remainingWidth, remainingHeight);
        const auto score = std::tuple(shortSide, longSide, freeRectangle.y, freeRectangle.x);
        const auto bestScore = std::tuple(best.shortSide, best.longSide, best.y, best.x);

        if (!best.found || score < bestScore) {
            best = {
                true,
                freeRectangle.x,
                freeRectangle.y,
                shortSide,
                longSide
            };
        }
    }

    return best;
}

void pruneFreeRectangles(std::vector<PackingRectangle>& freeRectangles) {
    for (NomadIndex left = 0; left < freeRectangles.size();) {
        auto removeLeft = false;

        for (NomadIndex right = 0; right < freeRectangles.size(); ++right) {
            if (left != right && contains(freeRectangles[right], freeRectangles[left])) {
                removeLeft = true;
                break;
            }
        }

        if (removeLeft) {
            freeRectangles.erase(freeRectangles.begin() + static_cast<std::ptrdiff_t>(left));
        } else {
            ++left;
        }
    }
}

void reserveRectangle(WorkingPage& page, const PackingRectangle& used) {
    std::vector<PackingRectangle> remaining;

    for (const auto& freeRectangle : page.freeRectangles) {
        if (!intersects(freeRectangle, used)) {
            remaining.push_back(freeRectangle);
            continue;
        }

        if (used.x > freeRectangle.x) {
            remaining.push_back({
                freeRectangle.x,
                freeRectangle.y,
                used.x - freeRectangle.x,
                freeRectangle.height
            });
        }
        if (used.x + used.width < freeRectangle.x + freeRectangle.width) {
            remaining.push_back({
                used.x + used.width,
                freeRectangle.y,
                freeRectangle.x + freeRectangle.width - used.x - used.width,
                freeRectangle.height
            });
        }
        if (used.y > freeRectangle.y) {
            remaining.push_back({
                freeRectangle.x,
                freeRectangle.y,
                freeRectangle.width,
                used.y - freeRectangle.y
            });
        }
        if (used.y + used.height < freeRectangle.y + freeRectangle.height) {
            remaining.push_back({
                freeRectangle.x,
                used.y + used.height,
                freeRectangle.width,
                freeRectangle.y + freeRectangle.height - used.y - used.height
            });
        }
    }

    page.freeRectangles = std::move(remaining);
    pruneFreeRectangles(page.freeRectangles);
}

} // namespace

std::vector<RectanglePackingPage> packRectangles(
    const std::vector<RectanglePackingInput>& inputs,
    const NomadInteger maxTextureSize,
    const NomadInteger padding
) {
    if (maxTextureSize <= 0) {
        throw RectanglePackerException("Maximum texture size must be greater than zero");
    }
    if (padding < 0) {
        throw RectanglePackerException("Sprite padding must not be negative");
    }
    if (padding > std::numeric_limits<NomadInteger>::max() - maxTextureSize) {
        throw RectanglePackerException("Maximum texture size and padding overflow");
    }

    auto sortedInputs = inputs;
    std::set<NomadIndex> ids;

    for (const auto& input : sortedInputs) {
        if (input.width <= 0 || input.height <= 0) {
            throw RectanglePackerException("Packed rectangle dimensions must be greater than zero");
        }
        if (input.width > maxTextureSize || input.height > maxTextureSize) {
            throw RectanglePackerException(
                "Packed rectangle " + std::to_string(input.id) +
                " exceeds the maximum texture size"
            );
        }
        if (!ids.insert(input.id).second) {
            throw RectanglePackerException("Packed rectangle IDs must be unique");
        }
        if (
            input.width > std::numeric_limits<NomadInteger>::max() - padding ||
            input.height > std::numeric_limits<NomadInteger>::max() - padding
        ) {
            throw RectanglePackerException("Packed rectangle dimensions and padding overflow");
        }
    }

    std::ranges::sort(sortedInputs, [](const auto& left, const auto& right) {
        const auto leftLongestSide = std::max(left.width, left.height);
        const auto rightLongestSide = std::max(right.width, right.height);

        if (leftLongestSide != rightLongestSide) {
            return leftLongestSide > rightLongestSide;
        }

        const auto leftShortestSide = std::min(left.width, left.height);
        const auto rightShortestSide = std::min(right.width, right.height);

        if (leftShortestSide != rightShortestSide) {
            return leftShortestSide > rightShortestSide;
        }
        if (left.width != right.width) {
            return left.width > right.width;
        }

        return left.id < right.id;
    });

    const auto virtualSize = maxTextureSize + padding;
    std::vector<WorkingPage> pages;

    for (const auto& input : sortedInputs) {
        const auto reservedWidth = input.width + padding;
        const auto reservedHeight = input.height + padding;
        NomadIndex pageIndex = 0;
        PlacementCandidate placement;

        for (; pageIndex < pages.size(); ++pageIndex) {
            placement = findPlacement(pages[pageIndex], reservedWidth, reservedHeight);

            if (placement.found) {
                break;
            }
        }

        if (!placement.found) {
            WorkingPage page;
            page.freeRectangles.push_back({0, 0, virtualSize, virtualSize});
            pages.push_back(std::move(page));
            pageIndex = pages.size() - 1U;
            placement = findPlacement(pages[pageIndex], reservedWidth, reservedHeight);
        }

        const PackingRectangle used{
            placement.x,
            placement.y,
            reservedWidth,
            reservedHeight
        };
        auto& page = pages[pageIndex];
        reserveRectangle(page, used);
        page.result.rectangles.push_back({
            input.id,
            Rectangle(placement.x, placement.y, input.width, input.height)
        });
        page.result.width = std::max(page.result.width, placement.x + input.width);
        page.result.height = std::max(page.result.height, placement.y + input.height);
    }

    std::vector<RectanglePackingPage> results;
    results.reserve(pages.size());

    for (auto& page : pages) {
        results.push_back(std::move(page.result));
    }

    return results;
}

} // namespace nomad
