// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/GamePackaging.hpp>

#include <nomad/game/resource/SpriteCatalog.hpp>
#include <nomad/game/resource/SpriteDiscovery.hpp>
#include <nomad/game/resource/SpritePacker.hpp>

#include <nomad/system/ModuleManifest.hpp>
#include <nomad/system/NomadConfiguration.hpp>
#include <nomad/system/Path.hpp>

#include <exception>
#include <vector>

namespace nomad {

namespace {

void requireIncluded(ModulePackageContext& context, const NomadPath& relative) {
    if (context.isResourceExcluded(relative)) {
        throw ModuleException(
            "Package exclusions must not remove compiled sprite file: '" +
            pathToUtf8(relative) + "'"
        );
    }
}

} // namespace

void packageGameResources(ModulePackageContext* context) {
    if (context == nullptr || context->source == nullptr || context->release == nullptr ||
        (!context->dryRun && context->stagingDirectory.empty()) ||
        !context->recordFile || !context->excludeResource || !context->isResourceExcluded) {
        throw ModuleException("Game packaging requires a complete module package context");
    }

    const auto configuredDirectories = context->source->getStringArray("sprites.directories");
    const auto directories = configuredDirectories.value_or(std::vector<NomadString>{"sprites"});
    const auto maxTextureSize = context->source->getInteger("sprites.max-texture-size").value_or(2048);
    const auto padding = context->source->getInteger("sprites.padding").value_or(1);
    if (maxTextureSize < 1 || padding < 0) {
        throw ModuleException("Sprite maximum texture size must be positive and padding must not be negative");
    }

    try {
        for (const auto& directory : directories) {
            const auto configuredDirectory = pathFromUtf8(directory);
            const auto sourceDirectory = (context->resourceDirectory / configuredDirectory).lexically_normal();
            const auto relativeRoot = sourceDirectory.lexically_relative(context->resourceDirectory);
            if (configuredDirectory.empty() || configuredDirectory.is_absolute() ||
                relativeRoot.empty() || relativeRoot.is_absolute() || *relativeRoot.begin() == "..") {
                throw ModuleException("Sprite directory must remain within the resource directory: '" + directory + "'");
            }

            if (hasSpriteCatalog(sourceDirectory)) {
                const auto groups = loadSpriteCatalog(sourceDirectory);
                requireIncluded(*context, relativeRoot / NOMAD_SPRITE_CATALOG_FILE_NAME);
                for (const auto& group : groups) {
                    for (auto pageIndex = NomadIndex{0}; pageIndex < group.pages.size(); ++pageIndex) {
                        requireIncluded(
                            *context,
                            relativeRoot / pathFromUtf8(makePackedSpritePageFileName(group.name, pageIndex))
                        );
                    }
                }
                continue;
            }

            const auto sources = discoverSpriteSources(
                context->resourceDirectory, {configuredDirectory}, configuredDirectories.has_value()
            );
            if (sources.empty()) {
                continue;
            }
            const auto groups = packSpriteGroups(sources, maxTextureSize, padding);
            for (const auto& source : sources) {
                context->excludeResource(source.filePath.lexically_relative(context->resourceDirectory));
            }

            const auto destinationRoot = NomadPath("res") / relativeRoot;
            context->recordFile(destinationRoot / NOMAD_SPRITE_CATALOG_FILE_NAME);
            for (const auto& group : groups) {
                for (auto pageIndex = NomadIndex{0}; pageIndex < group.pages.size(); ++pageIndex) {
                    context->recordFile(
                        destinationRoot / pathFromUtf8(makePackedSpritePageFileName(group.name, pageIndex))
                    );
                }
            }
            if (!context->dryRun) {
                writeSpriteCatalog(context->stagingDirectory / destinationRoot, groups);
            }
        }
    } catch (const std::exception& error) {
        throw ModuleException("Failed to compile sprite resources: " + NomadString(error.what()));
    }
}

} // namespace nomad
