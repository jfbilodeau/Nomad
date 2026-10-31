// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Property.hpp>
#include <nomad/script/Type.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace nomad {

class TypeManager {
public:
    explicit TypeManager(Runtime& runtime);
    ~TypeManager();
    TypeManager(const TypeManager&) = delete;
    TypeManager& operator=(const TypeManager&) = delete;
    TypeManager(TypeManager&&) = delete;
    TypeManager& operator=(TypeManager&&) = delete;

    [[nodiscard]] Runtime& getRuntime() const;
    [[nodiscard]] TypeId getTypeId(const NomadString& name) const;
    [[nodiscard]] std::optional<const Type*> getType(TypeId id) const;
    [[nodiscard]] const Type* getTypeByName(const NomadString& name) const;
    void getTypes(std::vector<const Type*>& types) const;
    [[nodiscard]] NomadIndex getTypeCount() const;
    [[nodiscard]] bool ownsType(const Type* type) const;
    [[nodiscard]] ClassType* getMutableClassType(const ClassType* type);
    [[nodiscard]] CompositeType* getMutableCompositeType(TypeId id);
    void finalizeCompositeFieldLayout(TypeId id);
    [[nodiscard]] PropertyId registerScriptClassProperty(
        TypeId ownerId,
        const NomadString& name,
        const Type* valueType
    );
    [[nodiscard]] PropertyId registerScriptStructProperty(
        TypeId ownerId,
        const NomadString& name,
        const Type* valueType
    );

    // Low-level catalog operations; Runtime coordinates namespace registration.
    [[nodiscard]] TypeId reserveTypeId();
    void catalogType(std::unique_ptr<Type> type);

private:
    [[nodiscard]] PropertyId registerScriptProperty(
        CompositeType* owner,
        const NomadString& name,
        const Type* valueType
    );

    Runtime& m_runtime;
    std::vector<std::unique_ptr<Type>> m_types;
};

} // namespace nomad
