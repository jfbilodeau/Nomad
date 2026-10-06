// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/debug/DebugConsole.hpp>

#include <nomad/game/Alignment.hpp>
#include <nomad/game/Canvas.hpp>
#include <nomad/game/EntityVariableContext.hpp>
#include <nomad/game/Scene.hpp>
#include <nomad/game/VariablePersistence.hpp>

#include <nomad/resource/ResourceManager.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/Runtime.hpp>

#include <nomad/system/TempHeap.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <variant>

namespace nomad {

Game::Game(const GameOptions* options) :
    m_options(*options)
{}

void Game::initialize()
{
    initSdl();
    initSdlTtf();
    initResourcePath();
    initRuntime();
    initWindowCallbacks();
    initEvents();
    initDynamicVariables();
    initVariableContext();
    initFunctions();
    initResourceManager();
    initText();
    initConstants();

    setLanguage("en");

    compileFunctions();

    if (m_options.debug) {
        log::setLogLevel(LogLevel::Debug);

        std::ofstream instructionDump("instructions.txt");
        m_runtime->dumpInstructions(instructionDump);

        std::ofstream documentationDump("documentation.md");
        // m_runtime->dumpDocumentation(documentationDump);
        generateDocumentation(m_runtime.get(), documentationDump);
    }

    runInitFunction();

    // Debug console must be initialized after the init function has been run
    initDebugConsole();

    log::info("Game initialized");

    log::flush();
}

Game::~Game() {
    m_debugConsole.reset();
    m_scenes.clear();
    m_resourceManager.reset();
    m_canvas.reset();

    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        if (m_renderer) {
            SDL_DestroyRenderer(m_renderer);
        }

        if (m_window) {
            SDL_DestroyWindow(m_window);
        }
    }

    // Always quit: besides shutting down subsystems, SDL_Quit() releases SDL's thread-local storage
    // (including the error message buffer), which is allocated even when no subsystem was initialized.
    SDL_Quit();

    m_onWindowClose.reset();
    m_onWindowGainFocus.reset();
    m_onWindowLoseFocus.reset();
    m_onWindowMaximize.reset();
    m_onWindowMinimize.reset();
    m_onWindowMove.reset();
    m_onWindowResize.reset();
    m_onWindowRestore.reset();
    m_runtime.reset();
}

Runtime* Game::getRuntime() const {
    return m_runtime.get();
}

ResourceManager* Game::getResources() const {
    return m_resourceManager.get();
}

void Game::setDebug(const bool debug) {
    m_options.debug = debug;
}

bool Game::isDebug() const {
    return m_options.debug;
}

const VariableMap* Game::getSceneVariables() const {
    return m_sceneVariableMap;
}

const VariableMap* Game::getEntityVariables() const {
    return m_thisEntityVariableMap;
}

void Game::setWindowSize(const NomadInteger width, const NomadInteger height) {
    m_windowSize.set(width, height);

    SDL_SetWindowSize(
        m_window,
        static_cast<int>(m_windowSize.getX()),
        static_cast<int>(m_windowSize.getY())
    );
}

const Point& Game::getWindowSize() const {
    return m_windowSize;
}

void Game::centerWindow() const {
    if (m_window) {
        SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

void Game::setResolution(const NomadInteger width, const NomadInteger height) {
    m_resolution.set(width, height);

    const auto result = SDL_SetRenderLogicalPresentation(
        m_renderer,
        static_cast<int>(m_resolution.getX()),
        static_cast<int>(m_resolution.getY()),
        SDL_LOGICAL_PRESENTATION_LETTERBOX
    );

    if (result == false) {
        raiseError("Failed to set logical size: " + NomadString(SDL_GetError()));
    }
}

const Point& Game::getResolution() const {
    return m_resolution;
}

void Game::setFps(NomadIndex fps) {
    m_fps = fps;
}

NomadIndex Game::getFps() const {
    return m_fps;
}

const NomadString& Game::getOrganization() const {
    return m_organization;
}

void Game::setOrganization(const NomadString& organization) {
    m_organization = organization;
}

const NomadString& Game::getName() const {
    return m_name;
}
void Game::setName(const NomadString& name) {
    m_name = name;
}

NomadString Game::getResourcePath() const {
    return m_options.resourcePath;
}

NomadString Game::getStatePath() const {
    if (m_organization.empty()) {
        log::error("game.organization not set");
        return NOMAD_EMPTY_STRING;
    }

    if (m_name.empty()) {
        log::error("game.name not set");
        return NOMAD_EMPTY_STRING;
    }

    const auto pref_path = SDL_GetPrefPath(m_organization.c_str(), m_name.c_str());

    if (pref_path == nullptr) {
        log::error("Failed to get state path: " + NomadString(SDL_GetError()));

        return NOMAD_EMPTY_STRING;
    }

    const NomadString state_path = pref_path;

    SDL_free(pref_path);

    return state_path;
}

NomadString Game::getSavePath() const {
    const auto statePath = getStatePath();

    if (statePath.empty()) {
        return NOMAD_EMPTY_STRING;
    }

    // SDL_GetPrefPath guarantees a trailing platform separator.
    const auto path = statePath + "save" + statePath.back();

    if (!SDL_CreateDirectory(path.c_str())) {
        log::error("Failed to create save directory '" + path + "': " + SDL_GetError());
        return NOMAD_EMPTY_STRING;
    }

    return path;
}

NomadString Game::getSettingsPath() const {
    const auto path = getStatePath() + "settings";

    return path;
}

NomadString Game::makeResourcePath(const NomadString& resourceName) const {
    const auto file_name = m_options.resourcePath + resourceName;

    return file_name;
}

NomadString Game::makeStatePath(const NomadString& stateName) const {
    const auto file_name = getStatePath() + stateName;

    createPathToFile(file_name);

    return file_name;
}

NomadString Game::makeSavePath(const NomadString& saveName) const {
    const auto saveFileName = makeSaveFileName(saveName);

    if (!saveFileName) {
        log::error("Invalid save name '" + saveName + "'");
        return NOMAD_EMPTY_STRING;
    }

    const auto savePath = getSavePath();

    if (savePath.empty()) {
        return NOMAD_EMPTY_STRING;
    }

    return savePath + *saveFileName;
}

NomadString Game::makeSettingsPath(const NomadString& settingsName) const {
    const auto fileName = getSettingsPath() + settingsName;

    createPathToFile(fileName);

    return fileName;
}

bool Game::fileExists(const NomadString& fileName) const {
    return std::filesystem::exists(fileName);
}

void Game::deleteFile(const NomadString& fileName) const {
    std::filesystem::remove(fileName);
}

bool Game::running() const {
    return m_running;
}

bool Game::isPaused() const {
    return m_paused;
}

void Game::pause() {
    m_paused = true;
}

void Game::resume() {
    m_paused = false;
}

NomadInteger Game::getTicks() const {
    return static_cast<NomadInteger>(SDL_GetTicks());
}

void Game::setClearColor(const Color &color) {
    m_clearColor = color;
}

const Color& Game::getClearColor() const {
    return m_clearColor;
}

GameExecutionContext* Game::getCurrentContext() {
    return &m_currentContext;
}

const GameExecutionContext * Game::getCurrentContext() const {
    return &m_currentContext;
}

void Game::run() {
    m_running = true;

    // Fixed timestep in milliseconds per update
    const double fixedDeltaMs = m_fps > 0 ? (1000.0 / static_cast<double>(m_fps)) : (1000.0 / 60.0);

    double accumulator = 0.0;
    auto previous = getTicks();

    while (m_running) {
        FrameProfile frameProfile;
        const auto inputStartTime = getTicks();

        SDL_Event event{};

        while (SDL_PollEvent(&event) != 0) {
            if (m_debugConsole != nullptr) {
                m_debugConsole->processEvent(event);
            }

            switch (event.type) {
                case SDL_EVENT_WINDOW_RESIZED:
                    if (event.window.windowID == SDL_GetWindowID(m_window)) {
                        m_windowSize.set(event.window.data1, event.window.data2);
                        executeWindowCallback(
                            m_onWindowResize,
                            {
                                RuntimeValue(static_cast<NomadInteger>(event.window.data1)),
                                RuntimeValue(static_cast<NomadInteger>(event.window.data2))
                            }
                        );
                    }
                    break;

                case SDL_EVENT_WINDOW_MOVED:
                    if (event.window.windowID == SDL_GetWindowID(m_window)) {
                        executeWindowCallback(
                            m_onWindowMove,
                            {
                                RuntimeValue(static_cast<NomadInteger>(event.window.data1)),
                                RuntimeValue(static_cast<NomadInteger>(event.window.data2))
                            }
                        );
                    }
                    break;

                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    if (event.window.windowID == SDL_GetWindowID(m_window)) {
                        executeWindowCallback(m_onWindowGainFocus);
                    }
                    break;

                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    if (event.window.windowID == SDL_GetWindowID(m_window)) {
                        executeWindowCallback(m_onWindowLoseFocus);
                    }
                    break;

                case SDL_EVENT_WINDOW_MAXIMIZED:
                    if (event.window.windowID == SDL_GetWindowID(m_window)) {
                        executeWindowCallback(m_onWindowMaximize);
                    }
                    break;

                case SDL_EVENT_WINDOW_MINIMIZED:
                    if (event.window.windowID == SDL_GetWindowID(m_window)) {
                        executeWindowCallback(m_onWindowMinimize);
                    }
                    break;

                case SDL_EVENT_WINDOW_RESTORED:
                    if (event.window.windowID == SDL_GetWindowID(m_window)) {
                        executeWindowCallback(m_onWindowRestore);
                    }
                    break;

                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if (event.window.windowID == SDL_GetWindowID(m_window)) {
                        executeWindowCallback(m_onWindowClose);
                    }
                    break;

                case SDL_EVENT_WINDOW_DESTROYED:
                    if (event.window.windowID == SDL_GetWindowID(m_window)) {
                        m_running = false;
                    }
                    break;

                case SDL_EVENT_QUIT:
                    m_running = false;
                    break;

                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP:
                    processInput(event.key);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    processInput(event.button);
                    break;

                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    processInput(event.button);
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    if (m_debugConsole == nullptr || !m_debugConsole->wantsMouse()) {
                        m_mousePosition.set(event.motion.x, event.motion.y);
                    }
                    break;

                default:
                    break;
            }
        }

        const auto inputEndTime = getTicks();
        frameProfile.inputDuration = inputEndTime - inputStartTime;

        // Time management for fixed timestep
        const auto current = getTicks();
        auto frameTime = static_cast<double>(current - previous);
        frameProfile.frameDuration = static_cast<NomadInteger>(frameTime);
        frameProfile.frameNumber = m_currentFrame;
        previous = current;

        // Prevent "spiral of death" after long pauses
        if (frameTime > 250.0) {
            frameTime = 250.0;
        }

        if (m_paused) {
            accumulator = 0.0;
        } else {
            accumulator += frameTime;
        }

        const auto stepRequested =
            m_debugConsole != nullptr &&
            m_debugConsole->isActive() &&
            m_debugConsole->consumeStepRequest();
        if (stepRequested) {
            accumulator = fixedDeltaMs;
        }

        // Run as many fixed updates as needed
        NomadInteger totalUpdateDuration = 0;
        bool didUpdate = false;

        while (accumulator >= fixedDeltaMs && m_running && (!m_paused || stepRequested)) {
            const auto updateStartTime = getTicks();

            update();

            // Clear pressed/released state
            for (auto& mouseButtonState: m_mouseButtonState) {
                mouseButtonState.pressed = false;
                mouseButtonState.released = false;
            }

            const auto updateEndTime = getTicks();
            totalUpdateDuration += (updateEndTime - updateStartTime);

            accumulator -= fixedDeltaMs;
            didUpdate = true;

            if (stepRequested) {
                break;
            }
        }

        frameProfile.updateDuration = totalUpdateDuration;

        const auto renderStartTime = getTicks();

        const auto renderingToDebugConsole =
            m_debugConsole != nullptr &&
            m_debugConsole->isActive() &&
            m_debugConsole->beginGameRender();

        // Rendering uses the latest game state (optionally could be interpolated using accumulator / fixedDeltaMs)
        render(m_canvas.get());

        const auto renderEndTime = getTicks();
        frameProfile.renderDuration = renderEndTime - renderStartTime;

        if (didUpdate) {
            m_mouseLastPosition = m_mousePosition;
        }

        // Capture the frame's high-water mark before the heap is reset below.
        frameProfile.tempHeapSize = static_cast<NomadInteger>(getTempHeapSize());
        frameProfile.maxHeapSize = static_cast<NomadInteger>(getTempHeapMaxSize());

        frameProfile.totalDuration = frameProfile.inputDuration + frameProfile.updateDuration + frameProfile.renderDuration;

        if (m_debugConsole != nullptr) {
            m_debugConsole->pushFrameProfile(frameProfile);
        }

        if (renderingToDebugConsole) {
            m_debugConsole->endGameRender();
        }
        if (m_debugConsole != nullptr && m_debugConsole->isActive()) {
            m_debugConsole->render();
        }

        m_canvas->present();

        // Simple frame limiting to avoid busy spin if we're ahead of schedule
        const auto frameElapsed = getTicks() - current;
        const auto targetMs = static_cast<NomadInteger>(fixedDeltaMs);
        if (frameElapsed < targetMs) {
            SDL_Delay(static_cast<Uint32>(targetMs - frameElapsed));
        }

        // Free temporary heap allocations
        resetTempHeap();

        // Flush logs
        log::flush();
    }
}

void Game::executeWindowCallback(const std::shared_ptr<Closure>& callback, const std::vector<RuntimeValue>& args) {
    if (callback == nullptr) {
        return;
    }

    const auto activeCallback = callback;
    executeFunction(activeCallback.get(), nullptr, nullptr, {}, args);
}

void Game::quit() {
    m_running = false;
}

uint64_t Game::getUpdateDuration() const {
    return m_updateDuration;
}

uint64_t Game::getRenderDuration() const {
    return m_renderDuration;
}

Canvas* Game::getCanvas() const {
    return m_canvas.get();
}

NomadId Game::getFunctionId(const NomadString& functionName) const {
    return m_runtime->getFunctionId(functionName);
}

void Game::executeFunction(const NomadId functionId, Scene *scene, Entity *thisEntity, Entity *otherEntity) {
    m_currentContext.reset(scene, thisEntity);

    if (otherEntity) {
        m_currentContext.addOtherEntity(otherEntity);
    }

    m_runtime->executeFunction(functionId);
}

void Game::executeFunction(
    const NomadId functionId,
    Scene *scene,
    Entity *entity,
    const std::vector<RuntimeValue> &args,
    RuntimeValue &returnValue
) {
    m_currentContext.reset(scene, entity);

    m_runtime->executeFunction(functionId, args, returnValue);
}

void Game::executeFunction(
    const NomadId functionId,
    Scene* scene,
    Entity* entity,
    const std::vector<Entity*>& others,
    const std::vector<RuntimeValue>& args
) {
    m_currentContext.reset(scene, entity);
    m_currentContext.setOtherEntities(others);

    m_runtime->executeFunction(functionId, args);
}

void Game::executeFunction(
    const NomadId functionId,
    Scene* scene,
    Entity* entity,
    const std::vector<Entity*>& others,
    const std::vector<RuntimeValue>& args,
    RuntimeValue& returnValue
) {
    m_currentContext.reset(scene, entity);
    m_currentContext.setOtherEntities(others);

    m_runtime->executeFunction(functionId, args, returnValue);
}

void Game::executeFunction(
    const Closure* closure,
    Scene* scene,
    Entity* entity,
    const std::vector<Entity*>& others,
    const std::vector<RuntimeValue>& args
) {
    if (closure == nullptr) {
        log::error("Attempted to execute null function closure");
        return;
    }

    m_currentContext.reset(scene, entity);
    m_currentContext.setOtherEntities(others);

    m_runtime->executeFunction(closure, args);
}

void Game::executeFunction(
    const Closure* closure,
    Scene* scene,
    Entity* entity,
    const std::vector<Entity*>& others,
    const std::vector<RuntimeValue>& args,
    RuntimeValue& returnValue
) {
    if (closure == nullptr) {
        log::error("Attempted to execute null function closure");
        return;
    }

    m_currentContext.reset(scene, entity);
    m_currentContext.setOtherEntities(others);

    m_runtime->executeFunction(closure, args, returnValue);
}

//
// void Game::executeFunctionInCurrentContext(const NomadId functionId) {
//     m_runtime->executeFunction(functionId);
// }
//
// void Game::executeFunctionInCurrentContext(const NomadId functionId, RuntimeValue& returnValue) {
//     m_runtime->executeFunction(functionId, returnValue);
// }
//
// void Game::executeFunctionInNewContext(const NomadId functionId, Scene* scene, Entity* entity) {
//     pushExecutionContext(scene, entity);
//
//     m_runtime->executeFunction(functionId);
//
//     popExecutionContext();
// }
//
// void Game::executeFunctionInNewContext(const NomadId functionId, Scene* scene, Entity* entity, Entity* other) {
//     pushExecutionContext(scene, entity, other);
//
//     m_runtime->executeFunction(functionId);
//
//     popExecutionContext();
// }
//
// void Game::executeFunctionInNewContext(const NomadId functionId, Scene* scene, Entity* entity, const std::vector<Entity*>& other_entities) {
//     pushExecutionContext(scene, entity, other_entities);
//
//     m_runtime->executeFunction(functionId);
//
//     popExecutionContext();
// }
//
// void Game::executeFunctionInNewContext(const NomadId functionId, Scene* scene, Entity* entity, RuntimeValue& returnValue) {
//     pushExecutionContext(scene, entity);
//
//     m_runtime->executeFunction(functionId, returnValue);
//
//     popExecutionContext();
// }
//
// void Game::executeFunctionInContext(const NomadId functionId, GameExecutionContext* context) {
//     const auto previousContext = m_currentContext;
//
//     m_currentContext = context;
//
//     m_runtime->executeFunction(functionId);
//
//     m_currentContext = previousContext;
// }
//
// void Game::executeFunctionInContext(const NomadId functionId, GameExecutionContext* context, RuntimeValue& returnValue) {
//     const auto previousContext = m_currentContext;
//
//     m_currentContext = context;
//
//     m_runtime->executeFunction(functionId, returnValue);
//
//     m_currentContext = previousContext;
// }

// void Game::executeFunctionByName(const NomadString& functionName, Scene* scene, Entity* entity, RuntimeValue& returnValue) {
//     const auto function_id = getFunctionId(functionName);
//
//     if (function_id == NOMAD_INVALID_ID) {
//         raiseError("Function '" + functionName + "' not found");
//     }
//
//     executeFunctionInNewContext(function_id, scene, entity, returnValue);
// }

bool Game::executePredicate(const NomadId functionId) const {
    RuntimeValue return_value;

    m_runtime->executeFunction(functionId, {}, return_value);

    return return_value.getBooleanValue();
}

NomadId Game::createScene(
    const NomadString& sceneName,
    const NomadId functionId,
    std::unique_ptr<Closure> postCreateClosure
) {
    // Generate scene ID
    const auto sceneId = getNextSceneId();

    m_eventQueue.push(GameEventCreateScene {
        sceneId,
        sceneName,
        functionId,
        std::move(postCreateClosure)
   });

    return sceneId;
}

void Game::removeScene(const Scene* scene) {
    m_eventQueue.push(GameEventRemoveScene {
        scene->getId()
    });
}

Scene* Game::getSceneById(const NomadId id) const {
    if (id == NOMAD_INVALID_ID) {
        return nullptr;
    }

    const auto it = std::ranges::find_if(
        m_scenes,
        [id](const auto& scene) {
            return scene->getId() == id;
        }
    );

    return it != m_scenes.end() ? it->get() : nullptr;
}

Scene * Game::getSceneByName(const NomadString &name) const {
    auto it = std::ranges::find_if(
            m_scenes,
        [&name](const auto& scene) {
            return scene->getName() == name;
        }
    );

    return it != m_scenes.end() ? it->get() : nullptr;
}

void Game::forEachScene(const std::function<void(Scene*)>& callback) const {
    for (const auto& scene : m_scenes) {
        callback(scene.get());
    }
}

NomadId Game::addEntityToScene(
    const NomadId sceneId,
    const NomadId initFunctionId,
    const NomadFloat x,
    const NomadFloat y,
    const NomadFloat width,
    const NomadFloat height,
    const NomadInteger layer,
    const NomadString& text,
    std::unique_ptr<Closure> postCreateClosure
) {
    const auto scene = getSceneById(sceneId);

    if (scene == nullptr) {
        raiseError("Scene ID " + toString(sceneId) + " not found");
    }

    const auto entityId = scene->generateEntityId();

    m_eventQueue.push(GameEventCreateEntity{
        entityId,
        sceneId,
        initFunctionId,
        x,
        y,
        width,
        height,
        layer,
        text,
        std::move(postCreateClosure),
    });

    return entityId;
}

void Game::removeEntityFromScene(const NomadId sceneId, const NomadId entityId) {
    m_eventQueue.push(GameEventRemoveEntity {
        entityId,
        sceneId
    });
}

void Game::dispatchEvent(const EventDispatch& dispatch) {
    if (dispatch.eventId == NOMAD_INVALID_ID) {
        log::error("Cannot dispatch event: invalid event id");
        return;
    }

    if (!m_runtime->getEventDefinition(dispatch.eventId)) {
        log::error("Cannot dispatch event: event id " + toString(dispatch.eventId) + " not found");
        return;
    }

    m_eventQueue.push(GameEventDispatch{dispatch});
}

void Game::triggerEvent(const NomadString &eventName) {
    m_eventQueue.push(GameEventTriggerEvent{
        eventName
    });
}

void Game::triggerSceneEvent(const NomadId sceneId, const NomadString &eventName) {
    m_eventQueue.push(GameEventTriggerSceneEvent {
        sceneId,
        eventName
    });
}

void Game::triggerSceneLayerEvent(const NomadId sceneId, NomadInteger layerNumber, const NomadString &eventName) {
    m_eventQueue.push(GameEventTriggerSceneLayerEvent {
        sceneId,
        layerNumber,
        eventName
    });
}

void Game::triggerEntityEvent(const NomadId sceneId, const NomadId entityId, const NomadString &eventName, std::vector<RuntimeValue> args) {
    m_eventQueue.push(GameEventTriggerEntityEvent {
        sceneId,
        entityId,
        m_currentFrame,
        false,
        eventName,
        std::move(args)
    });
}

void Game::triggerEntityEvent(const NomadId eventId, const NomadId sceneId, const NomadId entityId, std::vector<RuntimeValue> args) {
    if (const auto event = m_runtime->getEventDefinition(eventId)) {
       triggerEntityEvent(sceneId, entityId, event->name, std::move(args));
    } else {
        log::warning("Event ID " + toString(eventId) + " not found");
    }
}

void Game::scheduleEntityEvent(
    const NomadId sceneId,
    const NomadId entityId,
    const NomadInteger frameCount,
    const NomadBoolean repeat,
    const NomadString& eventName
) {
    m_eventQueue.push(
        GameEventTriggerEntityEvent {
            sceneId,
            entityId,
            m_currentFrame + frameCount,
            repeat,
            eventName,
            {},
        }
    );
}


void Game::scheduleEntityCallback(
    const NomadId sceneId,
    const NomadId entityId,
    const NomadInteger frameCount,
    const NomadBoolean repeat,
    const NomadId callback)
{
    m_eventQueue.push(GameEventTriggerEntityCallback {
        sceneId,
        entityId,
        frameCount,
        m_currentFrame + frameCount,
        repeat,
        callback
    });
}

void Game::raiseError(const NomadString& message) {
    log::error(message);

    throw GameException(message);
}

void Game::initSdl() {
    log::info("Initializing SDL");

    constexpr auto SDL_INIT_FLAGS = SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD;

    if (SDL_Init(SDL_INIT_FLAGS) == false) {
        raiseError("Failed to initialize SDL: " + NomadString(SDL_GetError()));
    }

    // Attempt to initialize audio independently.
    // This will allow the game to run even if audio initialization fails.
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == false) {
        log::warning("Failed to initialize SDL audio subsystem: " + NomadString(SDL_GetError()));
    }

    const auto result = SDL_CreateWindowAndRenderer(
        m_title.c_str(),
        static_cast<int>(m_windowSize.getX()),
        static_cast<int>(m_windowSize.getY()),
        SDL_WINDOW_RESIZABLE,
        &m_window,
        &m_renderer
    );

    if (result == false) {
        raiseError("Failed to create SDL window. Reason: " + NomadString(SDL_GetError()));
    }

    setResolution(m_windowSize.getX(), m_windowSize.getY());

    m_canvas = std::make_unique<Canvas>(this, m_renderer);
}

void Game::initSdlTtf() {
    const auto result = TTF_Init();

    if (result == false) {
        const NomadString ttf_message = SDL_GetError();

        const NomadString error_message = "Failed to initialize SDL_ttf. Reason: " + ttf_message;

        throw GameException(error_message);
    }
}

void Game::initResourcePath() {
    // Get rid of the trailing backslash
    if (m_options.resourcePath[m_options.resourcePath.size() - 1] == '\\') {
        m_options.resourcePath = m_options.resourcePath.substr(0, m_options.resourcePath.size() - 1);
    }

    // Ensure non-empty resource path ends with a slash
    const auto resource_path_end = m_options.resourcePath[m_options.resourcePath.size() - 1];
    if (resource_path_end != '/') {
        m_options.resourcePath += '/';
    }

    log::info("Resource path: " + m_options.resourcePath);
}

void Game::initRuntime() {
    log::info("Initializing function runtime");

    m_runtime = std::make_unique<Runtime>();

    m_runtime->setDebug(m_options.debug);
}

void Game::initEvents() const {
    log::info("Initializing events");

    m_runtime->registerEvent("update", {});
    m_runtime->registerEvent("beforeUpdate", {});
    m_runtime->registerEvent("afterUpdate", {});
}

void Game::initFunctions() {
    log::info("Initializing functions");

    initGameFunctions();
    initInputFunctions();
    initOtherEntityFunctions();
    initSceneFunctions();
    initSystemFunctions();
    initThisEntityFunctions();
    initWindowFunctions();
}

void Game::initDynamicVariables() {
    log::info("Initializing dynamic variables");

    initGameDynamicVariables();
    initInputDynamicVariables();
    initOtherDynamicVariables();
    initSceneDynamicVariables();
    initThisDynamicVariables();
    initWindowDynamicVariables();
}

void Game::initVariableContext() {
    auto sceneVariableContext = std::make_unique<SimpleVariableContext>();
    auto thisEntityVariableContext = std::make_unique<ThisEntityVariableContext>(this);
    auto otherEntityVariableContext = std::make_unique<OtherEntityVariableContext>(thisEntityVariableContext.get());
    auto inventoryVariableContext = std::make_unique<SimpleVariableContext>();
    auto settingsVariableContext = std::make_unique<SimpleVariableContext>();

    m_sceneVariableMap = sceneVariableContext->getVariableMap();
    m_thisEntityVariableMap = thisEntityVariableContext->getThisVariableMap();
    m_otherEntityVariableMap = thisEntityVariableContext->getOtherVariableMap();
    m_inventoryVariableMap = inventoryVariableContext->getVariableMap();
    m_inventoryContext = inventoryVariableContext.get();
    m_settingsContext = settingsVariableContext.get();

    const auto sceneContextId = m_runtime->registerVariableContext("scene", "scene.", std::move(sceneVariableContext));
    const auto thisEntityContextId = m_runtime->registerVariableContext(THIS_ENTITY_VARIABLE_CONTEXT, THIS_ENTITY_VARIABLE_PREFIX, std::move(thisEntityVariableContext));
    const auto otherEntityContextId = m_runtime->registerVariableContext(OTHER_ENTITY_VARIABLE_CONTEXT, OTHER_ENTITY_VARIABLE_PREFIX, std::move(otherEntityVariableContext));
    const auto inventoryContextId = m_runtime->registerVariableContext("inventory", "inventory.", std::move(inventoryVariableContext));
    const auto settingsContextId = m_runtime->registerVariableContext("settings", "settings.", std::move(settingsVariableContext));

    if (sceneContextId == NOMAD_INVALID_ID) {
        raiseError("Failed to register scene variable context");
    }
    if (thisEntityContextId == NOMAD_INVALID_ID) {
        raiseError("Failed to register `this` entity variable context");
    }
    if (otherEntityContextId == NOMAD_INVALID_ID) {
        raiseError("Failed to register `other` entity variable context");
    }
    if (inventoryContextId == NOMAD_INVALID_ID) {
        raiseError("Failed to register inventory variable context");
    }
    if (settingsContextId == NOMAD_INVALID_ID) {
        raiseError("Failed to register settings variable context");
    }
}

void Game::initResourceManager() {
    log::info("Initializing resource manager");

    m_resourceManager = std::make_unique<ResourceManager>(this, m_options.resourcePath);
}

void Game::initConstants() const {
    log::info("Initializing constants");

    m_runtime->registerConstant("alignment.topLeft", RuntimeValue(static_cast<NomadInteger>(Alignment::TopLeft)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.topMiddle", RuntimeValue(static_cast<NomadInteger>(Alignment::TopMiddle)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.topRight", RuntimeValue(static_cast<NomadInteger>(Alignment::TopRight)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.centerLeft", RuntimeValue(static_cast<NomadInteger>(Alignment::CenterLeft)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.centerMiddle", RuntimeValue(static_cast<NomadInteger>(Alignment::CenterMiddle)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.centerRight", RuntimeValue(static_cast<NomadInteger>(Alignment::CenterRight)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.bottomLeft", RuntimeValue(static_cast<NomadInteger>(Alignment::BottomLeft)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.bottomMiddle", RuntimeValue(static_cast<NomadInteger>(Alignment::BottomMiddle)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.bottomRight", RuntimeValue(static_cast<NomadInteger>(Alignment::BottomRight)), m_runtime->getIntegerType());

    m_runtime->registerConstant("alignment.left", RuntimeValue(static_cast<NomadInteger>(HorizontalAlignment::Left)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.middle", RuntimeValue(static_cast<NomadInteger>(HorizontalAlignment::Middle)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.right", RuntimeValue(static_cast<NomadInteger>(HorizontalAlignment::Right)), m_runtime->getIntegerType());

    m_runtime->registerConstant("alignment.top", RuntimeValue(static_cast<NomadInteger>(VerticalAlignment::Top)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.center", RuntimeValue(static_cast<NomadInteger>(VerticalAlignment::Center)), m_runtime->getIntegerType());
    m_runtime->registerConstant("alignment.bottom", RuntimeValue(static_cast<NomadInteger>(VerticalAlignment::Bottom)), m_runtime->getIntegerType());

    m_runtime->registerConstant("body.static", RuntimeValue(static_cast<NomadInteger>(BodyType::Static)), m_runtime->getIntegerType());
    m_runtime->registerConstant("body.dynamic", RuntimeValue(static_cast<NomadInteger>(BodyType::Dynamic)), m_runtime->getIntegerType());
    m_runtime->registerConstant("body.kinematic", RuntimeValue(static_cast<NomadInteger>(BodyType::Kinematic)), m_runtime->getIntegerType());

    m_runtime->registerConstant("cardinal.north", RuntimeValue(static_cast<NomadInteger>(Cardinal::North)), m_runtime->getIntegerType());
    m_runtime->registerConstant("cardinal.east", RuntimeValue(static_cast<NomadInteger>(Cardinal::East)), m_runtime->getIntegerType());
    m_runtime->registerConstant("cardinal.south", RuntimeValue(static_cast<NomadInteger>(Cardinal::South)), m_runtime->getIntegerType());
    m_runtime->registerConstant("cardinal.west", RuntimeValue(static_cast<NomadInteger>(Cardinal::West)), m_runtime->getIntegerType());
}

void Game::initText() const {
    log::info("Loading text...");

    m_resourceManager->getText()->loadTextFromCsv(this, m_options.resourcePath + "text/text.csv");
}

void Game::initDebugConsole() {
    if (m_options.debug) {
        log::info("Initializing debug console");
        m_debugConsole = std::make_unique<DebugConsole>(this, m_window, m_renderer);

        // game.name and game.organization are only known once the init function has run.
        ensureDebugSettingsLoaded();

        m_debugConsole->setUiScale(m_debugConsoleScale);

        if (m_debugConsoleVisible) {
            m_debugConsole->activate();
        }
    }
}

//NomadString Game::make_path(const NomadString& path) const {
void Game::createPathToFile(const NomadString& fileName) const {
    const auto path = std::filesystem::path(fileName).parent_path();

    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
}

void Game::loadTextIntoConst(const NomadString& languageCode) const {
    std::unordered_map<NomadString, NomadString> textMap;

    m_resourceManager->getText()->getAllText(languageCode, textMap);

    const auto constantType = m_runtime->getStringType();
    auto constantValue = RuntimeValue();

    for (const auto& [key, value]: textMap) {
        auto constantName = "t." + key;

        constantValue.setStringValue(value);

        m_runtime->registerConstant(constantName, constantValue, constantType);

        constantType->freeValue(constantValue);
    }
}

void Game::compileFunctions() {
    log::info("Compiling functions...");

    const auto start_ticks = SDL_GetTicks();

    auto compiler = m_runtime->createCompiler();
    CompilerContext context(compiler.get());
    auto compiled = false;
    NomadString exceptionMessage;

    try {
        compiler->loadScriptsFromPath(m_options.resourcePath + "scripts");
        compiler->loadScriptsFromPath(m_options.resourcePath + "mods");

        compiled = compiler->compileFunctions(&context);
    } catch (NomadException& e) {
        exceptionMessage = NomadString("Failed to compile functions: ") + e.what();
    } catch (std::exception& e) {
        exceptionMessage = NomadString("Unexpected exception while compiling functions: ") + e.what();
    }

    NomadString diagnosticMessages;
    for (const auto& diagnostic : context.getDiagnostics()) {
        const auto message = formatDiagnostic(diagnostic);
        diagnosticMessages += "\n" + message;
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            log::error(message);
        } else {
            log::warning(message);
        }
    }

    if (!exceptionMessage.empty()) {
        raiseError(exceptionMessage + diagnosticMessages);
    }

    if (!compiled) {
        raiseError(
            "Failed to compile functions: " + toString(context.getErrorCount()) + " error(s), " +
            toString(context.getWarningCount()) + " warning(s)" + diagnosticMessages
        );
    }

    const auto end_ticks = SDL_GetTicks();
    const auto elapsed_ticks = end_ticks - start_ticks;
    const auto function_count = m_runtime->getFunctionCount();

    const auto message =
        NomadString("Compiled ") +
        std::to_string(function_count) +
        " functions in " +
        std::to_string(elapsed_ticks) +
        "ms";

    log::info(message);
}

// void Game::pushExecutionContext(Scene* scene, Entity* entity) {
//     m_contextIndex++;
//
//     if (m_contextIndex >= m_contextStack.size()) {
//         raiseError("Execution context stack overflow");
//     }
//
//     m_currentContext = &m_contextStack[m_contextIndex];
//     m_currentContext->reset(scene, entity);
// }
//
// void Game::pushExecutionContext(Scene* scene, Entity* entity, Entity* other) {
//     m_contextIndex++;
//
//     if (m_contextIndex >= m_contextStack.size()) {
//         raiseError("Execution context stack overflow");
//     }
//
//     m_currentContext = &m_contextStack[m_contextIndex];
//     m_currentContext->reset(scene, entity);
//     m_currentContext->addOtherEntity(other);
// }
//
// void Game::pushExecutionContext(Scene* scene, Entity* entity, const std::vector<Entity*>& other_entities) {
//     m_contextIndex++;
//
//     if (m_contextIndex >= m_contextStack.size()) {
//         raiseError("Execution context stack overflow");
//     }
//
//     m_currentContext = &m_contextStack[m_contextIndex];
//     m_currentContext->reset(scene, entity);
//     m_currentContext->setOtherEntities(other_entities);
// }
//
// void Game::popExecutionContext() {
//     m_contextIndex--;
//
//     if (m_contextIndex < 0) {
//         raiseError("Execution context stack underflow");
//     }
//
//     m_currentContext = &m_contextStack[m_contextIndex];
// }
//
// GameExecutionContext* Game::getCurrentContext() const {
// //    return &m_context_stack[m_context_index];
//     return m_currentContext;
// }

void Game::setLanguage(const NomadString& languageCode) {
    m_language = languageCode;

    loadTextIntoConst(languageCode);
}

const NomadString& Game::getLanguage() const {
    return m_language;
}

NomadString& Game::getText(const NomadString& key, NomadString& text) const {
    m_resourceManager->getText()->getText(m_language, key, text);

    return text;
}

void Game::runInitFunction() {
    log::info("Executing `init` function");

    const NomadId initFunctionId = m_runtime->getFunctionId("init");

    if (initFunctionId == NOMAD_INVALID_ID) {
        const auto message = "Fatal: No `init` function found";

        log::fatal(message);

        exit(EXIT_FAILURE);
    }

    try {
        executeFunction(initFunctionId, nullptr, nullptr);
    } catch (std::exception& e) {
        raiseError(NomadString("Failed to execute 'init' function: ") + e.what());
    }
}

void Game::render(Canvas* canvas) const {
    canvas->clear(m_clearColor);

    for (const auto& scene: m_scenes) {
        scene->render(canvas);
    }
}

void Game::update() {
    for (const auto& scene: m_scenes) {
        scene->update(this);
    }

    processGameEvents();

    // Sort scenes by layer
    std::ranges::sort(m_scenes, [](const auto& a, const auto& b) {
        return a->getZ() < b->getZ();
    });

    // Increase frame number
    m_currentFrame++;
}

namespace {
    template<class... Ts> struct EventVisitor : Ts... { using Ts::operator()...; };
    // template<class... Ts> EventVisitor(Ts...) -> EventVisitor<Ts...>;
}

void Game::processGameEvents() {
    // Declare EventVisitor `const` to minimize instantiation in the loop.
    const auto visitor = EventVisitor {
        [this](const GameEventCreateScene& e) -> bool { return processGameEventCreateScene(e); },
        [this](const GameEventRemoveScene& e) -> bool { return processGameEventRemoveScene(e); },
        [this](const GameEventCreateEntity& e) -> bool { return processGameEventCreateEntity(e); },
        [this](const GameEventRemoveEntity& e) -> bool { return processGameEventRemoveEntity(e); },
        [this](const GameEventDispatch& e) -> bool { return processGameDispatchEvent(e); },
        [this](const GameEventTriggerEvent& e) -> bool { return processGameEventTriggerEvent(e); },
        [this](const GameEventTriggerSceneEvent& e) -> bool { return processGameEventTriggerSceneEvent(e); },
        [this](const GameEventTriggerSceneLayerEvent& e) -> bool { return processGameEventTriggerSceneLayerEvent(e); },
        [this](const GameEventTriggerEntityEvent& e) -> bool { return processGameEventTriggerEntityEvent(e); },
        [this](const GameEventTriggerEntityCallback& e) -> bool { return processGameEventTriggerEntityCallback(e); }
    };

    m_eventQueue.processFrame(
        [&visitor](const GameEvent& event) {
            return std::visit(visitor, event);
        }
    );
}

bool Game::processGameEventCreateScene(const GameEventCreateScene &event) {
    auto scene = std::make_unique<Scene>(this, event.sceneId);
    auto scenePtr = scene.get();

    scenePtr->setName(event.sceneName);

    m_scenes.push_back(std::move(scene));

    if (event.initFunctionId != NOMAD_INVALID_ID) {
        executeFunction(event.initFunctionId, scenePtr, nullptr);
    } else {
        log::error("No init function for scene " + toString(event.sceneId));
    }

    if (event.postCreateClosure != nullptr) {
        executeFunction(event.postCreateClosure.get(), scenePtr, nullptr);
    }

    return true;
}

bool Game::processGameEventRemoveScene(const GameEventRemoveScene &event) {
    const auto scene = std::ranges::find_if(
        m_scenes,
        [&event](const auto& candidate) {
            return candidate->getId() == event.sceneId;
        }
    );

    if (scene == m_scenes.end()) {
        log::error("Failed to find scene with id " + toString(event.sceneId));
    } else {
        m_scenes.erase(scene);
    }

    return true;
}

bool Game::processGameEventCreateEntity(const GameEventCreateEntity &event) {
    const auto scene = getSceneById(event.sceneId);

    if (scene == nullptr) {
        log::error("Failed to find scene with id " + toString(event.sceneId));
    } else {

        auto entity = std::make_unique<Entity>(
            scene,
            m_thisEntityVariableMap,
            event.entityId,
            event.x,
            event.y,
            event.layer
        );
        auto entityPtr = entity.get();

        entityPtr->setSize(event.width, event.height);

        if (event.text.empty() == false) {
            entityPtr->setText(event.text);
        }

        scene->addEntity(std::move(entity));

        if (event.initFunctionId != NOMAD_INVALID_ID) {
            executeFunction(event.initFunctionId, scene, entityPtr);
        } else {
            log::error("No init function for entity " + toString(event.entityId) + " in scene " + toString(event.sceneId));
        }

        if (event.postCreateClosure != nullptr) {
            executeFunction(event.postCreateClosure.get(), scene, entityPtr);
        }
    }

    return true;
}

bool Game::processGameDispatchEvent(const GameEventDispatch& event) const {
    for (const auto& scene : m_scenes) {
        scene->dispatchEvent(event.dispatch);
    }

    return true;
}

bool Game::processGameEventTriggerEvent(const GameEventTriggerEvent &event) const {
    for (const auto& scene : m_scenes) {
        scene->dispatchEvent(event.eventName);
    }

    return true;
}

bool Game::processGameEventRemoveEntity(const GameEventRemoveEntity &event) const {
    const auto scene = getSceneById(event.sceneId);

    if (scene == nullptr) {
        log::error("Failed to find scene with id " + toString(event.sceneId));
    } else {

        const auto entity = scene->getEntityById(event.entityId);

        if (entity == nullptr) {
            log::error("Failed to find entity with id " + toString(event.entityId) + " in scene " + toString(event.sceneId));
        } else {
            scene->removeEntity(entity);
        }
    }

    return true;
}

bool Game::processGameEventTriggerSceneEvent(const GameEventTriggerSceneEvent &event) const {
    const auto scene = getSceneById(event.sceneId);

    if (scene == nullptr) {
        log::error("Failed to find scene with id " + toString(event.sceneId));
    } else {
        scene->dispatchEvent(event.eventName);
    }

    return true;
}

bool Game::processGameEventTriggerSceneLayerEvent(const GameEventTriggerSceneLayerEvent &event) const {
    const auto scene = getSceneById(event.sceneId);

    if (scene == nullptr) {
        log::error("Failed to find scene with id " + toString(event.sceneId));

        return true;
    }

    scene->triggerEventLayer(event.eventName, event.layer);

    return true;
}

bool Game::processGameEventTriggerEntityEvent(const GameEventTriggerEntityEvent &event) const {
    const auto scene = getSceneById(event.sceneId);

    if (scene == nullptr) {
        log::error("Failed to find scene with id " + toString(event.sceneId));

        return true;
    }

    const auto entity = scene->getEntityById(event.entityId);

    if (entity == nullptr) {
        log::error("Failed to find entity with id " + toString(event.entityId) + " in scene " + toString(event.sceneId));

        return true;
    }

    if (event.triggerAtFrame > m_currentFrame) {
        // Not yet ready to trigger. Re-queue
        return false;
    }

    scene->dispatchEventForEntity(event.eventName, entity);

    return true;
}

bool Game::processGameEventTriggerEntityCallback(const GameEventTriggerEntityCallback &event) {
    if (event.triggerAtFrame > m_currentFrame) {
        // Not yet ready to trigger. Re-queue
        return false;
    }

    const auto scene = getSceneById(event.sceneId);

    if (scene == nullptr) {
        log::error("Failed to find scene with id " + toString(event.sceneId));

        return true;
    }

    const auto entity = scene->getEntityById(event.entityId);

    if (entity == nullptr) {
        log::error("Failed to find entity with id " + toString(event.entityId) + " in scene " + toString(event.sceneId));

        return true;
    }

    executeFunction(event.functionId, scene, entity);

    if (event.repeat) {
        scheduleEntityCallback(
            event.sceneId,
            event.entityId,
            event.frameCount,
            event.repeat,
            event.functionId
        );
    }

    return true;
}

bool Game::processSystemInput(const SDL_KeyboardEvent& event) {
    switch (event.type) {
    case SDL_EVENT_KEY_UP:
        switch (event.key) {
        case SDLK_F12:
            if (m_debugConsole != nullptr) {
                setDebugConsoleVisible(!m_debugConsole->isActive());
                return true;
            }
            break;
        case SDLK_RETURN:
            if ((event.mod & SDL_KMOD_ALT) != 0) {
                const auto fullscreen = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
                SDL_SetWindowFullscreen(m_window, !fullscreen);
                return true;
            }
            break;
        default:
            // Not a system key. Ignore
            break;
        }
    default:
        // Not an event we care about. Ignore
        break;
    }

    return false;
}

void Game::processInput(SDL_KeyboardEvent& event) {
    // Process system input.
    if (processSystemInput(event)) {
        return;
    }

    if (m_debugConsole != nullptr && m_debugConsole->wantsKeyboard()) {
        return;
    }

    InputEvent input_event;

    mapSdlKeyboardEvent(event, input_event);

    dispatchInputEvent(input_event);
}

void Game::processInput(SDL_MouseButtonEvent& event) {
    if (m_debugConsole != nullptr && m_debugConsole->wantsMouse()) {
        return;
    }

    if (event.button < m_mouseButtonState.size()) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                m_mouseButtonState[event.button].pressed = true;
                m_mouseButtonState[event.button].held = true;
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                m_mouseButtonState[event.button].released = true;
                m_mouseButtonState[event.button].held = false;
                break;

            default:
                log::warning("Event type (" + toString(static_cast<NomadInteger>(event.type)) + " not recognized.");
        }

        InputEvent input_event;

        mapSdlMouseEvent(event, input_event);

        dispatchInputEvent(input_event);
    }
}

void Game::processInput(SDL_GamepadButtonEvent& event) const {
    InputEvent input_event;

    mapSdlGamepadButtonEvent(event, input_event);

    dispatchInputEvent(input_event);
}

void Game::dispatchInputEvent(const InputEvent& event) const {
    for (const auto& scene: m_scenes) {
        scene->processInputEvent(event);
    }
}

NomadId Game::getNextSceneId() {
    if (m_scenes.size() >= NOMAD_ID_MAX - NOMAD_ID_MIN) {
        // We're already at the max number of scenes.
        return NOMAD_INVALID_ID;
    }

    const auto startId = m_sceneIdCounter;

    while (true) {
        m_sceneIdCounter++;

        const auto scene = getSceneById(m_sceneIdCounter);

        if (scene == nullptr) {
            return m_sceneIdCounter;
        }

        if (m_sceneIdCounter == NOMAD_ID_MAX) {
            m_sceneIdCounter = NOMAD_ID_MIN;
        }

        if (m_sceneIdCounter == startId) {
            return NOMAD_INVALID_ID;
        }
    }
}

Scene* Game::getCurrentScene() const {
    return getCurrentContext()->getScene();
}

} // nomad
