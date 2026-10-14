// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/system/String.hpp>

namespace nomad {

class ResourceManager;

class ResourceException : public NomadException {
public:
    explicit ResourceException(const NomadString& message) : NomadException(message) {}
};

class Resource {
public:
    explicit Resource(const NomadString& name);
    Resource(const Resource& other) = delete;
    virtual ~Resource() = default;

    [[nodiscard]] const NomadString& getName() const { return m_name; }

private:
    NomadString m_name;
};

} // nomad
