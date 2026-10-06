// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/debug/DebugDraw.hpp>

#include <nomad/game/Canvas.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/system/TempHeap.hpp>

#include <imgui.h>
#include <SDL3/SDL.h>

#include <cstdint>

namespace nomad {

namespace {

SDL_Color toSdlColor(const b2HexColor color) {
    const auto packedColor = static_cast<std::uint32_t>(color);

    return {
        .r = static_cast<Uint8>((packedColor >> 16) & 0xffU),
        .g = static_cast<Uint8>((packedColor >> 8) & 0xffU),
        .b = static_cast<Uint8>(packedColor & 0xffU),
        .a = 0xffU
    };
}

void debug_draw_polygon(
    // b2WorldTransform transform,
    const b2Vec2* vertices,
    int vertex_count,
    b2HexColor color,
    void* context
) {
    const auto canvas = static_cast<Canvas*>(context);

    const auto offset = canvas->getOffset();

    auto temp_vertices = createTempVector<SDL_FPoint>();

    const auto sdlColor = toSdlColor(color);

    for (int i = 0; i < vertex_count; ++i) {
        auto& v = vertices[i];

        temp_vertices.push_back(SDL_FPoint{
            v.x + static_cast<float>(offset.getX()),
            v.y + static_cast<float>(offset.getY()),
        });
    }

    // Close polygon
    temp_vertices.push_back(SDL_FPoint{
        vertices[0].x + static_cast<float>(offset.getX()),
        vertices[0].y  + static_cast<float>(offset.getY())
    });

    auto renderer = canvas->getSdlRenderer();

    SDL_SetRenderDrawColor(
        renderer,
        sdlColor.r,
        sdlColor.g,
        sdlColor.b,
        sdlColor.a
    );

    SDL_RenderLines(
        renderer,
        temp_vertices.data(),
        static_cast<int>(temp_vertices.size())
    );
}

void debug_draw_solid_polygon(
    b2Transform /*transform*/,
    const b2Vec2* vertices,
    const int vertex_count,
    float /*radius*/,
    b2HexColor color,
    void* context
) {
    const auto canvas = static_cast<Canvas*>(context);

    const auto offset = canvas->getOffset();

    auto tempVertices = createTempVector<SDL_Vertex>();

    const auto sdlColor = toSdlColor(color);
    const auto sdlColorF = SDL_FColor{
        sdlColor.r / 255.0f,
        sdlColor.g / 255.0f,
        sdlColor.b / 255.0f,
        sdlColor.a / 255.0f
    };

    for (int i = 0; i < vertex_count; ++i) {
        const auto&[x, y] = vertices[i];

        tempVertices.push_back(SDL_Vertex{
            {
                x + static_cast<float>(offset.getX()),
                y + static_cast<float>(offset.getY())
            },
            SDL_FColor {
                sdlColorF,
            },
            {0, 0}  // Texture coordinates
        });
    }

    SDL_RenderGeometry(
        canvas->getSdlRenderer(),
        nullptr, // No texture
        tempVertices.data(),
        static_cast<int>(tempVertices.size()),
        nullptr, // No indices
        0 // No indices count
    );
}

void debug_draw_circle(
    b2Vec2 center,
    float radius,
    b2HexColor color,
    void* context
) {
    const auto canvas = static_cast<Canvas*>(context);

    auto offset = canvas->getOffset();

    const auto renderer = canvas->getSdlRenderer();
    const auto sdlColor = toSdlColor(color);

    constexpr auto vertex_count = 32;

    auto temp_vertices = createTempVector<SDL_FPoint>();

    center.x += static_cast<float>(offset.getX());
    center.y += static_cast<float>(offset.getY());

    for (int i = 0; i <= vertex_count; ++i) {  // Using '<=' (+1) to close the circle
        const auto angle = 2.0f * static_cast<float>(NOMAD_PI) * static_cast<float>(i) / vertex_count;
        const auto x = center.x + radius * cosf(angle);
        const auto y = center.y + radius * sinf(angle);
        temp_vertices.push_back(SDL_FPoint{x, y});
    }

    SDL_SetRenderDrawColor(
        renderer,
        sdlColor.r,
        sdlColor.g,
        sdlColor.b,
        sdlColor.a
    );

    SDL_RenderLines(
        renderer,
        temp_vertices.data(),
        static_cast<int>(temp_vertices.size())
    );
}

void debug_draw_solid_circle(
    b2Transform transform,
    // b2Vec2 center,
    float radius,
    b2HexColor color,
    void* context
) {
    // Temporary cheat
    debug_draw_circle(transform.p, radius, color, context);
}

void debug_draw_solid_capsule(
    b2Vec2 /*p1*/,
    b2Vec2 /*p2*/,
    float /*radius*/,
    b2HexColor /*color*/,
    void* /*context*/
) {
    log::warning("debug_draw_solid_capsule() called but Box2D capsule shapes are not used.");
}

void debug_draw_transform(
    b2Transform /*transform*/,
    void* /*context*/
) {
    log::warning("debug_draw_transform() not implemented");
}

void debug_draw_point(
    b2Vec2 p,
    float /*size*/,
    b2HexColor color,
    void* context
) {
    auto canvas = static_cast<Canvas*>(context);

    auto offset = canvas->getOffset();

    auto renderer = canvas->getSdlRenderer();

    const auto sdlColor = toSdlColor(color);

    SDL_SetRenderDrawColor(
        renderer,
        sdlColor.r,
        sdlColor.g,
        sdlColor.b,
        sdlColor.a
    );

    SDL_RenderPoint(
        renderer,
        p.x + static_cast<float>(offset.getX()),
        p.y + static_cast<float>(offset.getY())
    );
}

void debug_draw_string(
    b2Vec2 /*p*/,
    const char* /*s*/,
    b2HexColor /*color*/,
    void* /*context*/
) {
    // Ignore...
}

// void bounds(
//     b2AABB aabb,
//     void* context
// ) {
//     log::warning("bounds() not implemented");
// }

} // namespace (anonymous)

void createDebugDraw(nomad::Canvas* canvas, b2DebugDraw* debugDraw) {
    debugDraw->DrawPolygonFcn = debug_draw_polygon;
    debugDraw->DrawSolidPolygonFcn = debug_draw_solid_polygon;
    debugDraw->DrawCircleFcn = debug_draw_circle;
    debugDraw->DrawSolidCircleFcn = debug_draw_solid_circle;
    debugDraw->DrawSolidCapsuleFcn = debug_draw_solid_capsule;
    debugDraw->DrawTransformFcn = debug_draw_transform;
    debugDraw->DrawPointFcn = debug_draw_point;
    debugDraw->DrawStringFcn = debug_draw_string;
    debugDraw->drawingBounds = {
        {0, 0},
        {0, 0}
    };
    // debugDraw->useDrawingBounds = false;
    debugDraw->drawShapes = true;
    debugDraw->drawJoints = true;
    debugDraw->drawJointExtras = false;
    debugDraw->drawBounds = true;
    debugDraw->drawMass = false;
    debugDraw->drawContacts = false;
    debugDraw->drawGraphColors = false;
    debugDraw->drawContactNormals = false;
    // debugDraw->drawContactImpulses = false;
    // debugDraw->drawFrictionImpulses = false;
    debugDraw->context = canvas;
}

} // namespace nomad
