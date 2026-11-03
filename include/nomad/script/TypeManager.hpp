// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/BaseTypes.hpp>
#include <nomad/script/ClassType.hpp>
#include <nomad/script/EnumType.hpp>
#include <nomad/script/FunctionResolution.hpp>
#include <nomad/script/NativeMember.hpp>
#include <nomad/script/Property.hpp>
#include <nomad/script/Type.hpp>

#include <concepts>
#include <memory>
#include <optional>
#include <unordered_map>
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
    [[nodiscard]] const OpaqueIdType* registerOpaqueIdType(const NomadString& name, NomadDocArg);
    [[nodiscard]] TypeId createEnum(
        NomadString name,
        std::vector<EnumValueDefinition> values,
        NomadString documentation = {}
    );
    [[nodiscard]] const Type* getCallbackType(
        const std::vector<const Type*>& parameterTypes,
        const Type* returnType
    );
    [[nodiscard]] const Type* getPredicateType();
    using ParameterizedConstructorInitializer =
        std::function<void(const NomadString&, const std::vector<const Type*>&)>;
    bool registerNativeParameterizedConstructor(
        const NomadString& name,
        NomadIndex argumentCount,
        ParameterizedConstructorInitializer initializer
    );
    [[nodiscard]] std::optional<NomadIndex> getParameterizedConstructorArgumentCount(const NomadString& name) const;
    void getParameterizedConstructorNames(std::vector<NomadString>& names) const;
    [[nodiscard]] NomadString getParameterizedConstructorName(
        const NomadString& name,
        const std::vector<const Type*>& arguments
    );
    using ParameterizedTypeFactory =
        std::function<std::unique_ptr<Type>(Runtime*, TypeId, const std::vector<const Type*>&)>;
    using ParameterizedTypeInitializer = std::function<void(Runtime*, TypeId)>;
    bool registerParameterizedType(
        const NomadString& name,
        NomadIndex argumentCount,
        ParameterizedTypeFactory factory,
        ParameterizedTypeInitializer initializer = {}
    );
    [[nodiscard]] std::optional<NomadIndex> getParameterizedTypeArgumentCount(const NomadString& name) const;
    void getParameterizedTypeNames(std::vector<NomadString>& names) const;
    [[nodiscard]] const Type* getParameterizedType(
        const NomadString& name,
        const std::vector<const Type*>& arguments
    );
    bool registerConstructorTemplate(const NomadString& name, std::vector<TemplateParameter> parameters);
    [[nodiscard]] std::optional<std::vector<TemplateParameter>> getConstructorTemplate(const NomadString& name) const;
    [[nodiscard]] ConstructorInferenceResult inferConstructorTypeArguments(
        const NomadString& name,
        const std::vector<const Type*>& argumentTypes
    ) const;
    bool registerMemberTemplate(
        const NomadString& family,
        const NomadString& name,
        MemberType type,
        const std::vector<TemplateParameter>& parameters = {}
    );
    [[nodiscard]] std::optional<MemberSignature> getMemberTemplateSignature(const NomadString& name) const;
    [[nodiscard]] TypeId getTypeId(const NomadString& name) const;
    [[nodiscard]] std::optional<const Type*> getType(TypeId id) const;
    [[nodiscard]] const Type* getTypeByName(const NomadString& name) const;
    void getTypes(std::vector<const Type*>& types) const;
    [[nodiscard]] NomadIndex getTypeCount() const;
    [[nodiscard]] bool ownsType(const Type* type) const;
    [[nodiscard]] ClassType* getMutableClassType(const ClassType* type);
    [[nodiscard]] CompositeType* getMutableCompositeType(TypeId id);
    void finalizeCompositeFieldLayout(TypeId id);
    [[nodiscard]] const ClassType* getIteratorType(const Type* valueType);
    [[nodiscard]] std::optional<IterableInfo> getIterableInfo(const Type* sourceType) const;
    void registerNativeClassIterable(TypeId ownerId, NativeInterableDefinition definition);
    [[nodiscard]] IndexerId registerNativeClassIndexer(TypeId ownerId, NativeIndexerDefinition definition);
    [[nodiscard]] CallableId registerNativeClassMethod(
        TypeId ownerId,
        NativeMethodDefinition definition
    );
    [[nodiscard]] CallableId registerNativeStructMethod(
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
    [[nodiscard]] CallableId registerNativeStructConstructor(
        TypeId ownerId,
        NativeConstructorDefinition definition
    );
    [[nodiscard]] CallableId registerScriptClassMethod(
        TypeId ownerId,
        NomadString name,
        std::vector<Parameter> parameters,
        NomadString path,
        NomadString source
    );
    [[nodiscard]] CallableId registerScriptStructMethod(
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
    [[nodiscard]] CallableId registerScriptStructConstructor(
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
    [[nodiscard]] CallableId registerScriptConstructor(
        CompositeType* owner,
        std::vector<Parameter> parameters,
        NomadString path,
        NomadString source
    );
    [[nodiscard]] CallableId registerNativeConstructor(
        CompositeType* owner,
        const Type* createdType,
        NativeConstructorDefinition definition
    );
    struct ParameterizedConstructorRegistration {
        NomadString name;
        NomadIndex argumentCount;
        ParameterizedConstructorInitializer initializer;
    };

    struct ParameterizedTypeRegistration {
        NomadString name;
        NomadIndex argumentCount;
        ParameterizedTypeFactory factory;
        ParameterizedTypeInitializer initializer;
        std::optional<std::vector<TemplateParameter>> constructorTemplate;
        std::unordered_map<NomadString, MemberSignature> memberTemplates;
    };

    struct TypeSpecialization {
        NomadString name;
        std::vector<const Type*> arguments;
        const Type* type;
    };

    [[nodiscard]] bool canRegisterClass(const NomadString& name, ClassMode mode) const;
    [[nodiscard]] bool hasConstructor(const NomadString& name) const;
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
    [[nodiscard]] CallableId registerNativeMethod(
        CompositeType* owner,
        NativeMethodDefinition definition
    );
    [[nodiscard]] CallableId registerScriptMethod(
        CompositeType* owner,
        NomadString name,
        std::vector<Parameter> parameters,
        NomadString path,
        NomadString source
    );

    Runtime& m_runtime;
    std::vector<std::unique_ptr<Type>> m_types;
    std::vector<ParameterizedTypeRegistration> m_parameterizedTypes;
    std::vector<TypeSpecialization> m_typeSpecializations;
    std::vector<ParameterizedConstructorRegistration> m_parameterizedConstructors;
    std::vector<NomadString> m_constructorSpecializations;
};

} // namespace nomad
