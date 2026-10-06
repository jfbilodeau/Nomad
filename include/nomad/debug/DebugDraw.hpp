// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <box2d/box2d.h>

namespace nomad {

// Forward declarations
class Canvas;

void createDebugDraw(Canvas* canvas, b2DebugDraw* debugDraw);

} // namespace nomad
