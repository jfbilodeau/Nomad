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
    CallableId functionId;
    NomadString name;
    NomadInteger mask;
};

class EventRegistrationManager {
public:
    EventRegistrationManager() = default;
    EventRegistrationManager(const EventRegistrationManager&) = delete;
    ~EventRegistrationManager() = default;

    void registerSystemEvent(SystemEvent event, CallableId functionId);
    void unregisterSystemEvent(SystemEvent event);
    void registerUserEvent(const NomadString& name, CallableId functionId);
    void unregisterMaskEvent(SystemEvent event, NomadInteger mask);
    void registerMaskEvent(SystemEvent event, CallableId functionId, NomadInteger mask);
    void unregisterUserEvent(const NomadString& name);

    [[nodiscard]]
    CallableId getFunctionIdForSystemEvent(SystemEvent event) const;
    [[nodiscard]]
    CallableId getFunctionIdForUserEvent(const NomadString& name) const;
    [[nodiscard]]
    CallableId getFunctionIdForMaskEvent(SystemEvent event, NomadInteger mask) const;

    void enumerateRegistrations(const std::function<void(const EventRegistration&)>& callback) const;

private:
    std::vector<EventRegistration> m_registrations;
};

} // nomad
