// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Callable.hpp>
#include <nomad/script/Property.hpp>
#include <nomad/script/Type.hpp>

#include <memory>
#include <span>
#include <vector>

namespace nomad {

enum class ClassMode {
    Opened = 1,
    Closed,
};

class CompositeType : public Type {
public:
    struct StoredFieldLayout {
        const Type* type;
        NomadIndex offset;
        NomadIndex size;
    };

    ~CompositeType() override;

    using Type::toString;
    [[nodiscard]] NomadString getTypeName() const override;
    [[nodiscard]] const NomadString& getInitializerName() const;
    [[nodiscard]] const Method* getInitializer() const;
    [[nodiscard]] const CompositeType* asCompositeType() const override;
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] CallableId registerConstructor(
        Runtime& runtime,
        std::unique_ptr<Constructor> constructor
    );
    [[nodiscard]] std::vector<const Constructor*> getConstructors() const;
    [[nodiscard]] CallableId registerMethod(
        Runtime& runtime,
        std::unique_ptr<Method> method
    );
    [[nodiscard]] std::vector<const Method*> getMethods() const;
    [[nodiscard]] PropertyId registerProperty(
        Runtime& runtime,
        std::unique_ptr<Property> property
    );
    [[nodiscard]] const Property* getProperty(const NomadString& name) const;
    [[nodiscard]] const Property* getProperty(PropertyId id) const;
    [[nodiscard]] const Property* getPropertyAt(NomadIndex index) const;
    [[nodiscard]] std::vector<const Property*> getProperties() const;
    [[nodiscard]] NomadIndex getFieldIndex(PropertyId id) const;
    [[nodiscard]] NomadIndex getFieldOffset(PropertyId id) const;
    [[nodiscard]] NomadIndex getFieldStorageSize() const;
    // Runtime views require a finalized layout; violations terminate.
    [[nodiscard]] NomadIndex getLayoutSize() const noexcept;
    [[nodiscard]] std::span<const StoredFieldLayout> getFieldLayouts() const noexcept;
    [[nodiscard]] bool isFieldLayoutFinalized() const noexcept;
    void finalizeFieldLayout();
    [[nodiscard]] virtual NomadIndex getPropertyCount() const;

protected:
    CompositeType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        NomadString documentation = {}
    );

private:
    enum class FieldLayoutState {
        Unresolved = 1,
        Resolving,
        Finalized,
    };

    NomadString m_name;
    NomadString m_initializerName;
    ClassMode m_mode;
    std::vector<std::unique_ptr<Property>> m_properties;
    std::vector<std::unique_ptr<Constructor>> m_constructors;
    std::vector<std::unique_ptr<Method>> m_methods;
    FieldLayoutState m_fieldLayoutState = FieldLayoutState::Unresolved;
    NomadIndex m_fieldStorageSize = 0;
    std::vector<StoredFieldLayout> m_fieldLayouts;
};

} // namespace nomad
