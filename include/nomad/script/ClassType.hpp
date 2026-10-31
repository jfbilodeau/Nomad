// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/CompositeType.hpp>
#include <nomad/script/NativeMember.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace nomad {

struct IterableInfo {
    const Type* valueType;
    const ClassType* iteratorType;
    CallableId factoryId;
};

class ClassType : public CompositeType {
public:
    explicit ClassType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        std::vector<const Type*> typeArguments = {},
        NomadString documentation = {}
    );
    ~ClassType() override;
    using Type::toString;
    [[nodiscard]] TypeStorage getStorage() const override;
    [[nodiscard]] NomadIndex getSize() const override;
    [[nodiscard]] const std::vector<const Type*>& getTypeArguments() const;
    [[nodiscard]] const ClassType* asClassType() const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;

    void initValue(RuntimeValue* value) const override;
    void freeValue(RuntimeValue* value) const override;
    void copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;
    void emitFreeParameter(OpCodeEmitter& emitter, NomadId parameterId) const override;

    [[nodiscard]] IndexerId registerNativeIndexer(
        Runtime& runtime,
        NativeIndexerInfo info
    );
    [[nodiscard]] const NativeIndexerInfo* getNativeIndexer(NomadIndex indexCount) const;
    [[nodiscard]] std::vector<const NativeIndexerInfo*> getNativeIndexers() const;
    void registerIterableType(
        Runtime& runtime,
        const Type* valueType,
        const Method* factory
    );
    [[nodiscard]] std::optional<IterableInfo> getIterableInfo() const;

private:
    std::vector<const Type*> m_typeArguments;
    std::vector<std::unique_ptr<NativeIndexerInfo>> m_nativeIndexers;
    std::optional<IterableInfo> m_iterableInfo;
};

} // namespace nomad
