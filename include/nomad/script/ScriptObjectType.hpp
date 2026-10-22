// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ScriptObjectType final : public ObjectType {
public:
    explicit ScriptObjectType(NomadString name, bool open = true, NomadString documentation = {});
    [[nodiscard]] bool isOpen() const override;

private:
    bool m_open;
};

} // namespace nomad
