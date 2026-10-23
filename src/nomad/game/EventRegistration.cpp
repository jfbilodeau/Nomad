// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/EventRegistration.hpp>

#include "nomad/script/Callable.hpp"

namespace nomad {

void EventRegistrationManager::registerSystemEvent(SystemEvent event, CallableId callableId) {
    unregisterSystemEvent(event);

    m_registrations.emplace_back(event, callableId);
}

void EventRegistrationManager::unregisterSystemEvent(SystemEvent event) {
    std::erase_if(
        m_registrations,
        [event](const EventRegistration& registration) {
            return registration.eventId == event;
        }
    );
}

void EventRegistrationManager::registerUserEvent(const NomadString &name, CallableId callableId) {
    unregisterUserEvent(name);

    m_registrations.emplace_back(SystemEvent::User, callableId, name);
}

void EventRegistrationManager::unregisterUserEvent(const NomadString &name) {
    std::erase_if(
        m_registrations,
        [name](const EventRegistration& registration) {
            return registration.eventId == SystemEvent::User && registration.name == name;
        }
    );
}

CallableId EventRegistrationManager::getFunctionIdForSystemEvent(const SystemEvent event) const {
    for (const auto& registration : m_registrations) {
        if (registration.eventId == event && registration.mask == 0) {
            return registration.functionId;
        }
    }

    return CallableId::INVALID;
}

CallableId EventRegistrationManager::getFunctionIdForUserEvent(const NomadString &name) const {
    for (const auto& registration : m_registrations) {
        if (registration.eventId == SystemEvent::User && registration.name == name) {
            return registration.functionId;
        }
    }

    return CallableId::INVALID;
}

CallableId EventRegistrationManager::getFunctionIdForMaskEvent(SystemEvent event, NomadInteger mask) const {
    for (const auto& registration : m_registrations) {
        if (registration.eventId == event && registration.mask == mask) {
            return registration.functionId;
        }
    }

    return CallableId::INVALID;
}

void EventRegistrationManager::registerMaskEvent(SystemEvent event, CallableId callableId, NomadInteger mask) {
    unregisterMaskEvent(event, mask);

    m_registrations.emplace_back(event, callableId, NOMAD_EMPTY_STRING, mask);
}

void EventRegistrationManager::unregisterMaskEvent(SystemEvent event, NomadInteger mask) {
    std::erase_if(
        m_registrations,
        [event, mask](const EventRegistration& registration) {
            return registration.eventId == event && registration.mask == mask;
        }
    );
}

void EventRegistrationManager::enumerateRegistrations(const std::function<void(const EventRegistration &)> &callback) const {
    for (const auto& registration : m_registrations) {
        callback(registration);
    }
}

} // nomad
