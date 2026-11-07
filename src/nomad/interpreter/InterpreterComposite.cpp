#include <nomad/interpreter/InterpreterComposite.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/CompositeType.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <stdexcept>

namespace nomad {

InterpreterComposite::InterpreterComposite(const Interpreter& interpreter, const Type& type):
    m_interpreter(interpreter), m_type(type)
{}

InterpreterComposite::~InterpreterComposite() = default;

const Type* InterpreterComposite::getType() const {
    return &m_type;
}

const Interpreter& InterpreterComposite::getInterpreter() const {
    return m_interpreter;
}

PropertyId InterpreterComposite::resolveProperty(const NomadStringView name) const {
    const auto member = m_type.getRuntime()->getNativeMemberId(&m_type, NomadString{name});
    if (m_type.getRuntime()->getMemberType(member) == MemberType::Property) {
        return std::get<PropertyId>(member);
    }
    const auto* owner = m_type.asCompositeType();
    const auto* property = owner != nullptr ? owner->getProperty(NomadString{name}) : nullptr;
    if (property == nullptr) {
        throw std::invalid_argument("Unknown interpreter property '" + NomadString{name} + "'");
    }
    return property->getId();
}

InterpreterValue InterpreterComposite::getProperty(const NomadStringView propertyPath) const {
    const auto separator = propertyPath.find('.');
    const auto propertyId = resolveProperty(propertyPath.substr(0, separator));
    auto value = getProperty(propertyId);
    if (separator == NomadStringView::npos) {
        return value;
    }
    return m_interpreter.getProperty(value, propertyPath.substr(separator + 1));
}

void InterpreterComposite::setProperty(
    const NomadStringView propertyPath,
    const InterpreterValue& value
) {
    const auto separator = propertyPath.find('.');
    const auto propertyId = resolveProperty(propertyPath.substr(0, separator));
    if (separator == NomadStringView::npos) {
        setProperty(propertyId, value);
        return;
    }
    const auto current = getProperty(propertyId);
    const auto replacement = m_interpreter.setProperty(current, propertyPath.substr(separator + 1), value);
    setProperty(propertyId, replacement);
}

} // namespace nomad
