// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/GameResourceCheck.hpp>

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/game/resource/SpritePacker.hpp>

#include <iomanip>
#include <sstream>

namespace nomad {

NomadString checkGameResources(const ModuleContext& context) {
    const auto options = readGameOptions(*context.configuration);
    const auto& sprites = options.sprites;
    if (sprites.directories && sprites.directories->empty()) {
        return "Sprites: disabled\n";
    }

    const auto directories = sprites.directories.value_or(std::vector<NomadPath>{"sprites"});
    const auto report = analyzeSpritePacking(
        context.resourceDirectory,
        directories,
        sprites.directories.has_value(),
        sprites.maxTextureSize.value_or(2048),
        sprites.padding.value_or(1),
        !context.configuration->contains("project.executable")
    );
    std::ostringstream output;
    output
        << "Sprites: " << report.sourceCount
        << " logical sprites, " << report.groupCount
        << " groups, " << report.pageCount
        << " texture pages\n";

    for (const auto& page : report.pages) {
        const auto totalPixelCount = page.width * page.height;
        const auto occupancy = totalPixelCount == 0
            ? 0.0
            : static_cast<double>(page.usedPixelCount) * 100.0 / static_cast<double>(totalPixelCount);
        output
            << "  " << page.name << ": "
            << page.width << 'x' << page.height << ", "
            << page.spriteCount << " sprites ("
            << page.uniqueSpriteCount << " unique), "
            << std::fixed << std::setprecision(1) << occupancy << "% occupancy\n";
    }
    return output.str();
}

} // namespace nomad
