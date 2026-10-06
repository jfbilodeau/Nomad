// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/game/InputManager.hpp>

#include <functional>

namespace nomad {

// Forward declarations

class ActionManager {
public:
    explicit ActionManager();
    ActionManager(const ActionManager&) = delete;
    ~ActionManager() = default;

    void clearMapping();

    NomadId registerAction(const NomadString& name, InputCode code1, InputCode code2);

    bool getActionNameForInput(InputCode code, NomadString& action_name) const;

    void loadMapping(Game* game, const NomadString& mappingName);
    void saveMapping(Game* game, const NomadString& mappingName);
    void resetMappingToDefaults(Game* game, const NomadString& mappingName);

private:
    struct ActionMapping {
        NomadId id;
        NomadString name;
        InputCode code1;
        InputCode code2;
    };

    [[nodiscard]] NomadString createMappingFileName(const NomadString& mappingName) const;
    [[nodiscard]] NomadString createDefaultMappingFileName(Game* game, const NomadString& mappingName) const;
    [[nodiscard]] NomadString createCustomMappingFileName(Game* game, const NomadString& mappingName) const;
    void loadMappingFromFile(Game* game, const NomadString& fileName);
    void saveMappingToFile(Game* game, const NomadString& fileName) const;

    ActionMapping* getActionMapping(const NomadString& name);

    std::vector<ActionMapping> m_mappings;
};

} // nomad
