// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <functional>
#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

enum class SystemEvent
{
    Update = 1,
    PostUpdate,
    Render,
    BeginCollision,
    EndCollision,
    BeginCollisionWith,
    EndCollisionWith,
    EnterCamera,
    ExitCamera,
    User = 1000,
};

struct EventRegistration {
    SystemEvent eventId;
    NomadId functionId;
    NomadString name;
    NomadInteger mask;
};

class EventRegistrationManager {
public:
    EventRegistrationManager() = default;
    EventRegistrationManager(const EventRegistrationManager&) = delete;
    ~EventRegistrationManager() = default;

    void registerSystemEvent(SystemEvent event, NomadId functionId);
    void unregisterSystemEvent(SystemEvent event);
    void registerUserEvent(const NomadString& name, NomadId functionId);
    void unregisterMaskEvent(SystemEvent event, NomadInteger mask);
    void registerMaskEvent(SystemEvent event, NomadId functionId, NomadInteger mask);
    void unregisterUserEvent(const NomadString& name);

    [[nodiscard]]
    NomadId getFunctionIdForSystemEvent(SystemEvent event) const;
    [[nodiscard]]
    NomadId getFunctionIdForUserEvent(const NomadString& name) const;
    [[nodiscard]]
    NomadId getFunctionIdForMaskEvent(SystemEvent event, NomadInteger mask) const;

    void enumerateRegistrations(const std::function<void(const EventRegistration&)>& callback) const;

private:
    std::vector<EventRegistration> m_registrations;
};

} // nomad
