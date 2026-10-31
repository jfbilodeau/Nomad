// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ClassType.hpp>
#include <nomad/script/NativeMember.hpp>
#include <nomad/script/Property.hpp>
#include <nomad/script/Type.hpp>

#include <concepts>
#include <memory>
#include <optional>
#include <utility>
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
    [[nodiscard]] TypeId registerClass(
        NomadString name,
        ClassMode mode,
        NomadString documentation = {}
    );
    template<std::derived_from<ClassType> Descriptor, typename... Arguments>
    [[nodiscard]] TypeId registerClass(
        NomadString name,
        ClassMode mode,
        Arguments&&... arguments
    ) {
        if (!canRegisterClass(name, mode)) {
            return TypeId::INVALID;
        }
        const auto id = reserveTypeId();
        auto type = std::make_unique<Descriptor>(
            &m_runtime, id, name, mode, std::forward<Arguments>(arguments)...
        );
        return publishClass(id, name, mode, std::move(type));
    }
    [[nodiscard]] TypeId registerStruct(NomadString name, NomadString documentation = {});
    void registerType(std::unique_ptr<Type> type);
    [[nodiscard]] TypeId getTypeId(const NomadString& name) const;
    [[nodiscard]] std::optional<const Type*> getType(TypeId id) const;
    [[nodiscard]] const Type* getTypeByName(const NomadString& name) const;
    void getTypes(std::vector<const Type*>& types) const;
    [[nodiscard]] NomadIndex getTypeCount() const;
    [[nodiscard]] bool ownsType(const Type* type) const;
    [[nodiscard]] ClassType* getMutableClassType(const ClassType* type);
    [[nodiscard]] CompositeType* getMutableCompositeType(TypeId id);
    void finalizeCompositeFieldLayout(TypeId id);
    [[nodiscard]] CallableId registerNativeClassMethod(
        TypeId ownerId,
        NativeMethodDefinition definition
    );
    [[nodiscard]] CallableId registerNativeClassConstructor(
        TypeId ownerId,
        NativeConstructorDefinition definition
    );
    [[nodiscard]] CallableId registerNativeClassConstructor(
        TypeId ownerId,
        TypeId createdTypeId,
        NativeConstructorDefinition definition
    );
    [[nodiscard]] CallableId registerScriptClassMethod(
        TypeId ownerId,
        NomadString name,
        std::vector<Parameter> parameters,
        NomadString path,
        NomadString source
    );
    [[nodiscard]] CallableId registerScriptClassConstructor(
        TypeId ownerId,
        std::vector<Parameter> parameters,
        NomadString path,
        NomadString source
    );
    [[nodiscard]] PropertyId registerNativeClassProperty(
        TypeId ownerId,
        NativePropertyDefinition definition
    );
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

    // Low-level catalog operations do not publish namespace members.
    [[nodiscard]] TypeId reserveTypeId();
    void catalogType(std::unique_ptr<Type> type);

private:
    [[nodiscard]] bool canRegisterClass(const NomadString& name, ClassMode mode) const;
    [[nodiscard]] TypeId publishClass(
        TypeId id,
        const NomadString& name,
        ClassMode mode,
        std::unique_ptr<ClassType> type
    );
    [[nodiscard]] PropertyId registerScriptProperty(
        CompositeType* owner,
        const NomadString& name,
        const Type* valueType
    );

    Runtime& m_runtime;
    std::vector<std::unique_ptr<Type>> m_types;
};

} // namespace nomad
