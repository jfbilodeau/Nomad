// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

namespace nomad {

struct HubId;
struct ModuleContext;

void registerLogConfigurationFunctions(ModuleContext* context, HubId hubId);

} // namespace nomad
