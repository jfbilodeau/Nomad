// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Identifier.hpp>

#include <nomad/script/Variable.hpp>

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

// Forward declarations
class VirtualMachine;
class InterpreterContext;

struct DynamicFormatDiagnostic {
    NomadString message;
    NomadString sourceName;
    NomadIndex line;
    NomadIndex column;
    NomadIndex offset;
    NomadIndex length;
};

struct DynamicFormatResult {
    NomadString text;
    std::vector<DynamicFormatDiagnostic> diagnostics;
};

// Syntax/resolution failures produce inline markers and logged diagnostics; execution faults propagate.
[[nodiscard]] DynamicFormatResult formatDynamicString(NomadStringView format, InterpreterContext& context,
    NomadStringView sourceName, NomadIndex line, NomadIndex column);

class FormatString {
    enum class SegmentType {
        Literal = 1,
        Variable,
        Expression,
    };

    struct Segment {
        SegmentType type;
        IdentifierType variableType;
        const Type* valueType;
        NomadId variableId;
        CallableId functionId;
        NomadString value;
    };

public:
    explicit FormatString(const NomadString& formatString, CallableId functionId);
    ~FormatString();

    [[nodiscard]] const NomadString& getFormatString() const {
        return m_formatString;
    }

    [[nodiscard]] CallableId getFunctionId() const {
        return m_functionId;
    }

    void addLiteral(const NomadString& literal);
    void addVariable(IdentifierType identifierType, const Type* valueType, NomadId variableId);
    void addFunction(const Type* valueType, CallableId functionId);
    void addExpression(const Type* valueType, CallableId functionId);

    NomadString format(VirtualMachine* interpreter) const;

private:
    NomadString m_formatString;
    CallableId m_functionId;
    std::vector<Segment> m_segments;
};

} // nomad
