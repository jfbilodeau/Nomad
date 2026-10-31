// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ClassType.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/String.hpp>

#include <algorithm>
#include <utility>

namespace nomad {

ClassType::~ClassType() = default;

ClassType::ClassType(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    const ClassMode mode,
    std::vector<const Type*> typeArguments,
    NomadString documentation
):
    CompositeType(runtime, id, std::move(name), mode, std::move(documentation)),
    m_typeArguments(std::move(typeArguments))
{
}

void ClassType::initValue(RuntimeValue* value) const {
    value->setObjectValue(nullptr);
}

void ClassType::freeValue(RuntimeValue* value) const {
    value->freeObjectValue();
}

void ClassType::copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const {
    if (sourceValue == destinationValue) {
        return;
    }
    RuntimeValue copy;
    copy.copyObjectValue(*sourceValue);
    destinationValue->freeObjectValue();
    *destinationValue = copy;
}

bool ClassType::sameType(const Type* other) const {
    return this == other;
}

bool ClassType::needsFree() const {
    return true;
}

void ClassType::emitInitResult(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_object_init_r);
}

void ClassType::emitInitIntermediate(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_object_init_i);
}

void ClassType::emitFreeResult(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_object_free_r);
}

void ClassType::emitFreeIntermediate(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_object_free_i);
}

void ClassType::emitFreeStackValue(
    OpCodeEmitter& emitter,
    const NomadIndex offset
) const {
    emitter.addOpCode(op_object_free_stack);
    emitter.addIndex(offset);
}

void ClassType::emitFreeFunctionVariable(
    OpCodeEmitter& emitter,
    const NomadIndex variableIndex
) const {
    emitter.addOpCode(op_object_free_variable);
    emitter.addIndex(variableIndex);
}

void ClassType::emitFreeParameter(OpCodeEmitter& emitter, const NomadId parameterId) const {
    emitter.addOpCode(op_object_free_parameter);
    emitter.addId(parameterId);
}

NomadIndex ClassType::emitLoadFunctionVariable(
    OpCodeEmitter& emitter,
    const NomadId variableId,
    const ExpressionTarget target
) const {
    return emitter.addLoadTarget(
        op_function_variable_object_get_r,
        op_function_variable_object_get_i,
        op_function_variable_object_push,
        variableId,
        target
    );
}

NomadIndex ClassType::emitLoadParameter(
    OpCodeEmitter& emitter,
    const NomadId parameterId,
    const ExpressionTarget target
) const {
    return emitter.addLoadTarget(
        op_object_parameter_load_r,
        op_object_parameter_load_i,
        op_object_parameter_push,
        parameterId,
        target
    );
}

NomadIndex ClassType::emitLoadContextVariable(
    OpCodeEmitter& emitter,
    const NomadId variableId,
    const ExpressionTarget target
) const {
    return emitter.addLoadTarget(
        op_context_variable_object_get_r,
        op_context_variable_object_get_i,
        op_context_variable_object_push,
        variableId,
        target
    );
}

NomadIndex ClassType::emitCopyResultToStack(OpCodeEmitter& emitter) const {
    return emitter.addOpCode(op_object_push_r);
}

void ClassType::emitMoveResultToIntermediate(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_object_move_r_to_i);
}

void ClassType::emitMoveIntermediateToResult(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_object_move_i_to_r);
}

void ClassType::emitMoveResultToStack(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_object_move_r_to_stack);
}

NomadIndex ClassType::emitPushIntermediate(OpCodeEmitter& emitter) const {
    return emitter.addOpCode(op_object_push_i);
}

NomadIndex ClassType::emitPopResult(OpCodeEmitter& emitter) const {
    return emitter.addOpCode(op_object_pop_r);
}

NomadIndex ClassType::emitPopIntermediate(OpCodeEmitter& emitter) const {
    return emitter.addOpCode(op_object_pop_i);
}

TypeStorage ClassType::getStorage() const {
    return TypeStorage::Object;
}

NomadIndex ClassType::getSize() const {
    return 1;
}

const ClassType* ClassType::asClassType() const {
    return this;
}

IndexerId ClassType::registerNativeIndexer(
    Runtime& runtime,
    NativeIndexerInfo info
) {
    if (info.classType != this || info.id.isValid() ||
        runtime.getType(getId()) != this) {
        log::error("Cannot register native indexer: invalid owner or identity");
        return IndexerId::INVALID;
    }
    const auto isValidType = [&](const Type* type) {
        return type != nullptr && !type->isVoid() && !type->isInternal() &&
            runtime.getType(type->getId()) == type;
    };
    if (!isValidType(info.valueType)) {
        log::error("Cannot register native indexer: value type must be registered, public, and non-void");
        return IndexerId::INVALID;
    }
    if (info.indexTypes.empty()) {
        log::error("Cannot register native indexer: at least one index type is required");
        return IndexerId::INVALID;
    }
    if (!std::ranges::all_of(info.indexTypes, isValidType)) {
        log::error("Cannot register native indexer: index types must be registered, public, and non-void");
        return IndexerId::INVALID;
    }
    if (!info.getFn && !info.setFn) {
        log::error("Cannot register native indexer: indexer has no accessor or mutator");
        return IndexerId::INVALID;
    }
    if (getNativeIndexer(info.indexTypes.size()) != nullptr) {
        log::error("Cannot register native indexer: type already has an indexer with " +
            nomad::toString(info.indexTypes.size()) + " indices");
        return IndexerId::INVALID;
    }
    m_nativeIndexers.push_back(std::make_unique<NativeIndexerInfo>(std::move(info)));
    auto& indexer = *m_nativeIndexers.back();
    const auto id = runtime.catalogNativeIndexer(&indexer);
    if (id.isInvalid()) {
        m_nativeIndexers.pop_back();
        log::error("Cannot register native indexer: indexer registration failed");
        return IndexerId::INVALID;
    }
    indexer.id = id;
    return id;
}

const NativeIndexerInfo* ClassType::getNativeIndexer(const NomadIndex indexCount) const {
    const auto indexer = std::find_if(m_nativeIndexers.begin(), m_nativeIndexers.end(), [&](const auto& candidate) {
        return candidate->indexTypes.size() == indexCount;
    });
    return indexer == m_nativeIndexers.end() ? nullptr : indexer->get();
}

std::vector<const NativeIndexerInfo*> ClassType::getNativeIndexers() const {
    std::vector<const NativeIndexerInfo*> indexers;
    indexers.reserve(m_nativeIndexers.size());
    for (const auto& indexer : m_nativeIndexers) {
        indexers.push_back(indexer.get());
    }
    return indexers;
}

void ClassType::registerIterableType(
    Runtime& runtime,
    const Type* valueType,
    const Method* factory
) {
    const auto* iteratorType = runtime.getIteratorType(valueType);
    if (runtime.getType(getId()) != this ||
        factory == nullptr || runtime.getMethod(factory->getId()) != factory ||
        factory->getClassType() != this || factory->getParameterCount() != 0 ||
        factory->getReturnType() != iteratorType || m_iterableInfo.has_value()) {
        throw NomadException("Invalid iterable registration");
    }
    m_iterableInfo = IterableInfo{valueType, iteratorType, factory->getId()};
}

std::optional<IterableInfo> ClassType::getIterableInfo() const {
    return m_iterableInfo;
}

const std::vector<const Type*>& ClassType::getTypeArguments() const {
    return m_typeArguments;
}

void ClassType::toString(const RuntimeValue&, NomadString& string) const {
    string = CompositeType::getTypeName();
}

} // namespace nomad
