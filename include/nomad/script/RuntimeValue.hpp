// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_RUNTIME_VALUE_HPP
#define NOMAD_RUNTIME_VALUE_HPP

#include <nomad/Nomad.hpp>

#include <nomad/script/NomadObject.hpp>

#include <type_traits>

namespace nomad {

class NomadObject;
class NomadStringObject;

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
    // Stores the object without changing its reference count: the value takes over the caller's reference.
    void setObjectValue(NomadObject* value);

    // Overloaded setter
    void set(NomadFloat value);
    void set(NomadInteger value);
    void set(NomadBoolean value);
    void set(const NomadChar* value);
    void set(const NomadString& value);

    // Adds a reference to the object held by `other` and stores it. The previous value is not released.
    void copyObjectValue(const RuntimeValue& other);

    // Overloaded getter
    [[nodiscard]] NomadFloat getFloatValue() const;
    [[nodiscard]] NomadInteger getIntegerValue() const;
    [[nodiscard]] NomadIndex getIndexValue() const;
    [[nodiscard]] NomadBoolean getBooleanValue() const;
    [[nodiscard]] NomadId getIdValue() const;
    [[nodiscard]] NomadObject* getObjectValue() const;
    template<typename Object>
    [[nodiscard]] Object* asObject() const;
    [[nodiscard]] NomadStringObject* getStringObject() const;
    [[nodiscard]] const NomadChar* getStringValue() const;

    void initStringValue();
    void initStringValue(const NomadString& value);

    // Transfers the object from `other`, which is left null.
    void moveObjectValue(RuntimeValue& other);

    void freeObjectValue();

    // Number of non-empty string objects currently alive. Only tracked in debug builds; always 0 otherwise.
    [[nodiscard]] static NomadInteger getLiveStringCount();

private:
    union {
        NomadInteger m_integerValue;
        NomadFloat m_floatValue;
        NomadIndex m_indexValue;
        NomadBoolean m_booleanValue;
        NomadId m_idValue;
        NomadObject* m_objectValue;
    };
};

template<typename Object>
Object* RuntimeValue::asObject() const {
    static_assert(std::is_base_of_v<NomadObject, Object>);
    return static_cast<Object*>(m_objectValue);
}

static_assert(std::is_trivially_copyable_v<RuntimeValue>, "RuntimeValue must be trivially copyable for bitwise copy");

} // namespace nomad

#endif // NOMAD_RUNTIME_VALUE_HPP
