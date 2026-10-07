// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

namespace nomad {

struct GameOptions;

void parseCommandLine(int argc, char** argv, GameOptions* options);
int run(int argc, char** argv);

} // nomad
