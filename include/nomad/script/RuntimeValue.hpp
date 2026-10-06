// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_RUNTIME_VALUE_HPP
#define NOMAD_RUNTIME_VALUE_HPP

#include <nomad/Nomad.hpp>

#include <type_traits>

namespace nomad {

const NomadString FUNCTION_INTERNAL_NAME_PREFIX = "$";

class RuntimeValue {
public:
    RuntimeValue();
    explicit RuntimeValue(NomadFloat value);
    explicit RuntimeValue(NomadInteger value);
    explicit RuntimeValue(NomadIndex value);
    explicit RuntimeValue(NomadBoolean value);
    explicit RuntimeValue(NomadId value);
    explicit RuntimeValue(const NomadString& value);
    ~RuntimeValue() = default;

    void setFloatValue(NomadFloat value);
    void setIntegerValue(NomadInteger value);
    void setIndexValue(NomadIndex value);
    void setBooleanValue(NomadBoolean value);
    void setStringValue(const NomadString& value);
    void setIdValue(NomadId value);
    void setStringValue(const NomadChar* value);
    void setNullStringValue();
    // Store a borrowed pointer (`$stringref`). The value does not own the string and must never be freed.
    void setStringRefValue(const NomadChar* value);

    // Overloaded setter
    void set(NomadFloat value);
    void set(NomadInteger value);
    void set(NomadBoolean value);
    void set(const NomadChar* value);
    void set(const NomadString& value);

    void copyStringValue(const RuntimeValue& other);

    // Overloaded getter
    [[nodiscard]] NomadFloat getFloatValue() const;
    [[nodiscard]] NomadInteger getIntegerValue() const;
    [[nodiscard]] NomadIndex getIndexValue() const;
    [[nodiscard]] NomadBoolean getBooleanValue() const;
    [[nodiscard]] NomadId getIdValue() const;
    [[nodiscard]] const NomadChar* getStringValue() const;

    void initStringValue();
    void initStringValue(const NomadString& value);

    void moveStringValue(RuntimeValue& other);

    void freeStringValue();

    // Number of owned string buffers currently allocated. Only tracked in debug builds; always 0 otherwise.
    [[nodiscard]] static NomadInteger getLiveStringCount();

private:
    union {
        NomadInteger m_integerValue;
        NomadFloat m_floatValue;
        NomadIndex m_indexValue;
        NomadBoolean m_booleanValue;
        NomadId m_idValue;
        const NomadChar* m_stringValue;
    };
};

static_assert(std::is_trivially_copyable_v<RuntimeValue>, "RuntimeValue must be trivially copyable for bitwise copy");

} // namespace nomad

#endif // NOMAD_RUNTIME_VALUE_HPP
