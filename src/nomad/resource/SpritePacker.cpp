// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/SpritePacker.hpp>

#include <nomad/resource/RectanglePacker.hpp>

#include <nomad/system/Path.hpp>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nomad {

namespace {

struct PreparedImage {
    const SpriteSourceDefinition* source;
    SdlSurfacePtr surface;
    Rectangle trimmed;
    Rectangle logicalSource;
    std::vector<std::uint8_t> trimmedPixels;
    std::uint64_t hash;
};

struct UniqueImage {
    NomadIndex preparedIndex;
    NomadInteger width;
    NomadInteger height;
};

struct GroupPreparation {
    std::vector<PreparedImage> images;
    std::vector<UniqueImage> uniqueImages;
    std::vector<NomadIndex> aliases;
};

[[noreturn]] void raiseImageError(
    const SpriteSourceDefinition& source,
    const NomadStringView operation
) {
    throw SpritePackerError(
        NomadString(operation) + " for sprite '" + source.name + "' from '" +
        pathToUtf8(source.filePath) + "': " + SDL_GetError()
    );
}

class SurfaceLock {
public:
    SurfaceLock(SDL_Surface* surface, const SpriteSourceDefinition& source) :
        m_surface(surface)
    {
        if (!SDL_LockSurface(m_surface)) {
            raiseImageError(source, "Failed to lock image");
        }
    }

    SurfaceLock(const SurfaceLock&) = delete;
    SurfaceLock& operator=(const SurfaceLock&) = delete;

    ~SurfaceLock() {
        SDL_UnlockSurface(m_surface);
    }

private:
    SDL_Surface* m_surface;
};

Rectangle normalizeAndFindTrimBounds(
    SDL_Surface* surface,
    const SpriteSourceDefinition& source
) {
    const SurfaceLock lock(surface, source);

    auto minimumX = surface->w;
    auto minimumY = surface->h;
    auto maximumX = -1;
    auto maximumY = -1;
    auto* pixels = static_cast<std::uint8_t*>(surface->pixels);

    for (auto y = 0; y < surface->h; ++y) {
        auto* row = pixels + y * surface->pitch;

        for (auto x = 0; x < surface->w; ++x) {
            auto* pixel = row + x * 4;

            if (pixel[3] == 0) {
                pixel[0] = 0;
                pixel[1] = 0;
                pixel[2] = 0;
                continue;
            }

            minimumX = std::min(minimumX, x);
            minimumY = std::min(minimumY, y);
            maximumX = std::max(maximumX, x);
            maximumY = std::max(maximumY, y);
        }
    }

    if (maximumX < 0) {
        return {0, 0, 1, 1};
    }

    return {
        minimumX,
        minimumY,
        maximumX - minimumX + 1,
        maximumY - minimumY + 1
    };
}

std::vector<std::uint8_t> copyTrimmedPixels(
    SDL_Surface* surface,
    const Rectangle& trimmed,
    const SpriteSourceDefinition& source
) {
    const SurfaceLock lock(surface, source);
    std::vector<std::uint8_t> result;
    const auto rowBytes = static_cast<std::size_t>(trimmed.getWidth()) * 4U;
    result.reserve(rowBytes * static_cast<std::size_t>(trimmed.getHeight()));
    const auto* pixels = static_cast<const std::uint8_t*>(surface->pixels);

    for (auto y = 0; y < trimmed.getHeight(); ++y) {
        const auto* row =
            pixels +
            (trimmed.getTop() + y) * surface->pitch +
            trimmed.getLeft() * 4;
        result.insert(result.end(), row, row + rowBytes);
    }

    return result;
}

std::uint64_t hashTrimmedPixels(const PreparedImage& image) {
    constexpr std::uint64_t FNV_OFFSET = 14695981039346656037ULL;
    constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;
    auto hash = FNV_OFFSET;
    auto hashValue = [&](const std::uint8_t value) {
        hash ^= value;
        hash *= FNV_PRIME;
    };

    for (const auto dimension : {image.trimmed.getWidth(), image.trimmed.getHeight()}) {
        auto value = static_cast<std::uint64_t>(dimension);

        for (auto byte = 0; byte < 8; ++byte) {
            hashValue(static_cast<std::uint8_t>(value & 0xffU));
            value >>= 8U;
        }
    }

    for (const auto value : image.trimmedPixels) {
        hashValue(value);
    }

    return hash;
}

bool trimmedPixelsEqual(const PreparedImage& left, const PreparedImage& right) {
    if (
        left.trimmed.getWidth() != right.trimmed.getWidth() ||
        left.trimmed.getHeight() != right.trimmed.getHeight()
    ) {
        return false;
    }

    return left.trimmedPixels == right.trimmedPixels;
}

PreparedImage prepareImage(const SpriteSourceDefinition& source) {
    SdlSurfacePtr loaded(IMG_Load(pathToUtf8(source.filePath).c_str()));

    if (!loaded) {
        raiseImageError(source, "Failed to load image");
    }

    SdlSurfacePtr converted(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32));

    if (!converted) {
        raiseImageError(source, "Failed to convert image to RGBA");
    }
    if (converted->w <= 0 || converted->h <= 0) {
        throw SpritePackerError(
            "Sprite image must have positive dimensions: '" + pathToUtf8(source.filePath) + "'"
        );
    }

    auto image = PreparedImage{
        &source,
        std::move(converted),
        {},
        {},
        {},
        0
    };
    image.trimmed = normalizeAndFindTrimBounds(image.surface.get(), source);
    image.logicalSource = Rectangle(
        image.trimmed.getLeft(),
        image.trimmed.getTop(),
        image.surface->w,
        image.surface->h
    );
    image.trimmedPixels = copyTrimmedPixels(image.surface.get(), image.trimmed, source);
    image.hash = hashTrimmedPixels(image);
    return image;
}

GroupPreparation prepareGroup(const std::vector<const SpriteSourceDefinition*>& sources) {
    GroupPreparation preparation;
    std::unordered_map<std::uint64_t, std::vector<NomadIndex>> candidates;
    preparation.images.reserve(sources.size());
    preparation.aliases.reserve(sources.size());

    for (const auto* source : sources) {
        preparation.images.push_back(prepareImage(*source));
        const auto preparedIndex = preparation.images.size() - 1U;
        const auto& image = preparation.images.back();
        auto alias = NOMAD_INVALID_INDEX;

        if (const auto matching = candidates.find(image.hash); matching != candidates.end()) {
            for (const auto uniqueIndex : matching->second) {
                const auto& unique = preparation.uniqueImages[uniqueIndex];

                if (trimmedPixelsEqual(image, preparation.images[unique.preparedIndex])) {
                    alias = uniqueIndex;
                    break;
                }
            }
        }

        if (alias == NOMAD_INVALID_INDEX) {
            alias = preparation.uniqueImages.size();
            preparation.uniqueImages.push_back({
                preparedIndex,
                image.trimmed.getWidth(),
                image.trimmed.getHeight()
            });
            candidates[image.hash].push_back(alias);
        }

        preparation.aliases.push_back(alias);
    }

    return preparation;
}

SdlSurfacePtr createAtlasSurface(
    const RectanglePackingPage& layout,
    const GroupPreparation& preparation
) {
    if (
        layout.width > std::numeric_limits<int>::max() ||
        layout.height > std::numeric_limits<int>::max()
    ) {
        throw SpritePackerError("Packed texture dimensions exceed SDL surface limits");
    }

    SdlSurfacePtr surface(SDL_CreateSurface(
        static_cast<int>(layout.width),
        static_cast<int>(layout.height),
        SDL_PIXELFORMAT_RGBA32
    ));

    if (!surface) {
        throw SpritePackerError("Failed to create packed texture surface: " + NomadString(SDL_GetError()));
    }
    if (!SDL_FillSurfaceRect(surface.get(), nullptr, SDL_MapSurfaceRGBA(surface.get(), 0, 0, 0, 0))) {
        throw SpritePackerError("Failed to clear packed texture surface: " + NomadString(SDL_GetError()));
    }

    for (const auto& placement : layout.rectangles) {
        const auto& unique = preparation.uniqueImages[placement.id];
        const auto& image = preparation.images[unique.preparedIndex];
        auto sourceRectangle = image.trimmed.toSdlRect();
        auto destinationRectangle = placement.frame.toSdlRect();

        if (!SDL_SetSurfaceBlendMode(image.surface.get(), SDL_BLENDMODE_NONE)) {
            raiseImageError(*image.source, "Failed to configure image blending");
        }
        if (!SDL_BlitSurface(
            image.surface.get(),
            &sourceRectangle,
            surface.get(),
            &destinationRectangle
        )) {
            raiseImageError(*image.source, "Failed to copy image into packed texture");
        }
    }

    return surface;
}

PackedSpriteGroup packGroup(
    const NomadString& name,
    const std::vector<const SpriteSourceDefinition*>& sources,
    const NomadInteger maxTextureSize,
    const NomadInteger padding
) {
    auto preparation = prepareGroup(sources);
    std::vector<RectanglePackingInput> inputs;
    inputs.reserve(preparation.uniqueImages.size());

    for (NomadIndex index = 0; index < preparation.uniqueImages.size(); ++index) {
        const auto& image = preparation.uniqueImages[index];
        inputs.push_back({index, image.width, image.height});
    }

    const auto layouts = packRectangles(inputs, maxTextureSize, padding);
    std::vector<NomadIndex> uniquePages(preparation.uniqueImages.size());
    std::vector<Rectangle> uniqueFrames(preparation.uniqueImages.size());
    PackedSpriteGroup result;
    result.name = name;
    result.pages.reserve(layouts.size());

    for (NomadIndex pageIndex = 0; pageIndex < layouts.size(); ++pageIndex) {
        const auto& layout = layouts[pageIndex];

        for (const auto& rectangle : layout.rectangles) {
            uniquePages[rectangle.id] = pageIndex;
            uniqueFrames[rectangle.id] = rectangle.frame;
        }

        result.pages.push_back({
            layout.width,
            layout.height,
            createAtlasSurface(layout, preparation)
        });
    }

    result.sprites.reserve(preparation.images.size());

    for (NomadIndex index = 0; index < preparation.images.size(); ++index) {
        const auto& image = preparation.images[index];
        const auto uniqueIndex = preparation.aliases[index];
        result.sprites.push_back({
            image.source->name,
            uniquePages[uniqueIndex],
            image.logicalSource,
            uniqueFrames[uniqueIndex]
        });
    }

    return result;
}

} // namespace

void SdlSurfaceDeleter::operator()(SDL_Surface* surface) const {
    SDL_DestroySurface(surface);
}

std::vector<PackedSpriteGroup> packSpriteGroups(
    const std::vector<SpriteSourceDefinition>& sources,
    const NomadInteger maxTextureSize,
    const NomadInteger padding
) {
    std::map<NomadString, std::vector<const SpriteSourceDefinition*>> groups;

    for (const auto& source : sources) {
        groups[source.group].push_back(&source);
    }

    std::vector<PackedSpriteGroup> results;
    results.reserve(groups.size());

    for (const auto& [name, groupSources] : groups) {
        results.push_back(packGroup(name, groupSources, maxTextureSize, padding));
    }

    return results;
}

} // namespace nomad
