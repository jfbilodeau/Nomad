// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Type.hpp>

#include <utility>

namespace nomad {

Type::Type(Runtime* runtime, const TypeId id, NomadString documentation):
    m_runtime(runtime),
    m_id(id),
    m_documentation(std::move(documentation))
{
    if (runtime == nullptr) {
        throw NomadException("A type requires an owning runtime");
    }
}

Runtime* Type::getRuntime() const {
    return m_runtime;
}

TypeId Type::getId() const {
    return m_id;
}

const NomadString& Type::getDocumentation() const {
    return m_documentation;
}

void Type::freeValue(RuntimeValue& /*value*/) const {
    // Default implementation does nothing
}

void Type::assignValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    RuntimeValue copy;
    try {
        copyValue(sourceValue, copy);
    } catch (...) {
        freeValue(copy);
        throw;
    }

    freeValue(destinationValue);
    destinationValue = copy;
}

const TypeOpCodes* Type::getTypeOpCodes() const {
    return &m_typeOpCodes;
}

bool Type::needsFree() const {
    return m_typeOpCodes.freeResult != nullptr;
}

bool Type::acceptsArgument(const Type* argumentType) const {
    return sameType(argumentType);
}

bool Type::isInternal() const {
    return getTypeName().starts_with(FUNCTION_INTERNAL_NAME_PREFIX);
}

NomadString Type::toString(const RuntimeValue& value) const {
    NomadString text_value;

    toString(value, text_value);

    return text_value;
}

bool Type::sameType(const Type* other) const {
    if (other == nullptr) {
        return false;
    }

    if (other == this) {
        return true;
    }

    if (other->getTypeName() == getTypeName()) {
        return true;
    }

    return false;
}

const FunctionType* Type::asCallback() const {
    return nullptr;
}

const EventType * Type::asEvent() const {
    return nullptr;
}

const EnumType* Type::asEnum() const {
    return nullptr;
}

const ClassType* Type::asClassType() const {
    return nullptr;
}

const CompositeType* Type::asCompositeType() const {
    return nullptr;
}

bool Type::isVoid() const {
    return false;
}

bool Type::isString() const {
    return false;
}

NomadString getTypeDisplayName(const Type* type) {
    if (type == nullptr) {
        return "<unknown>";
    }

    const auto prefix = type->asEnum() == nullptr ? NomadString{} : NomadString{"enum "};
    return prefix + type->getTypeName();
}

} // namespace nomad
