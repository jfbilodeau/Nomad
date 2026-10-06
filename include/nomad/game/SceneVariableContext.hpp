// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/VariableContext.hpp>

namespace nomad {

// Forward declarations
class Game;

class SceneVariableContext : public VariableContext {
public:
    explicit SceneVariableContext(Game* game);
    SceneVariableContext(const SceneVariableContext&) = delete;
    ~SceneVariableContext() override = default;

    void setValue(NomadId variable_id, const RuntimeValue& value) override;
    void getValue(NomadId variable_id, RuntimeValue& value) override;

private:
    Game* m_game;
};

} // nomad
