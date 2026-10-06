// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/debug/DebugConsole.hpp>

#include <nomad/game/VariablePersistence.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VariableContext.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <cmath>

namespace nomad {

bool Game::saveGame(const NomadString& saveName) {
    const auto path = makeSavePath(saveName);

    if (path.empty()) {
        return false;
    }

    boost::json::object root;

    if (!saveVariableContext(m_runtime.get(), *m_inventoryContext, root)) {
        log::error("Game not saved to '" + path + "'");
        return false;
    }

    if (!writeJsonFile(path, root)) {
        return false;
    }

    log::info("Game saved to '" + path + "'");

    return true;
}

bool Game::loadGame(const NomadString& saveName) {
    const auto path = makeSavePath(saveName);

    if (path.empty()) {
        return false;
    }

    if (!pathExists(path)) {
        log::error("Save '" + saveName + "' not found at '" + path + "'");
        return false;
    }

    const auto root = readJsonFile(path);

    if (!root) {
        return false;
    }

    loadVariableContext(m_runtime.get(), *m_inventoryContext, *root);

    log::info("Game loaded from '" + path + "'");

    return true;
}

bool Game::saveExists(const NomadString& saveName) const {
    const auto path = makeSavePath(saveName);

    return !path.empty() && pathExists(path);
}

bool Game::saveSettings() {
    const auto path = getSettingsFilePath();

    if (path.empty()) {
        return false;
    }

    boost::json::object root;

    if (!saveVariableContext(m_runtime.get(), *m_settingsContext, root)) {
        log::error("Settings not saved to '" + path + "'");
        return false;
    }

    if (!writeJsonFile(path, root)) {
        return false;
    }

    log::info("Settings saved to '" + path + "'");

    return true;
}

bool Game::loadSettings() {
    const auto path = getSettingsFilePath();

    if (path.empty()) {
        return false;
    }

    if (!pathExists(path)) {
        log::info("No settings found at '" + path + "'. Using defaults");
        return false;
    }

    const auto root = readJsonFile(path);

    if (!root) {
        return false;
    }

    loadVariableContext(m_runtime.get(), *m_settingsContext, *root);

    log::info("Settings loaded from '" + path + "'");

    return true;
}

void Game::setDebugConsoleVisible(const bool visible) {
    ensureDebugSettingsLoaded();

    const auto changed = visible != m_debugConsoleVisible;

    applyDebugConsoleVisible(visible);

    if (changed) {
        saveDebugSettings();
    }
}

bool Game::isDebugConsoleVisible() const {
    return m_debugConsoleVisible;
}

void Game::setDebugConsoleScale(const NomadFloat scale) {
    if (!std::isfinite(scale)) {
        log::warning("Ignoring invalid debug console scale: " + std::to_string(scale));
        return;
    }

    ensureDebugSettingsLoaded();

    const auto clampedScale = std::clamp(scale, DEBUG_CONSOLE_MINIMUM_SCALE, DEBUG_CONSOLE_MAXIMUM_SCALE);
    const auto changed = clampedScale != m_debugConsoleScale;

    applyDebugConsoleScale(clampedScale);

    if (changed) {
        saveDebugSettings();
    }
}

NomadFloat Game::getDebugConsoleScale() const {
    return m_debugConsoleScale;
}

NomadString Game::getSettingsFilePath() const {
    const auto statePath = getStatePath();

    if (statePath.empty()) {
        return NOMAD_EMPTY_STRING;
    }

    return statePath + "settings.json";
}

NomadString Game::getDebugSettingsFilePath() const {
    const auto statePath = getStatePath();

    if (statePath.empty()) {
        return NOMAD_EMPTY_STRING;
    }

    return statePath + "debug.json";
}

void Game::ensureDebugSettingsLoaded() {
    // The location of debug.json depends on game.organization and game.name.
    if (m_debugSettingsLoaded || m_organization.empty() || m_name.empty()) {
        return;
    }

    m_debugSettingsLoaded = true;

    loadDebugSettings();
}

void Game::loadDebugSettings() {
    const auto path = getDebugSettingsFilePath();

    if (path.empty()) {
        return;
    }

    if (!pathExists(path)) {
        log::debug("No debug settings found at '" + path + "'");
        return;
    }

    const auto root = readJsonFile(path);

    if (!root) {
        return;
    }

    RuntimeValue value;

    if (readJsonVariable(m_runtime.get(), *root, DEBUG_CONSOLE_VISIBLE_VARIABLE, m_runtime->getBooleanType(), value)) {
        applyDebugConsoleVisible(value.getBooleanValue());
    }

    if (readJsonVariable(m_runtime.get(), *root, DEBUG_CONSOLE_SCALE_VARIABLE, m_runtime->getFloatType(), value)) {
        const auto scale = value.getFloatValue();
        applyDebugConsoleScale(std::clamp(scale, DEBUG_CONSOLE_MINIMUM_SCALE, DEBUG_CONSOLE_MAXIMUM_SCALE));
    }
}

void Game::saveDebugSettings() const {
    if (m_organization.empty() || m_name.empty()) {
        log::debug("Debug settings not saved: game.organization and game.name are not set");
        return;
    }

    boost::json::object root;

    const auto written =
        writeJsonVariable(
            m_runtime.get(),
            root,
            DEBUG_CONSOLE_VISIBLE_VARIABLE,
            m_runtime->getBooleanType(),
            RuntimeValue(m_debugConsoleVisible)
        ) &&
        writeJsonVariable(
            m_runtime.get(),
            root,
            DEBUG_CONSOLE_SCALE_VARIABLE,
            m_runtime->getFloatType(),
            RuntimeValue(m_debugConsoleScale)
        );

    if (!written) {
        return;
    }

    const auto path = getDebugSettingsFilePath();

    if (!path.empty() && writeJsonFile(path, root)) {
        log::debug("Debug settings saved to '" + path + "'");
    }
}

void Game::applyDebugConsoleVisible(const bool visible) {
    m_debugConsoleVisible = visible;

    if (m_debugConsole == nullptr) {
        return;
    }

    if (visible) {
        m_debugConsole->activate();
    } else {
        m_debugConsole->deactivate();
    }
}

void Game::applyDebugConsoleScale(const NomadFloat scale) {
    m_debugConsoleScale = scale;

    if (m_debugConsole != nullptr) {
        m_debugConsole->setUiScale(scale);
    }
}

} // namespace nomad
