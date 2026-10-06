// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/EventRegistration.hpp>

namespace nomad {

void EventRegistrationManager::registerSystemEvent(SystemEvent event, NomadId functionId) {
    unregisterSystemEvent(event);

    m_registrations.emplace_back(event, functionId);
}

void EventRegistrationManager::unregisterSystemEvent(SystemEvent event) {
    std::erase_if(
        m_registrations,
        [event](const EventRegistration& registration) {
            return registration.eventId == event;
        }
    );
}

void EventRegistrationManager::registerUserEvent(const NomadString &name, NomadId functionId) {
    unregisterUserEvent(name);

    m_registrations.emplace_back(SystemEvent::User, functionId, name);
}

void EventRegistrationManager::unregisterUserEvent(const NomadString &name) {
    std::erase_if(
        m_registrations,
        [name](const EventRegistration& registration) {
            return registration.eventId == SystemEvent::User && registration.name == name;
        }
    );
}

NomadId EventRegistrationManager::getFunctionIdForSystemEvent(SystemEvent event) const {
    for (const auto& registration : m_registrations) {
        if (registration.eventId == event && registration.mask == 0) {
            return registration.functionId;
        }
    }

    return NOMAD_INVALID_ID;
}

NomadId EventRegistrationManager::getFunctionIdForUserEvent(const NomadString &name) const {
    for (const auto& registration : m_registrations) {
        if (registration.eventId == SystemEvent::User && registration.name == name) {
            return registration.functionId;
        }
    }

    return NOMAD_INVALID_ID;
}

NomadId EventRegistrationManager::getFunctionIdForMaskEvent(SystemEvent event, NomadInteger mask) const {
    for (const auto& registration : m_registrations) {
        if (registration.eventId == event && registration.mask == mask) {
            return registration.functionId;
        }
    }

    return NOMAD_INVALID_ID;
}

void EventRegistrationManager::registerMaskEvent(SystemEvent event, NomadId functionId, NomadInteger mask) {
    unregisterMaskEvent(event, mask);

    m_registrations.emplace_back(event, functionId, NOMAD_EMPTY_STRING, mask);
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
