// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/ActionManager.hpp>

#include <nomad/game/Game.hpp>
#include <nomad/game/Scene.hpp>

#include <boost/json.hpp>

#include <fstream>

namespace nomad {

ActionManager::ActionManager() {
}

void ActionManager::clearMapping() {
    m_mappings.clear();
}

NomadId ActionManager::registerAction(const NomadString& name, InputCode code1, InputCode code2) {
    auto mapping = getActionMapping(name);

    if (mapping) {
        mapping->code1 = code1;
        mapping->code2 = code2;

        return mapping->id;
    }

    auto mapping_id = toNomadId(m_mappings.size());

    m_mappings.emplace_back(ActionMapping{
        mapping_id,
        name,
        code1,
        code2,
    });

    return mapping_id;
}

bool ActionManager::getActionNameForInput(InputCode code, NomadString& action_name) const {
    for (auto& mapping : m_mappings) {
        if (mapping.code1 == code || mapping.code2 == code) {
            action_name = mapping.name;
            return true;
        }
    }

    return false;
}

void ActionManager::loadMapping(Game* game, const NomadString& mappingName) {
    clearMapping();

    // Load defaults first
    auto default_mapping_file = createDefaultMappingFileName(game, mappingName);

    loadMappingFromFile(game, default_mapping_file);

    // Load custom if it exists
    auto custom_mapping_file = createCustomMappingFileName(game, mappingName);
    if (game->fileExists(custom_mapping_file)) {
        loadMappingFromFile(game, custom_mapping_file);
    }
}

void ActionManager::saveMapping(Game* game, const NomadString& mappingName) {
    auto custom_mapping_file = createCustomMappingFileName(game, mappingName);

    saveMappingToFile(game, custom_mapping_file);
}

void ActionManager::resetMappingToDefaults(Game* game, const NomadString& mappingName) {
    clearMapping();

    auto custom_mapping_file_name = createCustomMappingFileName(game, mappingName);

    game->deleteFile(custom_mapping_file_name);

    loadMapping(game, mappingName);
}

NomadString ActionManager::createMappingFileName(const NomadString& mappingName) const {
    return mappingName + ".input.json";
}

NomadString ActionManager::createDefaultMappingFileName(Game* game, const NomadString& mappingName) const {
    auto file_name = createMappingFileName(mappingName);

    return game->makeResourcePath(mappingName + ".input.json");
}

NomadString ActionManager::createCustomMappingFileName(Game* game, const NomadString& mappingName) const {
    auto file_name = createMappingFileName(mappingName);

    return game->makeSettingsPath(mappingName + ".input.json");
}

void ActionManager::loadMappingFromFile(Game* /*game*/, const NomadString& fileName) {
    std::ifstream file(fileName);

    if (!file) {
        log::error("Failed to open mapping file for reading: " + fileName);
        return;
    }

    NomadString content(std::istreambuf_iterator<char>(file), {});

    auto json = boost::json::parse(content);

    for (const auto& [key, value] : json.as_object()) {
        NomadString action_name = key;

        auto code1 = getInputCode(value.at("code1").as_string().c_str());
        auto code2 = NOMAD_INVALID_ID;

        if (value.as_object().contains("code2")) {
            code2 = getInputCode(value.at("code2").as_string().c_str());
        }

        registerAction(action_name, code1, code2);
    }
}

void ActionManager::saveMappingToFile(Game* /*game*/, const NomadString& fileName) const {
    std::ofstream file(fileName);

    if (!file) {
        log::error("Failed to open mapping file for writing: " + fileName);
        return;
    }

    boost::json::object json;

    for (const auto& mapping: m_mappings) {
        json[mapping.name] = {
            { "code1", getInputCodeName(mapping.code1) },
            { "code2", getInputCodeName(mapping.code2) },
        };
    }

    file << boost::json::serialize(json);
}

ActionManager::ActionMapping* ActionManager::getActionMapping(const NomadString& name) {
    for (auto& mapping : m_mappings) {
        if (mapping.name == name) {
            return &mapping;
        }
    }

    return nullptr;
}

} // nomad
