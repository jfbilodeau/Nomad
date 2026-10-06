// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/debug/DebugConsole.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/EntityVariableContext.hpp>
#include <nomad/game/Game.hpp>
#include <nomad/game/GameExecutionContext.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Function.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VariableContext.hpp>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace nomad {

namespace {

// `Game` registers the scene variable context under this name.
const NomadString SCENE_VARIABLE_CONTEXT = "scene";

bool containsText(const NomadString& value, const char* filter) {
    if (filter[0] == '\0') {
        return true;
    }

    NomadString lowerValue = value;
    NomadString lowerFilter = filter;
    std::ranges::transform(lowerValue, lowerValue.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    std::ranges::transform(lowerFilter, lowerFilter.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return lowerValue.find(lowerFilter) != NomadString::npos;
}

constexpr float CONSOLE_SPLITTER_THICKNESS = 6.0f;
constexpr float CONSOLE_SPLIT_MINIMUM = 0.20f;
constexpr float CONSOLE_SPLIT_MAXIMUM = 0.85f;
constexpr std::size_t FRAME_PROFILE_HISTORY_LIMIT = 240;
constexpr float RESOURCE_CHART_MINIMUM_HEIGHT = 44.0f;
constexpr ImU32 INPUT_COLOR = IM_COL32(241, 196, 83, 230);
constexpr ImU32 UPDATE_COLOR = IM_COL32(80, 145, 230, 230);
constexpr ImU32 RENDER_COLOR = IM_COL32(232, 91, 98, 230);
constexpr ImU32 FRAME_TARGET_COLOR = IM_COL32(92, 200, 132, 240);
constexpr ImU32 HEAP_USAGE_COLOR = IM_COL32(116, 208, 184, 255);

struct ProfileStatistics {
    float current = 0.0f;
    float average = 0.0f;
    float peak = 0.0f;
};

template<typename Projection>
ProfileStatistics calculateProfileStatistics(
    const std::deque<FrameProfile>& profiles,
    Projection projection
) {
    if (profiles.empty()) {
        return {};
    }

    ProfileStatistics statistics;
    auto total = 0.0f;
    for (const auto& profile : profiles) {
        const auto value = static_cast<float>(projection(profile));
        total += value;
        statistics.peak = std::max(statistics.peak, value);
    }

    statistics.current = static_cast<float>(projection(profiles.back()));
    statistics.average = total / static_cast<float>(profiles.size());
    return statistics;
}

NomadString formatByteCount(const float byteCount) {
    constexpr auto bytesPerKibibyte = 1024.0f;
    constexpr auto bytesPerMebibyte = bytesPerKibibyte * 1024.0f;
    char buffer[32]{};

    if (byteCount >= bytesPerMebibyte) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MiB", byteCount / bytesPerMebibyte);
    } else if (byteCount >= bytesPerKibibyte) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KiB", byteCount / bytesPerKibibyte);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f B", byteCount);
    }

    return buffer;
}

void drawChartBackground(ImDrawList* drawList, const ImVec2& minimum, const ImVec2& maximum) {
    drawList->AddRectFilled(minimum, maximum, IM_COL32(13, 15, 19, 245), 3.0f);
    drawList->AddRect(minimum, maximum, IM_COL32(74, 82, 96, 255), 3.0f);
}

float drawChartLegendItem(
    ImDrawList* drawList,
    float x,
    float y,
    float textHeight,
    float scale,
    const char* label,
    ImU32 color,
    bool line
) {
    const auto markerWidth = 10.0f * scale;
    const auto markerCenterY = y + textHeight * 0.5f;
    if (line) {
        drawList->AddLine({x, markerCenterY}, {x + markerWidth, markerCenterY}, color, 2.0f * scale);
    } else {
        drawList->AddRectFilled(
            {x, markerCenterY - 4.0f * scale},
            {x + markerWidth, markerCenterY + 4.0f * scale},
            color
        );
    }

    const auto textX = x + markerWidth + 4.0f * scale;
    drawList->AddText({textX, y}, IM_COL32(226, 230, 238, 255), label);
    return textX + ImGui::CalcTextSize(label).x + 10.0f * scale;
}

void setPanel(float x, float y, float width, float height) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + x, viewport->WorkPos.y + y});
    ImGui::SetNextWindowSize({width, height});
}

class ExecutionContextGuard {
public:
    ExecutionContextGuard(Game* game, Scene* scene, Entity* entity):
        m_context(game->getCurrentContext()),
        m_scene(m_context->getScene()),
        m_entity(m_context->getThisEntity()),
        m_otherEntities(m_context->getOtherEntities())
    {
        m_context->reset(scene, entity);
    }

    ExecutionContextGuard(const ExecutionContextGuard&) = delete;
    ExecutionContextGuard& operator=(const ExecutionContextGuard&) = delete;

    ~ExecutionContextGuard() {
        m_context->reset(m_scene, m_entity);
        m_context->setOtherEntities(m_otherEntities);
    }

private:
    GameExecutionContext* m_context;
    Scene* m_scene;
    Entity* m_entity;
    std::vector<Entity*> m_otherEntities;
};

// Inspecting dynamic variables re-runs their getters every frame. Getters that
// warn about unavailable state (a sprite-less entity, for example) would
// otherwise flood the log with one entry per frame.
class LogSuppressionGuard {
public:
    LogSuppressionGuard(): m_previousLevel(log::getLogLevel()) {
        log::setLogLevel(LogLevel::Fatal);
    }

    LogSuppressionGuard(const LogSuppressionGuard&) = delete;
    LogSuppressionGuard& operator=(const LogSuppressionGuard&) = delete;

    ~LogSuppressionGuard() {
        log::setLogLevel(m_previousLevel);
    }

private:
    LogLevel m_previousLevel;
};

// Dynamic getters are allowed to leave a string value untouched when the value
// is unavailable, so the raw pointer may be null.
NomadString valueToDisplayString(const Type* type, const RuntimeValue& value) {
    if (type == nullptr) {
        return "<unknown>";
    }

    if (type->isString() && value.getStringValue() == nullptr) {
        return "<null>";
    }

    return type->toString(value);
}

bool parseBoolean(const NomadString& text, NomadBoolean& value) {
    if (text == BOOLEAN_TRUE_STRING || text == "1") {
        value = true;
        return true;
    }

    if (text == BOOLEAN_FALSE_STRING || text == "0") {
        value = false;
        return true;
    }

    return false;
}

// Builds a scalar RuntimeValue from user-entered text. String values are
// allocated and must be released by the caller.
bool parseValue(const Type* type, const NomadString& text, RuntimeValue& value) {
    if (type == nullptr) {
        return false;
    }

    if (type->isString()) {
        value.setStringValue(text);
        return true;
    }

    const auto typeName = type->getTypeName();

    try {
        if (typeName == BOOLEAN_TYPE_NAME) {
            NomadBoolean booleanValue = false;
            if (!parseBoolean(text, booleanValue)) {
                return false;
            }
            value.setBooleanValue(booleanValue);
            return true;
        }

        if (typeName == INTEGER_TYPE_NAME) {
            size_t consumed = 0;
            const auto integerValue = std::stoll(text, &consumed);
            if (consumed != text.size()) {
                return false;
            }
            value.setIntegerValue(static_cast<NomadInteger>(integerValue));
            return true;
        }

        if (typeName == FLOAT_TYPE_NAME) {
            size_t consumed = 0;
            const auto floatValue = std::stod(text, &consumed);
            if (consumed != text.size()) {
                return false;
            }
            value.setFloatValue(static_cast<NomadFloat>(floatValue));
            return true;
        }
    } catch (const std::exception&) {
        return false;
    }

    return false;
}

[[nodiscard]] bool isBooleanType(const Type* type) {
    return type != nullptr && type->getTypeName() == BOOLEAN_TYPE_NAME;
}

[[nodiscard]] bool isEditableType(const Type* type) {
    if (type == nullptr || type->isVoid()) {
        return false;
    }

    if (type->isString()) {
        return true;
    }

    const auto typeName = type->getTypeName();
    return typeName == BOOLEAN_TYPE_NAME ||
           typeName == INTEGER_TYPE_NAME ||
           typeName == FLOAT_TYPE_NAME;
}

} // namespace

DebugConsole::DebugConsole(Game* game, SDL_Window* window, SDL_Renderer* renderer):
    m_game(game),
    m_window(window),
    m_renderer(renderer),
    m_interpreter(game->getRuntime()),
    m_inspectionVirtualMachine(game->getRuntime()->createVirtualMachine())
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    ImGui::StyleColorsDark();
    m_baseStyle = std::make_unique<ImGuiStyle>(ImGui::GetStyle());
    applyUiScale(m_requestedUiScale);

    if (!ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer)) {
        ImGui::DestroyContext();
        throw std::runtime_error("Failed to initialize the Dear ImGui SDL3 backend");
    }
    if (!ImGui_ImplSDLRenderer3_Init(m_renderer)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("Failed to initialize the Dear ImGui SDL renderer backend");
    }

    log::addSink(&m_memorySink);
}

DebugConsole::~DebugConsole() {
    if (m_active) {
        deactivate();
    }

    log::removeSink(&m_memorySink);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (m_gameTexture != nullptr) {
        SDL_DestroyTexture(m_gameTexture);
    }
}

void DebugConsole::activate() {
    if (m_active) {
        return;
    }

    SDL_GetWindowPosition(m_window, &m_savedWindowX, &m_savedWindowY);
    SDL_GetWindowSize(m_window, &m_savedWindowWidth, &m_savedWindowHeight);
    m_wasFullscreen = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (m_wasFullscreen) {
        SDL_SetWindowFullscreen(m_window, false);
    }

    SDL_Rect usableBounds{};
    const auto displayId = SDL_GetDisplayForWindow(m_window);
    if (displayId != 0 && SDL_GetDisplayUsableBounds(displayId, &usableBounds)) {
        const auto desiredWidth = std::min(usableBounds.w, std::max(m_savedWindowWidth, 1440));
        const auto desiredHeight = std::min(usableBounds.h, std::max(m_savedWindowHeight, 900));
        SDL_SetWindowSize(m_window, desiredWidth, desiredHeight);
        SDL_SetWindowPosition(
            m_window,
            usableBounds.x + (usableBounds.w - desiredWidth) / 2,
            usableBounds.y + (usableBounds.h - desiredHeight) / 2
        );
    }

    SDL_SetRenderLogicalPresentation(m_renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    m_active = true;
    ensureGameTexture();
}

void DebugConsole::deactivate() {
    if (!m_active) {
        return;
    }

    m_active = false;
    m_game->setResolution(m_game->getResolution().getX(), m_game->getResolution().getY());
    SDL_SetWindowSize(m_window, m_savedWindowWidth, m_savedWindowHeight);
    SDL_SetWindowPosition(m_window, m_savedWindowX, m_savedWindowY);
    if (m_wasFullscreen) {
        SDL_SetWindowFullscreen(m_window, true);
    }
}

void DebugConsole::toggle() {
    if (m_active) {
        deactivate();
    } else {
        activate();
    }
}

void DebugConsole::setUiScale(const float scale) {
    m_requestedUiScale = std::clamp(scale, DEBUG_CONSOLE_MINIMUM_SCALE, DEBUG_CONSOLE_MAXIMUM_SCALE);
}

bool DebugConsole::isActive() const {
    return m_active;
}

bool DebugConsole::wantsKeyboard() const {
    return m_active && ImGui::GetIO().WantCaptureKeyboard;
}

bool DebugConsole::wantsMouse() const {
    return m_active && ImGui::GetIO().WantCaptureMouse;
}

void DebugConsole::processEvent(const SDL_Event& event) const {
    if (m_active) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
}

bool DebugConsole::beginGameRender() {
    if (!m_active) {
        return false;
    }

    ensureGameTexture();
    SDL_SetRenderLogicalPresentation(m_renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderScale(m_renderer, 1.0f, 1.0f);
    if (!SDL_SetRenderTarget(m_renderer, m_gameTexture)) {
        log::error("Failed to set debug game render target: " + NomadString(SDL_GetError()));
        return false;
    }
    return true;
}

void DebugConsole::endGameRender() const {
    if (m_active && !SDL_SetRenderTarget(m_renderer, nullptr)) {
        log::error("Failed to restore the window render target: " + NomadString(SDL_GetError()));
    }
}

void DebugConsole::render() {
    if (!m_active) {
        return;
    }

    if (m_requestedUiScale != m_uiScale) {
        applyUiScale(m_requestedUiScale);
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    renderToolbar();
    renderGameView();
    renderEntityInspector();
    renderVariableInspector();
    renderFunctionInspector();
    renderConsole();
    renderConsoleSplitter();
    renderLogs();

    ImGui::Render();
    SDL_SetRenderTarget(m_renderer, nullptr);
    SDL_SetRenderScale(
        m_renderer,
        ImGui::GetIO().DisplayFramebufferScale.x,
        ImGui::GetIO().DisplayFramebufferScale.y
    );
    SDL_SetRenderDrawColor(m_renderer, 20, 20, 24, 255);
    SDL_RenderClear(m_renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
}

void DebugConsole::pushFrameProfile(const FrameProfile& frameProfile) {
    if (m_frameProfiles.size() == FRAME_PROFILE_HISTORY_LIMIT) {
        m_frameProfiles.pop_front();
    }
    m_frameProfiles.push_back(frameProfile);
}

bool DebugConsole::consumeStepRequest() {
    const auto requested = m_stepRequested;
    m_stepRequested = false;
    return requested;
}

void DebugConsole::ensureGameTexture() {
    const auto& resolution = m_game->getResolution();
    const auto width = resolution.getX();
    const auto height = resolution.getY();
    if (m_gameTexture != nullptr && width == m_textureWidth && height == m_textureHeight) {
        return;
    }

    if (m_gameTexture != nullptr) {
        SDL_DestroyTexture(m_gameTexture);
        m_gameTexture = nullptr;
    }

    m_gameTexture = SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        static_cast<int>(width),
        static_cast<int>(height)
    );
    if (m_gameTexture == nullptr) {
        throw std::runtime_error("Failed to create debug game texture: " + NomadString(SDL_GetError()));
    }

    SDL_SetTextureScaleMode(m_gameTexture, SDL_SCALEMODE_NEAREST);
    m_textureWidth = width;
    m_textureHeight = height;
}

void DebugConsole::applyUiScale(const float scale) {
    const auto clampedScale = std::clamp(scale, DEBUG_CONSOLE_MINIMUM_SCALE, DEBUG_CONSOLE_MAXIMUM_SCALE);
    auto& style = ImGui::GetStyle();
    style = *m_baseStyle;
    style.ScaleAllSizes(clampedScale);
    style.FontScaleMain = clampedScale;
    m_uiScale = clampedScale;
    m_requestedUiScale = clampedScale;
}

float DebugConsole::getToolbarHeight() const {
    return 44.0f * m_uiScale;
}

void DebugConsole::renderToolbar() {
    const auto* viewport = ImGui::GetMainViewport();
    setPanel(0.0f, 0.0f, viewport->WorkSize.x, getToolbarHeight());
    ImGui::Begin(
        "Debug Toolbar",
        nullptr,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
    );

    if (m_game->isPaused()) {
        if (ImGui::Button("Resume")) {
            m_game->resume();
        }
        ImGui::SameLine();
        if (ImGui::Button("Step")) {
            m_stepRequested = true;
        }
    } else if (ImGui::Button("Pause")) {
        m_game->pause();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f * m_uiScale);
    ImGui::SliderFloat(
        "UI scale",
        &m_requestedUiScale,
        DEBUG_CONSOLE_MINIMUM_SCALE,
        DEBUG_CONSOLE_MAXIMUM_SCALE,
        "%.2fx",
        ImGuiSliderFlags_AlwaysClamp
    );

    // Persist once the edit is complete rather than on every frame of a drag.
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        m_game->setDebugConsoleScale(m_requestedUiScale);
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("F12 closes |");
    ImGui::SameLine();
    const auto* scene = getSelectedScene();
    const auto* entity = getSelectedEntity();
    ImGui::Text(
        "Context: %s / %s | %.1f FPS",
        scene != nullptr ? scene->getName().c_str() : "<no scene>",
        entity != nullptr ? entity->getName().c_str() : "<no entity>",
        ImGui::GetIO().Framerate
    );

    ImGui::End();
}

void DebugConsole::renderGameView() const {
    const auto* viewport = ImGui::GetMainViewport();
    const auto bottomY = viewport->WorkSize.y * 0.72f;
    const auto leftWidth = viewport->WorkSize.x * 0.20f;
    const auto rightWidth = viewport->WorkSize.x * 0.28f;
    const auto toolbarHeight = getToolbarHeight();
    setPanel(
        leftWidth,
        toolbarHeight,
        viewport->WorkSize.x - leftWidth - rightWidth,
        bottomY - toolbarHeight
    );
    ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    const auto available = ImGui::GetContentRegionAvail();
    const auto textureAspect = static_cast<float>(m_textureWidth) / static_cast<float>(m_textureHeight);
    auto imageSize = available;
    if (imageSize.x / imageSize.y > textureAspect) {
        imageSize.x = imageSize.y * textureAspect;
    } else {
        imageSize.y = imageSize.x / textureAspect;
    }

    const auto cursor = ImGui::GetCursorPos();
    const auto contentTopLeft = ImGui::GetCursorScreenPos();
    const ImVec2 imageOffset{
        (available.x - imageSize.x) * 0.5f,
        (available.y - imageSize.y) * 0.5f
    };
    ImGui::SetCursorPos({
        cursor.x + imageOffset.x,
        cursor.y + imageOffset.y
    });
    ImGui::Image(
        ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_gameTexture))),
        imageSize
    );
    renderResourceCharts(
        contentTopLeft,
        available,
        {contentTopLeft.x + imageOffset.x, contentTopLeft.y + imageOffset.y},
        imageSize
    );

    ImGui::End();
}

void DebugConsole::renderResourceCharts(
    const ImVec2& contentTopLeft,
    const ImVec2& available,
    const ImVec2& imageTopLeft,
    const ImVec2& imageSize
) const {
    if (m_frameProfiles.empty()) {
        return;
    }

    const auto gap = 4.0f * m_uiScale;
    const auto minimumHeight = RESOURCE_CHART_MINIMUM_HEIGHT * m_uiScale;
    const auto chartLeft = imageTopLeft.x;
    const auto chartRight = imageTopLeft.x + imageSize.x;
    const auto imageBottom = imageTopLeft.y + imageSize.y;
    const auto contentBottom = contentTopLeft.y + available.y;
    auto* drawList = ImGui::GetWindowDrawList();

    const ImVec2 timingMinimum{chartLeft, contentTopLeft.y};
    const ImVec2 timingMaximum{chartRight, imageTopLeft.y - gap};
    if (timingMaximum.y - timingMinimum.y >= minimumHeight) {
        drawChartBackground(drawList, timingMinimum, timingMaximum);

        const auto statistics = calculateProfileStatistics(
            m_frameProfiles,
            [](const FrameProfile& profile) {
                return profile.totalDuration;
            }
        );
        const auto targetDuration = m_game->getFps() > 0
            ? 1000.0f / static_cast<float>(m_game->getFps())
            : 0.0f;
        const auto chartScale = std::max({1.0f, targetDuration * 1.5f, statistics.peak * 1.1f});
        const auto textHeight = ImGui::GetTextLineHeight();

        char statisticsLabel[128]{};
        std::snprintf(
            statisticsLabel,
            sizeof(statisticsLabel),
            "now %.1f ms  |  avg %.1f  |  peak %.1f  |  target %.1f",
            statistics.current,
            statistics.average,
            statistics.peak,
            targetDuration
        );
        drawList->PushClipRect(timingMinimum, timingMaximum, true);
        const ImVec2 labelPosition{
            timingMinimum.x + 6.0f * m_uiScale,
            timingMinimum.y + 3.0f * m_uiScale
        };
        drawList->AddText(labelPosition, IM_COL32(226, 230, 238, 255), "CPU frame");
        auto legendX = labelPosition.x + ImGui::CalcTextSize("CPU frame").x + 12.0f * m_uiScale;
        legendX = drawChartLegendItem(drawList, legendX, labelPosition.y, textHeight, m_uiScale, "Input", INPUT_COLOR, false);
        legendX = drawChartLegendItem(drawList, legendX, labelPosition.y, textHeight, m_uiScale, "Update", UPDATE_COLOR, false);
        legendX = drawChartLegendItem(drawList, legendX, labelPosition.y, textHeight, m_uiScale, "Render", RENDER_COLOR, false);
        legendX = drawChartLegendItem(drawList, legendX, labelPosition.y, textHeight, m_uiScale, "Target", FRAME_TARGET_COLOR, true);
        const auto statisticsWidth = ImGui::CalcTextSize(statisticsLabel).x;
        const auto statisticsX = timingMaximum.x - statisticsWidth - 6.0f * m_uiScale;
        const auto statisticsOnSecondRow = statisticsX < legendX;
        const auto showStatistics = !statisticsOnSecondRow ||
            (timingMaximum.y - timingMinimum.y >= 2.0f * textHeight + 24.0f * m_uiScale &&
             statisticsWidth <= timingMaximum.x - labelPosition.x - 6.0f * m_uiScale);
        if (showStatistics) {
            drawList->AddText(
                {statisticsOnSecondRow ? labelPosition.x : statisticsX,
                 labelPosition.y + (statisticsOnSecondRow ? textHeight + 2.0f * m_uiScale : 0.0f)},
                IM_COL32(190, 197, 210, 255),
                statisticsLabel
            );
        }

        const ImVec2 plotMinimum{
            timingMinimum.x + 5.0f * m_uiScale,
            timingMinimum.y + textHeight + 7.0f * m_uiScale +
                (showStatistics && statisticsOnSecondRow ? textHeight + 2.0f * m_uiScale : 0.0f)
        };
        const ImVec2 plotMaximum{
            timingMaximum.x - 5.0f * m_uiScale,
            timingMaximum.y - 4.0f * m_uiScale
        };

        const auto plotHeight = plotMaximum.y - plotMinimum.y;
        const auto plotWidth = plotMaximum.x - plotMinimum.x;
        if (plotHeight > 0.0f && plotWidth > 0.0f) {
            const auto sampleWidth = plotWidth / static_cast<float>(FRAME_PROFILE_HISTORY_LIMIT);
            auto sampleIndex = FRAME_PROFILE_HISTORY_LIMIT - m_frameProfiles.size();
            for (const auto& profile : m_frameProfiles) {
                const auto xMinimum = plotMinimum.x + static_cast<float>(sampleIndex) * sampleWidth;
                const auto xMaximum = std::max(xMinimum + 1.0f, xMinimum + sampleWidth);
                auto yBottom = plotMaximum.y;

                const auto drawSegment = [&](const NomadInteger duration, const ImU32 color) {
                    const auto segmentHeight =
                        plotHeight * static_cast<float>(duration) / chartScale;
                    const auto yTop = std::max(plotMinimum.y, yBottom - segmentHeight);
                    if (yTop < yBottom) {
                        drawList->AddRectFilled({xMinimum, yTop}, {xMaximum, yBottom}, color);
                    }
                    yBottom = yTop;
                };

                drawSegment(profile.inputDuration, INPUT_COLOR);
                drawSegment(profile.updateDuration, UPDATE_COLOR);
                drawSegment(profile.renderDuration, RENDER_COLOR);
                ++sampleIndex;
            }

            if (targetDuration > 0.0f) {
                const auto targetY = plotMaximum.y - plotHeight * targetDuration / chartScale;
                drawList->AddLine(
                    {plotMinimum.x, targetY},
                    {plotMaximum.x, targetY},
                    FRAME_TARGET_COLOR,
                    1.0f
                );
            }
        }
        drawList->PopClipRect();
    }

    const ImVec2 memoryMinimum{chartLeft, imageBottom + gap};
    const ImVec2 memoryMaximum{chartRight, contentBottom};
    if (memoryMaximum.y - memoryMinimum.y < minimumHeight) {
        return;
    }

    drawChartBackground(drawList, memoryMinimum, memoryMaximum);
    const auto statistics = calculateProfileStatistics(
        m_frameProfiles,
        [](const FrameProfile& profile) {
            return profile.tempHeapSize;
        }
    );
    auto heapCapacity = 0.0f;
    for (const auto& profile : m_frameProfiles) {
        heapCapacity = std::max(heapCapacity, static_cast<float>(profile.maxHeapSize));
    }
    const auto chartScale = std::max(1.0f, statistics.peak * 1.1f);
    const auto usagePercent = heapCapacity > 0.0f
        ? statistics.current * 100.0f / heapCapacity
        : 0.0f;
    const auto textHeight = ImGui::GetTextLineHeight();

    const auto currentText = formatByteCount(statistics.current);
    const auto capacityText = formatByteCount(heapCapacity);
    const auto averageText = formatByteCount(statistics.average);
    const auto peakText = formatByteCount(statistics.peak);
    char label[192]{};
    std::snprintf(
        label,
        sizeof(label),
        "last %s (%.1f%% of %s)  |  avg %s  |  max %s",
        currentText.c_str(),
        usagePercent,
        capacityText.c_str(),
        averageText.c_str(),
        peakText.c_str()
    );
    drawList->PushClipRect(memoryMinimum, memoryMaximum, true);
    const ImVec2 labelPosition{
        memoryMinimum.x + 6.0f * m_uiScale,
        memoryMinimum.y + 3.0f * m_uiScale
    };
    drawList->AddText(labelPosition, IM_COL32(226, 230, 238, 255), "Temporary heap");
    auto legendX = labelPosition.x + ImGui::CalcTextSize("Temporary heap").x + 12.0f * m_uiScale;
    legendX = drawChartLegendItem(drawList, legendX, labelPosition.y, textHeight, m_uiScale, "Peak / frame", HEAP_USAGE_COLOR, true);
    const auto statisticsWidth = ImGui::CalcTextSize(label).x;
    const auto statisticsX = memoryMaximum.x - statisticsWidth - 6.0f * m_uiScale;
    const auto statisticsOnSecondRow = statisticsX < legendX;
    const auto showStatistics = !statisticsOnSecondRow ||
        (memoryMaximum.y - memoryMinimum.y >= 2.0f * textHeight + 24.0f * m_uiScale &&
         statisticsWidth <= memoryMaximum.x - labelPosition.x - 6.0f * m_uiScale);
    if (showStatistics) {
        drawList->AddText(
            {statisticsOnSecondRow ? labelPosition.x : statisticsX,
             labelPosition.y + (statisticsOnSecondRow ? textHeight + 2.0f * m_uiScale : 0.0f)},
            IM_COL32(190, 197, 210, 255),
            label
        );
    }

    const ImVec2 plotMinimum{
        memoryMinimum.x + 5.0f * m_uiScale,
        memoryMinimum.y + textHeight + 7.0f * m_uiScale +
            (showStatistics && statisticsOnSecondRow ? textHeight + 2.0f * m_uiScale : 0.0f)
    };
    const ImVec2 plotMaximum{
        memoryMaximum.x - 5.0f * m_uiScale,
        memoryMaximum.y - 4.0f * m_uiScale
    };

    const auto plotHeight = plotMaximum.y - plotMinimum.y;
    const auto plotWidth = plotMaximum.x - plotMinimum.x;
    if (plotHeight > 0.0f && plotWidth > 0.0f) {
        const auto sampleWidth = plotWidth / static_cast<float>(FRAME_PROFILE_HISTORY_LIMIT);
        auto sampleIndex = FRAME_PROFILE_HISTORY_LIMIT - m_frameProfiles.size();
        ImVec2 previousPoint{};
        bool hasPreviousPoint = false;
        for (const auto& profile : m_frameProfiles) {
            const auto x =
                plotMinimum.x + (static_cast<float>(sampleIndex) + 0.5f) * sampleWidth;
            const auto y = plotMaximum.y -
                plotHeight * static_cast<float>(profile.tempHeapSize) / chartScale;
            const ImVec2 point{x, std::max(plotMinimum.y, y)};
            drawList->AddRectFilled(
                {x - sampleWidth * 0.5f, point.y},
                {x + sampleWidth * 0.5f, plotMaximum.y},
                IM_COL32(58, 122, 109, 90)
            );
            if (hasPreviousPoint) {
                drawList->AddLine(previousPoint, point, HEAP_USAGE_COLOR, 1.5f);
            }
            previousPoint = point;
            hasPreviousPoint = true;
            ++sampleIndex;
        }
    }
    drawList->PopClipRect();
}

void DebugConsole::renderEntityInspector() {
    const auto* viewport = ImGui::GetMainViewport();
    const auto bottomY = viewport->WorkSize.y * 0.72f;
    const auto toolbarHeight = getToolbarHeight();
    setPanel(0.0f, toolbarHeight, viewport->WorkSize.x * 0.20f, bottomY - toolbarHeight);
    ImGui::Begin("Scenes and Entities", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::InputTextWithHint("##entityFilter", "Filter scenes and entities", m_entityFilter.data(), m_entityFilter.size());

    constexpr auto baseFlags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_DefaultOpen |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    auto gameFlags = baseFlags;
    if (m_selectionKind == SelectionKind::Game) {
        gameFlags |= ImGuiTreeNodeFlags_Selected;
    }

    const auto gameOpen = ImGui::TreeNodeEx("Game", gameFlags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selectGame();
    }

    if (gameOpen) {
        m_game->forEachScene([this](Scene* scene) {
            const auto sceneMatches = containsText(scene->getName(), m_entityFilter.data());
            bool childMatches = false;
            scene->forEachEntities([this, &childMatches](Entity* entity) {
                childMatches = childMatches || containsText(entity->getName(), m_entityFilter.data()) ||
                               containsText(std::to_string(entity->getId()), m_entityFilter.data());
            });
            if (!sceneMatches && !childMatches) {
                return;
            }

            ImGui::PushID(static_cast<int>(scene->getId()));
            const auto label = scene->getName() + " [" + std::to_string(scene->getId()) + "]";
            auto sceneFlags = baseFlags;
            if (m_selectionKind == SelectionKind::Scene && scene->getId() == m_selectedSceneId) {
                sceneFlags |= ImGuiTreeNodeFlags_Selected;
            }

            const auto open = ImGui::TreeNodeEx(label.c_str(), sceneFlags);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                selectScene(scene);
            }
            if (open) {
                scene->forEachEntities([this](Entity* entity) {
                    const auto id = entity->getId();
                    const auto entityLabel = entity->getName() + " [" + std::to_string(id) + "]";
                    if (!containsText(entityLabel, m_entityFilter.data())) {
                        return;
                    }

                    ImGui::PushID(static_cast<int>(id));
                    const auto selected = m_selectionKind == SelectionKind::Entity &&
                                          id == m_selectedEntityId &&
                                          entity->getScene()->getId() == m_selectedSceneId;
                    if (ImGui::Selectable(entityLabel.c_str(), selected)) {
                        selectEntity(entity);
                    }
                    ImGui::PopID();
                });
                ImGui::TreePop();
            }
            ImGui::PopID();
        });
        ImGui::TreePop();
    }

    ImGui::End();
}

void DebugConsole::renderVariableInspector() {
    const auto* viewport = ImGui::GetMainViewport();
    const auto bottomY = viewport->WorkSize.y * 0.72f;
    const auto rightWidth = viewport->WorkSize.x * 0.28f;
    const auto toolbarHeight = getToolbarHeight();
    const auto panelHeight = (bottomY - toolbarHeight) * 0.5f;
    setPanel(viewport->WorkSize.x - rightWidth, toolbarHeight, rightWidth, panelHeight);
    ImGui::Begin("Variables", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    const auto selectedScene = getSelectedScene();
    const auto selectedEntity = getSelectedEntity();

    switch (m_selectionKind) {
    case SelectionKind::Game:
        ImGui::TextUnformatted("Scope: Game");
        break;
    case SelectionKind::Scene:
        ImGui::Text(
            "Scope: Scene %s",
            selectedScene != nullptr ? selectedScene->getName().c_str() : "<deleted>"
        );
        break;
    case SelectionKind::Entity:
        ImGui::Text(
            "Scope: Entity %s",
            selectedEntity != nullptr ? selectedEntity->getName().c_str() : "<deleted>"
        );
        break;
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##variableFilter", "Filter variables", m_variableFilter.data(), m_variableFilter.size());

    const auto tableFlags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("VariableTable", 3, tableFlags)) {
        ImGui::End();
        return;
    }

    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("Value");
    ImGui::TableHeadersRow();

    ExecutionContextGuard contextGuard(m_game, selectedScene, selectedEntity);

    std::vector<VariableRow> rows;
    collectVariableRows(rows);
    std::ranges::sort(rows, {}, &VariableRow::name);

    for (const auto& row : rows) {
        ImGui::TableNextRow();
        ImGui::PushID(row.name.c_str());

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(row.name.c_str());

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(row.type != nullptr ? row.type->getTypeName().c_str() : "<unknown>");

        ImGui::TableSetColumnIndex(2);
        if (isBooleanType(row.type)) {
            auto checked = row.value == BOOLEAN_TRUE_STRING;
            ImGui::BeginDisabled(!row.editable);
            if (ImGui::Checkbox("##booleanValue", &checked)) {
                const NomadString text = checked ? BOOLEAN_TRUE_STRING : BOOLEAN_FALSE_STRING;
                if (!applyVariableEdit(row, text)) {
                    m_consoleEntries.push_back({
                        true,
                        "Could not set " + row.name + " to '" + text + "'"
                    });
                }
            }
            ImGui::EndDisabled();
        } else if (m_editingVariableName == row.name) {
            if (m_editFocusRequested) {
                ImGui::SetKeyboardFocusHere();
                m_editFocusRequested = false;
            }

            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText(
                "##editValue",
                m_editBuffer.data(),
                m_editBuffer.size(),
                ImGuiInputTextFlags_EnterReturnsTrue
            )) {
                const NomadString text = m_editBuffer.data();
                if (!applyVariableEdit(row, text)) {
                    m_consoleEntries.push_back({
                        true,
                        "Could not set " + row.name + " to '" + text + "'"
                    });
                }
                m_editingVariableName.clear();
            } else if (ImGui::IsItemDeactivated()) {
                m_editingVariableName.clear();
            }
        } else if (row.editable) {
            // Many listed variables change every frame (mouse position, entity
            // coordinates, velocity, ...). Pin the ImGui ID to the row instead
            // of the displayed value so clicking to edit stays reliable.
            const auto label = (row.value.empty() ? NomadString(" ") : row.value) + "##value";
            if (ImGui::Selectable(label.c_str())) {
                m_editingVariableName = row.name;
                m_editFocusRequested = true;
                m_editBuffer.fill('\0');
                const auto length = std::min(row.value.size(), m_editBuffer.size() - 1);
                std::copy_n(row.value.begin(), length, m_editBuffer.begin());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to edit");
            }
        } else {
            ImGui::TextUnformatted(row.value.c_str());
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
    ImGui::End();
}

void DebugConsole::collectVariableRows(std::vector<VariableRow>& rows) const {
    collectContextVariableRows(rows);
    collectDynamicVariableRows(rows);
    collectConsoleVariableRows(rows);
}

void DebugConsole::collectContextVariableRows(std::vector<VariableRow>& rows) const {
    auto* runtime = m_game->getRuntime();
    auto* selectedScene = getSelectedScene();
    auto* selectedEntity = getSelectedEntity();

    for (
        auto contextId = runtime->getFirstVariableContextId();
        contextId != NOMAD_INVALID_ID;
        contextId = runtime->getNextVariableContextId(contextId)
    ) {
        const auto contextName = runtime->getContextName(contextId);

        switch (m_selectionKind) {
        case SelectionKind::Game:
            if (contextName == SCENE_VARIABLE_CONTEXT ||
                contextName == THIS_ENTITY_VARIABLE_CONTEXT ||
                contextName == OTHER_ENTITY_VARIABLE_CONTEXT) {
                continue;
            }
            break;
        case SelectionKind::Scene:
            if (contextName != SCENE_VARIABLE_CONTEXT || selectedScene == nullptr) {
                continue;
            }
            break;
        case SelectionKind::Entity:
            if (contextName != THIS_ENTITY_VARIABLE_CONTEXT || selectedEntity == nullptr) {
                continue;
            }
            break;
        }

        auto* context = runtime->getVariableContext(contextId);
        for (
            auto variableId = context->getFirstVariableId();
            variableId != NOMAD_INVALID_ID;
            variableId = context->getNextVariableId(variableId)
        ) {
            const auto* type = context->getVariableType(variableId);
            if (type == nullptr) {
                continue;
            }

            // Context variables are registered under their fully qualified
            // name (`global.font.default`), so the context name is not prepended.
            const auto name = context->getVariableName(variableId);
            if (!containsText(name, m_variableFilter.data())) {
                continue;
            }

            RuntimeValue value;
            if (contextName == SCENE_VARIABLE_CONTEXT) {
                selectedScene->getVariableValue(variableId, value);
            } else if (contextName == THIS_ENTITY_VARIABLE_CONTEXT) {
                selectedEntity->getVariableValue(variableId, value);
            } else {
                context->getValue(variableId, value);
            }

            rows.push_back({
                name,
                type,
                valueToDisplayString(type, value),
                VariableRow::Source::Context,
                contextId,
                variableId,
                isEditableType(type),
            });
        }
    }
}

void DebugConsole::collectDynamicVariableRows(std::vector<VariableRow>& rows) const {
    auto* runtime = m_game->getRuntime();

    // Dynamic getters re-evaluate live game state and may warn when that state
    // is unavailable. Keep those warnings out of the log while inspecting.
    LogSuppressionGuard logGuard;

    for (
        auto variableId = runtime->getFirstDynamicVariableId();
        variableId != NOMAD_INVALID_ID;
        variableId = runtime->getNextDynamicVariableId(variableId)
    ) {
        if (!runtime->canGetDynamicVariable(variableId)) {
            continue;
        }

        const auto name = runtime->getDynamicVariableName(variableId);
        const auto isSceneVariable = name.rfind("scene.", 0) == 0 || name.rfind("select.", 0) == 0;
        const auto isEntityVariable = name.rfind("this", 0) == 0;
        const auto isOtherVariable = name.rfind("other", 0) == 0;

        switch (m_selectionKind) {
        case SelectionKind::Game:
            if (isSceneVariable || isEntityVariable || isOtherVariable) {
                continue;
            }
            break;
        case SelectionKind::Scene:
            if (!isSceneVariable) {
                continue;
            }
            break;
        case SelectionKind::Entity:
            if (!isEntityVariable) {
                continue;
            }
            break;
        }

        if (!containsText(name, m_variableFilter.data())) {
            continue;
        }

        const auto* type = runtime->getDynamicVariableType(variableId);
        RuntimeValue value;
        if (type != nullptr && type->isString()) {
            value.setNullStringValue();
        }

        runtime->getDynamicVariableValue(m_inspectionVirtualMachine.get(), variableId, value);

        rows.push_back({
            name,
            type,
            valueToDisplayString(type, value),
            VariableRow::Source::Dynamic,
            NOMAD_INVALID_ID,
            variableId,
            runtime->canSetDynamicVariable(variableId) && isEditableType(type),
        });

        if (type != nullptr && type->isString()) {
            type->freeValue(value);
        }
    }
}

void DebugConsole::collectConsoleVariableRows(std::vector<VariableRow>& rows) const {
    if (m_selectionKind != SelectionKind::Game) {
        return;
    }

    for (const auto& variable : m_interpreter.listVariables()) {
        const auto name = "console." + variable.name;
        if (!containsText(name, m_variableFilter.data())) {
            continue;
        }

        rows.push_back({
            name,
            variable.type,
            variable.value,
            VariableRow::Source::Console,
            NOMAD_INVALID_ID,
            NOMAD_INVALID_ID,
            isEditableType(variable.type),
        });
    }
}

bool DebugConsole::applyVariableEdit(const VariableRow& row, const NomadString& text) {
    if (!row.editable || row.type == nullptr) {
        return false;
    }

    auto* runtime = m_game->getRuntime();

    // Dynamic string variables have a dedicated setter that owns the copy, so
    // no temporary allocation is needed for them.
    if (row.source == VariableRow::Source::Dynamic && row.type->isString()) {
        runtime->setStringDynamicVariable(m_inspectionVirtualMachine.get(), row.variableId, text);
        return true;
    }

    RuntimeValue value;
    if (!parseValue(row.type, text, value)) {
        return false;
    }

    switch (row.source) {
    case VariableRow::Source::Context: {
        const auto contextName = runtime->getContextName(row.contextId);

        // Variable storage copies what it keeps, so the parsed value is released here.
        auto applied = true;
        if (contextName == SCENE_VARIABLE_CONTEXT) {
            if (auto* scene = getSelectedScene()) {
                scene->setVariableValue(row.variableId, value);
            } else {
                applied = false;
            }
        } else if (contextName == THIS_ENTITY_VARIABLE_CONTEXT) {
            if (auto* entity = getSelectedEntity()) {
                entity->setVariableValue(row.variableId, value);
            } else {
                applied = false;
            }
        } else {
            runtime->getVariableContext(row.contextId)->setValue(row.variableId, value);
        }

        row.type->freeValue(value);
        return applied;
    }
    case VariableRow::Source::Dynamic:
        runtime->setDynamicVariable(m_inspectionVirtualMachine.get(), row.variableId, value);
        return true;
    case VariableRow::Source::Console: {
        // `Interpreter` copies the value, so the temporary is released here.
        const auto name = row.name.substr(std::strlen("console."));
        m_interpreter.setVariable(name, row.type, value);
        row.type->freeValue(value);
        return true;
    }
    }

    row.type->freeValue(value);
    return false;
}

void DebugConsole::renderFunctionInspector() {
    const auto* viewport = ImGui::GetMainViewport();
    const auto bottomY = viewport->WorkSize.y * 0.72f;
    const auto rightWidth = viewport->WorkSize.x * 0.28f;
    const auto toolbarHeight = getToolbarHeight();
    const auto panelHeight = (bottomY - toolbarHeight) * 0.5f;
    setPanel(
        viewport->WorkSize.x - rightWidth,
        toolbarHeight + panelHeight,
        rightWidth,
        panelHeight
    );
    ImGui::Begin("Functions", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::InputTextWithHint("##functionFilter", "Filter functions", m_functionFilter.data(), m_functionFilter.size());

    std::vector<Function*> functions;
    m_game->getRuntime()->getFunctions(functions);
    std::ranges::sort(functions, {}, &Function::getName);

    if (ImGui::BeginCombo(
        "##function",
        m_selectedFunctionId != NOMAD_INVALID_ID
            ? m_game->getRuntime()->getFunction(m_selectedFunctionId)->getName().c_str()
            : "<select function>"
    )) {
        for (const auto* function : functions) {
            if (!containsText(function->getName(), m_functionFilter.data())) {
                continue;
            }
            if (ImGui::Selectable(function->getName().c_str(), function->getId() == m_selectedFunctionId)) {
                m_selectedFunctionId = function->getId();
            }
        }
        ImGui::EndCombo();
    }

    if (m_selectedFunctionId != NOMAD_INVALID_ID) {
        const auto* function = m_game->getRuntime()->getFunction(m_selectedFunctionId);
        ImGui::TextWrapped("%s", function->getPath().c_str());
        ImGui::Text(
            "Parameters: %zu | Variables: %zu | Instructions: %zu-%zu | Return: %s",
            function->getParameterCount(),
            function->getVariableCount(),
            function->getFunctionStart(),
            function->getFunctionEnd(),
            function->getReturnType() != nullptr ? function->getReturnType()->getTypeName().c_str() : "<unknown>"
        );
        ImGui::Separator();
        ImGui::BeginChild("FunctionSource");
        ImGui::TextUnformatted(function->getSource().c_str());
        ImGui::EndChild();
    }

    ImGui::End();
}

void DebugConsole::renderConsole() {
    const auto* viewport = ImGui::GetMainViewport();
    const auto y = viewport->WorkSize.y * 0.72f;
    const auto width = viewport->WorkSize.x * m_consoleSplitFraction;
    setPanel(0.0f, y, width, viewport->WorkSize.y - y);
    ImGui::Begin("Console", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    ImGui::BeginChild("ConsoleOutput", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true);
    for (const auto& entry : m_consoleEntries) {
        if (entry.error) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", entry.text.c_str());
        } else {
            ImGui::TextUnformatted(entry.text.c_str());
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText(
        "##nativeFunction",
        m_nativeFunctionBuffer.data(),
        m_nativeFunctionBuffer.size(),
        ImGuiInputTextFlags_EnterReturnsTrue
    )) {
        executeNativeFunction();
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::End();
}

void DebugConsole::renderConsoleSplitter() {
    const auto* viewport = ImGui::GetMainViewport();
    const auto y = viewport->WorkSize.y * 0.72f;
    const auto height = viewport->WorkSize.y - y;
    const auto thickness = CONSOLE_SPLITTER_THICKNESS * m_uiScale;
    const auto x = viewport->WorkSize.x * m_consoleSplitFraction;

    setPanel(x, y, thickness, height);
    // ImGui clamps window size to style.WindowMinSize, which is far wider than
    // the splitter, so relax it (and the padding/border) for this window only.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
    ImGui::Begin(
        "##consoleSplitter",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
    );

    ImGui::InvisibleButton("##grip", ImVec2(thickness, height));

    const auto dragging = ImGui::IsItemActive();
    if (dragging || ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (dragging && viewport->WorkSize.x > 0.0f) {
        const auto delta = ImGui::GetIO().MouseDelta.x;
        m_consoleSplitFraction = std::clamp(
            m_consoleSplitFraction + delta / viewport->WorkSize.x,
            CONSOLE_SPLIT_MINIMUM,
            CONSOLE_SPLIT_MAXIMUM
        );
    }

    const auto gripColor = ImGui::GetColorU32(
        dragging ? ImGuiCol_SeparatorActive :
        ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator
    );
    const auto topLeft = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        topLeft,
        ImVec2(topLeft.x + thickness, topLeft.y + height),
        gripColor
    );

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void DebugConsole::renderLogs() {
    const auto* viewport = ImGui::GetMainViewport();
    const auto y = viewport->WorkSize.y * 0.72f;
    const auto x = viewport->WorkSize.x * m_consoleSplitFraction + CONSOLE_SPLITTER_THICKNESS * m_uiScale;
    setPanel(x, y, viewport->WorkSize.x - x, viewport->WorkSize.y - y);
    ImGui::Begin("Logs", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    if (ImGui::Button("Clear")) {
        m_memorySink.clear();
    }
    ImGui::SameLine();
    ImGui::Text("%zu entries", m_memorySink.getEntries().size());
    ImGui::Separator();

    ImGui::BeginChild("LogEntries");
    for (const auto& entry : m_memorySink.getEntries()) {
        ImVec4 color;
        switch (entry.level) {
        case LogLevel::Debug:
            color = {0.60f, 0.60f, 0.60f, 1.0f};
            break;
        case LogLevel::Info:
            color = {0.90f, 0.90f, 0.90f, 1.0f};
            break;
        case LogLevel::Warning:
            color = {1.0f, 0.80f, 0.20f, 1.0f};
            break;
        case LogLevel::Error:
        case LogLevel::Fatal:
            color = {1.0f, 0.30f, 0.30f, 1.0f};
            break;
        }
        ImGui::TextColored(color, "%s", entry.message.c_str());
    }
    ImGui::EndChild();

    ImGui::End();
}

void DebugConsole::executeNativeFunction() {
    const NomadString nativeFunction = m_nativeFunctionBuffer.data();
    if (nativeFunction.empty()) {
        return;
    }

    m_nativeFunctionHistory.push_back(nativeFunction);
    m_consoleEntries.push_back({false, "> " + nativeFunction});

    ExecutionContextGuard contextGuard(m_game, getSelectedScene(), getSelectedEntity());
    if (!m_interpreter.execute(nativeFunction)) {
        const auto* error = m_interpreter.getError();
        m_consoleEntries.push_back({true, error != nullptr ? *error : "NativeFunction failed"});
    } else if (const auto* result = m_interpreter.getResult(); result != nullptr) {
        if (!result->getType()->isVoid()) {
            m_consoleEntries.push_back({
                false,
                result->getType()->toString(result->getValue())
            });
        }
    }

    m_nativeFunctionBuffer.fill('\0');
}

Scene* DebugConsole::getSelectedScene() const {
    return m_selectedSceneId != NOMAD_INVALID_ID
        ? m_game->getSceneById(m_selectedSceneId)
        : nullptr;
}

Entity* DebugConsole::getSelectedEntity() const {
    const auto* scene = getSelectedScene();
    return scene != nullptr && m_selectedEntityId != NOMAD_INVALID_ID
        ? scene->getEntityById(m_selectedEntityId)
        : nullptr;
}

void DebugConsole::selectGame() {
    m_selectionKind = SelectionKind::Game;
    m_selectedSceneId = NOMAD_INVALID_ID;
    m_selectedEntityId = NOMAD_INVALID_ID;
    m_editingVariableName.clear();
}

void DebugConsole::selectScene(Scene* scene) {
    if (scene == nullptr) {
        selectGame();
        return;
    }

    m_selectionKind = SelectionKind::Scene;
    m_selectedSceneId = scene->getId();
    m_selectedEntityId = NOMAD_INVALID_ID;
    m_editingVariableName.clear();
}

void DebugConsole::selectEntity(Entity* entity) {
    if (entity == nullptr) {
        selectGame();
        return;
    }

    m_selectionKind = SelectionKind::Entity;
    m_selectedSceneId = entity->getScene()->getId();
    m_selectedEntityId = entity->getId();
    m_editingVariableName.clear();
}

} // namespace nomad
