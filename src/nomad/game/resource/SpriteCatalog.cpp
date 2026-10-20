// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/resource/SpriteCatalog.hpp>

#include <nomad/game/resource/SpriteDiscovery.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/String.hpp>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <boost/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>

namespace nomad {

namespace {

[[noreturn]] void raiseCatalogError(
    const NomadPath& catalog,
    const NomadStringView message
) {
    throw SpriteCatalogException(
        "Invalid sprite catalog '" + pathToUtf8(catalog) + "': " + NomadString(message)
    );
}

NomadInteger readInteger(
    const boost::json::object& object,
    const NomadStringView name,
    const NomadPath& catalog
) {
    const auto* value = object.if_contains(name);

    if (value == nullptr || !value->is_int64()) {
        raiseCatalogError(catalog, "field '" + NomadString(name) + "' must be an integer");
    }

    return value->as_int64();
}

NomadString readString(
    const boost::json::object& object,
    const NomadStringView name,
    const NomadPath& catalog
) {
    const auto* value = object.if_contains(name);

    if (value == nullptr || !value->is_string()) {
        raiseCatalogError(catalog, "field '" + NomadString(name) + "' must be a string");
    }

    return NomadString(value->as_string());
}

Rectangle readRectangle(
    const boost::json::object& object,
    const NomadStringView name,
    const NomadPath& catalog
) {
    const auto* value = object.if_contains(name);

    if (value == nullptr || !value->is_array() || value->as_array().size() != 4U) {
        raiseCatalogError(catalog, "field '" + NomadString(name) + "' must contain four integers");
    }

    const auto& values = value->as_array();

    if (!std::ranges::all_of(values, [](const auto& entry) { return entry.is_int64(); })) {
        raiseCatalogError(catalog, "field '" + NomadString(name) + "' must contain four integers");
    }

    return {
        values[0].as_int64(),
        values[1].as_int64(),
        values[2].as_int64(),
        values[3].as_int64()
    };
}

boost::json::array writeRectangle(const Rectangle& rectangle) {
    return {
        rectangle.getLeft(),
        rectangle.getTop(),
        rectangle.getWidth(),
        rectangle.getHeight()
    };
}

void validatePageFileName(const NomadString& fileName, const NomadPath& catalog) {
    const auto path = pathFromUtf8(fileName);

    if (
        fileName.empty() ||
        fileName.find_first_of("/\\:") != NomadString::npos ||
        path.has_parent_path() ||
        stringToLowerAscii(pathToUtf8(path.extension())) != ".png"
    ) {
        raiseCatalogError(catalog, "page file must be a PNG filename: '" + fileName + "'");
    }
}

SdlSurfacePtr loadPage(
    const NomadPath& catalog,
    const NomadPath& spriteRoot,
    const NomadString& fileName
) {
    validatePageFileName(fileName, catalog);
    const auto pagePath = spriteRoot / pathFromUtf8(fileName);
    std::error_code error;
    const auto status = std::filesystem::symlink_status(pagePath, error);

    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        raiseCatalogError(catalog, "page file is missing or invalid: '" + fileName + "'");
    }

    SdlSurfacePtr loaded(IMG_Load(pathToUtf8(pagePath).c_str()));

    if (!loaded) {
        raiseCatalogError(catalog, "failed to load page '" + fileName + "': " + SDL_GetError());
    }

    SdlSurfacePtr converted(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32));

    if (!converted) {
        raiseCatalogError(catalog, "failed to convert page '" + fileName + "': " + SDL_GetError());
    }

    return converted;
}

void validateSpriteRoot(
    const NomadPath& resourcePath,
    const NomadPath& configuredDirectory,
    const bool directoryRequired
) {
    const auto directory = (resourcePath / configuredDirectory).lexically_normal();
    const auto relativeDirectory = directory.lexically_relative(resourcePath);

    if (
        relativeDirectory.empty() ||
        relativeDirectory.is_absolute() ||
        *relativeDirectory.begin() == ".."
    ) {
        throw SpriteCatalogException(
            "Sprite directory must remain within the resource directory: '" +
            pathToUtf8(configuredDirectory) + "'"
        );
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(directory, error);

    if (
        (!error && !std::filesystem::exists(status)) ||
        error == std::errc::no_such_file_or_directory
    ) {
        if (!directoryRequired) {
            return;
        }

        throw SpriteCatalogException("Sprite directory does not exist: '" + pathToUtf8(directory) + "'");
    }
    if (error) {
        throw SpriteCatalogException(
            "Failed to inspect sprite directory '" + pathToUtf8(directory) + "': " + error.message()
        );
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) {
        throw SpriteCatalogException("Sprite directory is invalid: '" + pathToUtf8(directory) + "'");
    }
}

} // namespace

NomadString makePackedSpritePageFileName(
    const NomadString& group,
    const NomadIndex page
) {
    return group + "-" + toString(page) + ".png";
}

bool hasSpriteCatalog(const NomadPath& spriteRoot) {
    std::error_code error;
    const auto catalog = spriteRoot / NOMAD_SPRITE_CATALOG_FILE_NAME;
    const auto status = std::filesystem::symlink_status(catalog, error);

    if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status))) {
        return false;
    }
    if (error) {
        throw SpriteCatalogException(
            "Failed to inspect sprite catalog '" + pathToUtf8(catalog) + "': " + error.message()
        );
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        throw SpriteCatalogException("Sprite catalog is not a regular file: '" + pathToUtf8(catalog) + "'");
    }

    return true;
}

std::vector<PackedSpriteGroup> loadSpriteCatalog(const NomadPath& spriteRoot) {
    const auto catalog = spriteRoot / NOMAD_SPRITE_CATALOG_FILE_NAME;
    std::ifstream input(catalog, std::ios::binary);

    if (!input) {
        throw SpriteCatalogException("Failed to open sprite catalog '" + pathToUtf8(catalog) + "'");
    }

    const NomadString text{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };

    if (input.bad()) {
        throw SpriteCatalogException("Failed to read sprite catalog '" + pathToUtf8(catalog) + "'");
    }

    boost::system::error_code parseError;
    const auto value = boost::json::parse(text, parseError);

    if (parseError || !value.is_object()) {
        raiseCatalogError(catalog, "expected a JSON object");
    }

    const auto& object = value.as_object();

    if (readInteger(object, "schema", catalog) != NOMAD_SPRITE_CATALOG_SCHEMA_VERSION) {
        raiseCatalogError(catalog, "unsupported schema");
    }

    const auto* groupsValue = object.if_contains("groups");

    if (groupsValue == nullptr || !groupsValue->is_array()) {
        raiseCatalogError(catalog, "field 'groups' must be an array");
    }

    std::set<NomadString> groupNames;
    std::set<NomadString> spriteNames;
    std::set<NomadString> pageFiles;
    std::vector<PackedSpriteGroup> groups;
    groups.reserve(groupsValue->as_array().size());

    for (const auto& groupValue : groupsValue->as_array()) {
        if (!groupValue.is_object()) {
            raiseCatalogError(catalog, "group entries must be objects");
        }

        const auto& groupObject = groupValue.as_object();
        PackedSpriteGroup group;
        group.name = readString(groupObject, "name", catalog);

        if (
            group.name.empty() ||
            !groupNames.insert(stringToLowerAscii(group.name)).second
        ) {
            raiseCatalogError(catalog, "group names must be non-empty and unique");
        }

        const auto* pagesValue = groupObject.if_contains("pages");
        const auto* spritesValue = groupObject.if_contains("sprites");

        if (pagesValue == nullptr || !pagesValue->is_array() || pagesValue->as_array().empty()) {
            raiseCatalogError(catalog, "group pages must be a non-empty array");
        }
        if (spritesValue == nullptr || !spritesValue->is_array() || spritesValue->as_array().empty()) {
            raiseCatalogError(catalog, "group sprites must be a non-empty array");
        }

        group.pages.reserve(pagesValue->as_array().size());

        for (NomadIndex pageIndex = 0; pageIndex < pagesValue->as_array().size(); ++pageIndex) {
            const auto& pageValue = pagesValue->as_array()[pageIndex];

            if (!pageValue.is_object()) {
                raiseCatalogError(catalog, "page entries must be objects");
            }

            const auto fileName = readString(pageValue.as_object(), "file", catalog);
            const auto expectedFileName = makePackedSpritePageFileName(group.name, pageIndex);

            if (!pageFiles.insert(stringToLowerAscii(fileName)).second) {
                raiseCatalogError(catalog, "page filenames must be unique");
            }
            if (fileName != expectedFileName) {
                raiseCatalogError(
                    catalog,
                    "page file for group '" + group.name + "' must be named '" +
                    expectedFileName + "'"
                );
            }

            auto surface = loadPage(catalog, spriteRoot, fileName);
            group.pages.push_back({surface->w, surface->h, std::move(surface)});
        }

        group.sprites.reserve(spritesValue->as_array().size());

        for (const auto& spriteValue : spritesValue->as_array()) {
            if (!spriteValue.is_object()) {
                raiseCatalogError(catalog, "sprite entries must be objects");
            }

            const auto& spriteObject = spriteValue.as_object();
            auto spriteName = readString(spriteObject, "name", catalog);
            const auto page = readInteger(spriteObject, "page", catalog);

            if (
                spriteName.empty() ||
                !spriteNames.insert(stringToLowerAscii(spriteName)).second
            ) {
                raiseCatalogError(catalog, "sprite names must be non-empty and unique");
            }
            if (page < 0 || static_cast<NomadIndex>(page) >= group.pages.size()) {
                raiseCatalogError(catalog, "sprite '" + spriteName + "' references an invalid page");
            }

            auto sprite = PackedSpriteDefinition{
                std::move(spriteName),
                static_cast<NomadIndex>(page),
                readRectangle(spriteObject, "source", catalog),
                readRectangle(spriteObject, "frame", catalog)
            };
            const auto& spritePage = group.pages[sprite.page];

            if (
                sprite.source.getLeft() < 0 ||
                sprite.source.getTop() < 0 ||
                sprite.source.getWidth() <= 0 ||
                sprite.source.getHeight() <= 0 ||
                sprite.frame.getLeft() < 0 ||
                sprite.frame.getTop() < 0 ||
                sprite.frame.getWidth() <= 0 ||
                sprite.frame.getHeight() <= 0 ||
                sprite.source.getLeft() + sprite.frame.getWidth() > sprite.source.getWidth() ||
                sprite.source.getTop() + sprite.frame.getHeight() > sprite.source.getHeight() ||
                sprite.frame.getRight() > spritePage.width ||
                sprite.frame.getBottom() > spritePage.height
            ) {
                raiseCatalogError(catalog, "sprite '" + sprite.name + "' has invalid rectangles");
            }

            group.sprites.push_back(std::move(sprite));
        }

        groups.push_back(std::move(group));
    }

    return groups;
}

void writeSpriteCatalog(
    const NomadPath& spriteRoot,
    const std::vector<PackedSpriteGroup>& groups
) {
    std::error_code error;
    std::filesystem::create_directories(spriteRoot, error);

    if (error) {
        throw SpriteCatalogException(
            "Failed to create compiled sprite directory '" +
            pathToUtf8(spriteRoot) + "': " + error.message()
        );
    }

    const auto catalog = spriteRoot / NOMAD_SPRITE_CATALOG_FILE_NAME;
    boost::json::array groupValues;

    for (const auto& group : groups) {
        boost::json::array pageValues;

        for (NomadIndex pageIndex = 0; pageIndex < group.pages.size(); ++pageIndex) {
            const auto fileName = makePackedSpritePageFileName(group.name, pageIndex);
            validatePageFileName(fileName, catalog);
            const auto pagePath = spriteRoot / pathFromUtf8(fileName);

            if (!IMG_SavePNG(group.pages[pageIndex].surface.get(), pathToUtf8(pagePath).c_str())) {
                throw SpriteCatalogException(
                    "Failed to write packed sprite page '" +
                    pathToUtf8(pagePath) + "': " + SDL_GetError()
                );
            }

            pageValues.emplace_back(boost::json::object{{"file", fileName}});
        }

        boost::json::array spriteValues;

        for (const auto& sprite : group.sprites) {
            spriteValues.emplace_back(boost::json::object{
                {"name", sprite.name},
                {"page", sprite.page},
                {"source", writeRectangle(sprite.source)},
                {"frame", writeRectangle(sprite.frame)}
            });
        }

        groupValues.emplace_back(boost::json::object{
            {"name", group.name},
            {"pages", std::move(pageValues)},
            {"sprites", std::move(spriteValues)}
        });
    }

    std::ofstream output(catalog, std::ios::binary | std::ios::trunc);

    if (!output) {
        throw SpriteCatalogException("Failed to create sprite catalog '" + pathToUtf8(catalog) + "'");
    }

    output << boost::json::serialize(boost::json::object{
        {"schema", NOMAD_SPRITE_CATALOG_SCHEMA_VERSION},
        {"groups", std::move(groupValues)}
    });

    if (!output) {
        throw SpriteCatalogException("Failed to write sprite catalog '" + pathToUtf8(catalog) + "'");
    }
}

std::vector<PackedSpriteGroup> loadSpriteRoot(
    const NomadPath& resourcePath,
    const NomadPath& directory,
    const bool directoryRequired,
    const NomadInteger maxTextureSize,
    const NomadInteger padding
) {
    validateSpriteRoot(resourcePath, directory, directoryRequired);
    const auto spriteRoot = (resourcePath / directory).lexically_normal();

    if (!std::filesystem::exists(spriteRoot)) {
        return {};
    }

    if (hasSpriteCatalog(spriteRoot)) {
        return loadSpriteCatalog(spriteRoot);
    }

    const auto sources = discoverSpriteSources(resourcePath, {directory}, directoryRequired);
    return packSpriteGroups(sources, maxTextureSize, padding);
}

} // namespace nomad
