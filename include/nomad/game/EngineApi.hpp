// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

namespace nomad {

class Runtime;

// Registers engine API signatures and documentation without executable callbacks.
void registerEngineApi(Runtime* runtime);

} // namespace nomad
