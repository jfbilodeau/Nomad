// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Identifier.hpp>

#include <nomad/script/Variable.hpp>

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

// Forward declarations
class VirtualMachine;

class FormatString {
    enum class SegmentType {
        Literal = 1,
        Variable,
    };

    struct Segment {
        SegmentType type;
        IdentifierType variableType;
        const Type* valueType;
        NomadId variableId;
        NomadString value;
    };

public:
    explicit FormatString(const NomadString& formatString, NomadId functionId);
    ~FormatString();

    [[nodiscard]] const NomadString& getFormatString() const {
        return m_formatString;
    }

    [[nodiscard]] NomadId getFunctionId() const {
        return m_functionId;
    }

    void addLiteral(const NomadString& literal);
    void addVariable(IdentifierType identifierType, const Type* valueType, NomadId variableId);

    NomadString format(VirtualMachine* interpreter) const;

private:
    NomadString m_formatString;
    NomadId m_functionId;
    std::vector<Segment> m_segments;
};

} // nomad
