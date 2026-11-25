// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/Operators.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/BaseTypes.hpp>
#include <nomad/script/Callable.hpp>
#include <nomad/script/Documentation.hpp>
#include <nomad/script/DynamicVariable.hpp>
#include <nomad/script/EnumType.hpp>
#include <nomad/script/Event.hpp>
#include <nomad/script/FunctionResolution.hpp>
#include <nomad/script/IteratorObject.hpp>
#include <nomad/script/Member.hpp>
#include <nomad/script/NativeCallable.hpp>
#include <nomad/script/NativeMember.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/TypeManager.hpp>
#include <nomad/script/TypedNativeFunction.hpp>
#include <nomad/script/VariableContext.hpp>

#include <concepts>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace nomad {


// Forward declarations
class Compiler;
class FormatString;
class Hub;
class VirtualMachine;
class NomadStringObject;
class Parser;
class FunctionBuilder;
class Tokenizer;
class Type;
class ClassType;
class EnumType;
struct EnumValueDefinition;

DeclareId(ConstantId);
DeclareId(FormatStringId);
DeclareId(HubId);
DeclareId(KeywordId);
DeclareId(StringId);

enum class HubMode {
    Opened = 1,
    Closed
};

struct HubFunctions {
    std::vector<CallableId> ids;
};

struct HubTypeFamily {
    NomadIndex argumentCount;
};

using HubMember = std::variant<
    VariableId, HubFunctions, ConstantId, const Type*, EventId, DynamicVariableId, HubTypeFamily
>;

struct KeywordDefinition {
    NomadString keyword;
    NomadDocField;
};

class Runtime {
private:
    struct DynamicVariableRegistration {
        NomadString name;
        const Type* type;
        DynamicVariableSetFn setFn;
        DynamicVariableGetFn getFn;
        NomadDocField;
    };

    struct ContextVariable {
        VariableContextId contextId;
        VariableId localVariableId;
    };

    struct VariableContextRegistration {
        VariableContextId id;
        NomadString name;
        NomadString prefix;
        std::shared_ptr<VariableContext> context;
    };

public:
    Runtime();
    Runtime(const Runtime&) = delete;
    ~Runtime();

    // Generate opcodes that are easier to debug
    // ie: replace op_load_constant with op_load_value
    void setDebug(bool debug);
    [[nodiscard]] bool isDebug() const;

    // Instructions
    [[nodiscard]] const std::vector<Instruction>& getInstructions() const;

    [[nodiscard]] HubId registerHub(const NomadString& name, HubMode mode);
    [[nodiscard]] TypeId registerHubClass(
        HubId hubId,
        NomadString name,
        ClassMode mode,
        NomadString documentation = {}
    );
    template<std::derived_from<ClassType> Descriptor, typename... Arguments>
    [[nodiscard]] TypeId registerHubClass(
        HubId hubId,
        NomadString name,
        ClassMode mode,
        Arguments&&... arguments
    ) {
        const auto qualifiedName = qualifyHubMemberName(hubId, name);
        if (!qualifiedName) {
            return TypeId::INVALID;
        }
        return registerClass<Descriptor>(*qualifiedName, mode, std::forward<Arguments>(arguments)...);
    }
    [[nodiscard]] ConstantId registerHubConstant(
        HubId hubId,
        const NomadString& name,
        const RuntimeValue& value,
        const Type* type
    );
    [[nodiscard]] TypeId registerHubEnum(
        HubId hubId,
        NomadString name,
        std::vector<EnumValueDefinition> values,
        NomadString documentation = {}
    );
    [[nodiscard]] CallableId registerHubNativeFunction(
        HubId hubId,
        const NomadString& name,
        NativeFunctionFn function,
        const std::vector<Parameter>& parameters,
        const Type* returnType,
        NomadDocArg
    );
    template<detail::TypedNativeFunction Function>
    [[nodiscard]] CallableId registerHubNativeFunction(
        HubId hubId,
        const NomadString& name,
        Function function,
        const std::vector<TypedParameterDefinition>& parameters,
        NomadDocArg
    ) {
        const auto qualifiedName = qualifyHubMemberName(hubId, name);
        if (!qualifiedName) {
            return CallableId::INVALID;
        }

        using Binding = detail::TypedNativeFunctionTraits<Function>;
        if (parameters.size() != Binding::parameterCount) {
            log::error("Cannot register typed native function '" + *qualifiedName + "': incorrect parameter definition count");

            return CallableId::INVALID;
        }

        if constexpr (std::is_pointer_v<Function>) {
            if (function == nullptr) {
                log::error("Cannot register typed native function '" + *qualifiedName + "': null function");

                return CallableId::INVALID;
            }
        }

        for (const auto& parameter : parameters) {
            if (parameter.name.empty()) {
                log::error("Cannot register typed native function '" + *qualifiedName + "': empty parameter name");

                return CallableId::INVALID;
            }
        }

        return registerHubNativeFunction(
            hubId,
            name,
            Binding::bind(std::move(function), typename Binding::Indices{}),
            Binding::makeParameters(*this, parameters, typename Binding::Indices{}),
            detail::getTypedNativeType<typename Binding::ReturnType>(*this),
            doc
        );
    }
    [[nodiscard]] CallableId registerHubScriptFunction(
        HubId hubId,
        const NomadString& name,
        const NomadString& path,
        const NomadString& source
    );
    [[nodiscard]] bool isHub(const NomadString& name) const;
    [[nodiscard]] HubId getHubId(const NomadString& name) const;
    [[nodiscard]] const Hub* getHub(HubId id) const;
    [[nodiscard]] NomadString getContainingHub(const NomadString& name) const;
    [[nodiscard]] ContextVariableId registerHubVariable(const NomadString& name);
    [[nodiscard]] ContextVariableId registerHubVariable(HubId hubId, const NomadString& name);
    [[nodiscard]] bool isHubVariable(const NomadString& name) const;
    void setScriptInitializer(CallableId functionId);
    void initializeScripts();

    // Types
    // Public declarations coordinate catalog registration and namespace publication.
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

        const auto id = m_typeManager.reserveTypeId();
        if (id.isInvalid()) {
            return TypeId::INVALID;
        }

        auto type = std::make_unique<Descriptor>(
            this, id, name, mode, std::forward<Arguments>(arguments)...
        );

        return publishClass(id, name, mode, std::move(type));
    }
    [[nodiscard]] TypeId registerStruct(NomadString name, NomadString documentation = {});
    [[nodiscard]] TypeId registerType(std::unique_ptr<Type> type);
    [[nodiscard]] const OpaqueIdType* registerOpaqueIdType(const NomadString& name, NomadDocArg);
    [[nodiscard]] TypeId createEnum(
        NomadString name,
        std::vector<EnumValueDefinition> values,
        NomadString documentation = {}
    );
    using ParameterizedConstructorInitializer = TypeManager::ParameterizedConstructorInitializer;
    bool registerNativeParameterizedConstructor(
        const NomadString& name,
        NomadIndex argumentCount,
        ParameterizedConstructorInitializer initializer
    );
    using ParameterizedTypeFactory = TypeManager::ParameterizedTypeFactory;
    using ParameterizedTypeInitializer = TypeManager::ParameterizedTypeInitializer;
    bool registerParameterizedType(
        const NomadString& name,
        NomadIndex argumentCount,
        ParameterizedTypeFactory factory,
        ParameterizedTypeInitializer initializer = {}
    );
    [[nodiscard]] TypeId getTypeId(const NomadString& typeName) const;
    [[nodiscard]] TypeManager& getTypeManager();
    [[nodiscard]] const TypeManager& getTypeManager() const;
    // Checks hub-name and member availability without logging.
    [[nodiscard]] bool canRegisterTypeName(const NomadString& name) const;
    void publishTypeNamespace(const Type& type);
    void publishTypeFamilyNamespace(const NomadString& name, NomadIndex argumentCount);
    [[nodiscard]] bool canRegisterMemberSignature(
        const NomadString& name,
        MemberType type,
        const std::vector<Parameter>& parameters,
        NomadString& error
    ) const;
    [[nodiscard]] std::optional<const Type*> getType(TypeId id) const;
    [[nodiscard]] const Type* getTypeByName(const NomadString& name) const;
    void getTypes(std::vector<const Type*>& types) const;
    void registerEnumOperators(const EnumType* type);

    // Null unless `functionId` identifies a constructor.
    [[nodiscard]] const NativeConstructor* getNativeConstructor(CallableId functionId) const;
    // Requires an ID already resolved by the compiler; performs no validation.
    [[nodiscard]] const NativeConstructor& getUncheckedNativeConstructor(const CallableId functionId) const {
        return *static_cast<const NativeConstructor*>(m_callables[toNomadIndex(functionId.id)]);
    }
    [[nodiscard]] std::vector<const NativeConstructor*> getConstructors() const;
    [[nodiscard]] std::vector<const NativeConstructor*> getConstructors(const Type* classType) const;

    // Member declarations
    // Publishes compatible family parsing signatures without creating a specialization.
    bool registerMemberTemplate(
        const NomadString& family,
        const NomadString& name,
        MemberType type,
        const std::vector<TemplateParameter>& parameters = {}
    );
    [[nodiscard]] std::optional<MemberSignature> getMemberSignature(const NomadString& name) const;

    // Member registration
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

    // Native members
    [[nodiscard]] IndexerId getNativeIndexerId(const Type* classType, NomadIndex indexCount) const;
    [[nodiscard]] IndexerId catalogNativeIndexer(const NativeIndexerInfo* info);
    // Borrows type-owned metadata until runtime destruction; null for an invalid or unregistered ID.
    [[nodiscard]] const NativeIndexerInfo* getNativeIndexerInfo(IndexerId indexerId) const;
    [[nodiscard]] const std::vector<const NativeIndexerInfo*>& getNativeIndexers() const;

    void setPropertyHasExplicitInitializer(PropertyId id, bool hasExplicitInitializer);
    [[nodiscard]] const Property* getProperty(PropertyId id) const;
    [[nodiscard]] const NativeProperty* getNativeProperty(PropertyId id) const;
    [[nodiscard]] NativeProperty* getMutableNativeProperty(PropertyId id);
    // Indexes an already registered native property for native-member lookup.
    void catalogNativeProperty(PropertyId id);
    void catalogNativeMethod(CallableId id);
    [[nodiscard]] bool canRegisterNativeMember(
        const Type* classType,
        const NomadString& name,
        MemberType type,
        const std::vector<Parameter>& parameters,
        NomadString& error
    ) const;
    void validateNativePropertyTypes();
    // First matching native member for this owner/name, or nullopt; does not select an overload.
    [[nodiscard]] std::optional<MemberReference> getNativeMemberId(
        const Type* classType,
        const NomadString& name
    ) const;
    [[nodiscard]] MemberType getMemberType(const MemberReference& member) const;
    [[nodiscard]] std::optional<NativePropertyInfo> getNativePropertyInfo(const MemberReference& member) const;
    [[nodiscard]] const NativeMethod* getNativeMemberMethod(const MemberReference& member) const;
    void getNativeMembers(const Type* classType, std::vector<MemberReference>& members) const;
    void getNativeMembers(std::vector<MemberReference>& members) const;
    void getNativeMemberOverloads(const NomadString& name, std::vector<MemberReference>& overloads) const;
    [[nodiscard]] NomadIndex getNativeMemberOverloadCount(const NomadString& name) const;

    // Convenience type access:
    [[nodiscard]] const Type* getVoidType() const;
    [[nodiscard]] const Type* getBooleanType() const;
    [[nodiscard]] const Type* getIntegerType() const;
    [[nodiscard]] const Type* getFloatType() const;
    [[nodiscard]] const Type* getStringType() const;
    [[nodiscard]] const Type* getFunctionType() const;
    [[nodiscard]] const Type* getEventCallbackType() const;
    [[nodiscard]] const Type* getEventDispatchType() const;
    [[nodiscard]] const Type* getFileNameType() const;
    [[nodiscard]] const Type* getFunctionNameType() const;
    [[nodiscard]] const Type* getLineNumberType() const;

    void registerUnaryOperator(UnaryOperator op, const Type* operand, const Type* result, const NomadString& opCodeName, UnaryFoldingFn fn);
    void registerBinaryOperator(BinaryOperator op, const Type* lhs, const Type* rhs, const Type* result, const NomadString& opCodeName, BinaryFoldingFn fn);
    [[nodiscard]] const Type* getUnaryOperatorResultType(UnaryOperator op, const Type* operandType) const;
    [[nodiscard]] const Type* getBinaryOperatorResultType(BinaryOperator op, const Type* lhsType, const Type* rhsType) const;
    [[nodiscard]] OpCodeId getUnaryOperatorOpCodeId(UnaryOperator op, const Type* operand) const;
    [[nodiscard]] OpCodeId getBinaryOperatorOpCodeId(BinaryOperator op, const Type* lhs, const Type* rhs) const;
    [[nodiscard]] bool foldUnary(UnaryOperator op, const Type* operandType, const RuntimeValue& value, RuntimeValue& result) const;
    [[nodiscard]] bool foldBinary(BinaryOperator op, const Type* lhsType, const RuntimeValue& lhs, const Type* rhsType, const RuntimeValue& rhs, RuntimeValue& result) const;

    // OpCodes
    OpCodeId registerInstruction(const NomadString& name, InstructionFn fn, NomadDocArg, std::vector<Operand> operands, StackEffect stackEffect = {});
    [[nodiscard]] OpCodeId getInstructionId(const NomadString& name) const;
    [[nodiscard]] OpCodeId getInstructionId(InstructionFn fn) const;
    // Returns nullptr for an invalid or unregistered ID without logging.
    [[nodiscard]] InstructionFn getInstructionFn(OpCodeId id) const;
    // Metadata getters throw NomadException for invalid or unregistered IDs without logging.
    [[nodiscard]] const NomadString& getInstructionName(OpCodeId id) const;
    [[nodiscard]] const std::vector<Operand>& getInstructionOperands(OpCodeId id) const;
    [[nodiscard]] const StackEffect& getInstructionStackEffect(OpCodeId id) const;

    CallableId registerNativeFunction(
        const NomadString& name, 
        NativeFunctionFn nativeFunction_fn, 
        const std::vector<Parameter>& parameters, 
        const Type* returnType, 
        NomadDocArg
    );
    template<detail::TypedNativeFunction Function>
    [[nodiscard]] CallableId registerNativeFunction(
        const NomadString& name,
        Function function,
        const std::vector<TypedParameterDefinition>& parameters,
        NomadDocArg
    ) {
        using Binding = detail::TypedNativeFunctionTraits<Function>;
        if (parameters.size() != Binding::parameterCount) {
            log::error("Cannot register typed native function '" + name + "': incorrect parameter definition count");
            return CallableId::INVALID;
        }
        if constexpr (std::is_pointer_v<Function>) {
            if (function == nullptr) {
                log::error("Cannot register typed native function '" + name + "': null function");
                return CallableId::INVALID;
            }
        }
        for (const auto& parameter : parameters) {
            if (parameter.name.empty()) {
                log::error("Cannot register typed native function '" + name + "': empty parameter name");
                return CallableId::INVALID;
            }
        }
        return registerNativeFunction(
            name,
            Binding::bind(std::move(function), typename Binding::Indices{}),
            Binding::makeParameters(*this, parameters, typename Binding::Indices{}),
            detail::getTypedNativeType<typename Binding::ReturnType>(*this),
            doc
        );
    }
    [[nodiscard]] CallableId getNativeFunctionId(const NomadString& name) const;

    // Null unless `functionId` identifies a native function.
    [[nodiscard]] const NativeFunction* getNativeFunction(CallableId functionId) const;
    // Requires an ID already resolved by the compiler; performs no validation.
    [[nodiscard]] const NativeFunction& getUncheckedNativeFunction(const CallableId functionId) const {
        return *static_cast<const NativeFunction*>(m_callables[toNomadIndex(functionId.id)]);
    }
    [[nodiscard]] NativeFunction* getNativeFunction(CallableId functionId);
    [[nodiscard]] const NativeFunction* getNativeFunction(const NomadString& name) const;
    [[nodiscard]] std::vector<const NativeFunction*> getNativeFunctions() const;
    void validateNativeFunctionSignatures();

    [[nodiscard]] CallableId getFirstNativeFunctionId() const;
    [[nodiscard]] CallableId getNextNativeFunctionId(CallableId currentId) const;

    // Callables
    // Unified catalog of free functions. Native functions and compiled functions are
    // registered here under one namespace, so a name resolves the same way regardless of how it is implemented.
    // Signatures are read live from the callable because a script function's parameters and return type are
    // only known once it has been pre-parsed and its return type inferred.
    // Tests for a registered native or script free function without selecting an overload.
    [[nodiscard]] bool functionExists(const NomadString& name) const;
    // First free-function overload, for signature-driven parsing; not an overload selection.
    [[nodiscard]] CallableId getFunctionId(const NomadString& name) const;
    // Every free function registered under `name`. Holds more than one entry when the name is overloaded.
    void getFunctionIds(const NomadString& name, std::vector<CallableId>& ids) const;
    [[nodiscard]] NomadIndex getFunctionOverloadCount(const NomadString& name) const;
    [[nodiscard]] ParameterShape getParameterShape(const Type* type) const;
    // The ID the next published callable receives; invalid when the callable table is full.
    [[nodiscard]] CallableId getNextCallableId() const;
    [[nodiscard]] CallableId registerMethod(
        CompositeType* owner,
        std::unique_ptr<Method> method
    );
    [[nodiscard]] CallableId registerConstructor(
        CompositeType* owner,
        std::unique_ptr<Constructor> constructor
    );

    // Callable table
    // Every function, method and constructor shares this ID space. Native member IDs are separate.
    [[nodiscard]] const Callable* getCallable(CallableId functionId) const;
    // Null unless `functionId` identifies a free function, native or script.
    [[nodiscard]] const Function* getFunction(CallableId functionId) const;
    // All category getters take callable-table IDs, not native member IDs.
    [[nodiscard]] const Method* getMethod(CallableId callableId) const;
    [[nodiscard]] const Constructor* getConstructor(CallableId callableId) const;
    [[nodiscard]] const NativeMethod* getNativeMethod(CallableId callableId) const;
    // Requires an ID already resolved by the compiler; performs no validation.
    [[nodiscard]] const NativeMethod& getUncheckedNativeMethod(const CallableId callableId) const {
        return *static_cast<const NativeMethod*>(m_callables[toNomadIndex(callableId.id)]);
    }
    // Counts all registered functions, methods, and constructors, regardless of source.
    [[nodiscard]] NomadIndex getCallableCount() const;
    // `argumentTypes` is indexed by parameter and holds `nullptr` for parameters the compiler supplies. An overload
    // matching every argument exactly wins over one that merely accepts them.
    [[nodiscard]] ResolveResult resolveFunction(
        const NomadString& name,
        const std::vector<const Type*>& argumentTypes
    ) const;
    [[nodiscard]] ResolveResult resolveMethod(
        const Type* classType,
        const NomadString& name,
        const std::vector<const Type*>& argumentTypes
    ) const;
    // `classType` is the class being constructed.
    [[nodiscard]] ResolveResult resolveConstructor(
        const Type* classType,
        const std::vector<const Type*>& argumentTypes
    ) const;
    // Describes why `result` is not resolved, e.g. "No overload of 'add' accepts arguments (string)".
    [[nodiscard]] NomadString describeResolveFailure(const NomadString& name, const ResolveResult& result) const;

    // Keywords
    KeywordId registerKeyword(const NomadString& keyword, NomadDocArg);
    [[nodiscard]] KeywordId getKeywordId(const NomadString& keyword) const;
    void getKeywords(std::vector<KeywordDefinition>& keywords) const;

    // Constants
    ConstantId registerConstant(const NomadString& name, const RuntimeValue& value, const Type* type);
    // Borrows the constant's value. The runtime retains ownership, so callers must not free the
    // returned value; copy it with `Type::copyValue` first if they need to own it.
    // Invalid IDs throw NomadException without changing the output or logging.
    void getConstantValue(ConstantId id, RuntimeValue& value) const;
    // Requires a compiler-resolved ID; the same borrowed-value contract applies without validation.
    void getUncheckedConstantValue(const ConstantId id, RuntimeValue& value) const {
        value = m_constants[toNomadIndex(id.id)];
    }
    [[nodiscard]] ConstantId getConstantId(const NomadString& name) const;
    // Throws NomadException for invalid or unregistered IDs without logging.
    [[nodiscard]] const NomadString& getConstantName(ConstantId id) const;
    // Returns nullptr for an invalid or unregistered ID without logging.
    [[nodiscard]] const Type* getConstantType(ConstantId id) const;
    // Requires an ID already resolved by the compiler; performs no validation.
    [[nodiscard]] const Type* getUncheckedConstantType(const ConstantId id) const {
        return m_constantsMap.getVariableType(VariableId{id.id});
    }
    [[nodiscard]] ConstantId getFirstConstantId() const;
    [[nodiscard]] ConstantId getNextConstantId(ConstantId currentId) const;

    // Events
    EventId registerEvent(const NomadString& name, const std::vector<EventParameter>& parameterTypes);
    [[nodiscard]] EventId getEventId(const NomadString& name) const;
    // Borrows read-only metadata until Runtime destruction, including across event registration.
    // Invalid or unregistered IDs return nullptr without logging.
    [[nodiscard]] const EventDefinition* getEventDefinition(EventId id) const;
    [[nodiscard]] EventId getFirstEventId() const;
    [[nodiscard]] EventId getNextEventId(EventId currentId) const;

    // Static strings
    // String literals and hidden `$file`/`$function` arguments are registered during compilation only. `$stringref`
    // values point directly into this table, so it must not grow or change once functions start executing.
    StringId registerString(const NomadString& string);
    [[nodiscard]] StringId getStringId(const NomadString& string) const;
    // Borrowed objects; invalid IDs or missing names return nullptr without logging.
    [[nodiscard]] NomadStringObject* getString(StringId stringId) const;
    [[nodiscard]] NomadStringObject* getStringByName(const NomadString& name) const;

    // Format strings
    FormatStringId registerFormatString(const NomadString& formatString, CallableId functionId);
    [[nodiscard]] FormatString* getFormatString(FormatStringId id);
    [[nodiscard]] const FormatString* getFormatString(FormatStringId id) const;
    [[nodiscard]] FormatStringId getFormatStringId(const NomadString& formatString, CallableId functionId) const;

    // Dynamic variables
    DynamicVariableId registerDynamicVariable(
        const NomadString& name,
        DynamicVariableSetFn setFn,
        DynamicVariableGetFn getFn,
        const Type* type,
        NomadDocArg
    );
    [[nodiscard]] DynamicVariableId getDynamicVariableId(const NomadString& name) const;
    // Invalid IDs throw for names, return nullptr for types, and false for capabilities.
    // These metadata queries do not log.
    [[nodiscard]] NomadString getDynamicVariableName(DynamicVariableId id) const;
    [[nodiscard]] const Type* getDynamicVariableType(DynamicVariableId id) const;
    [[nodiscard]] bool canSetDynamicVariable(DynamicVariableId id) const;
    [[nodiscard]] bool canGetDynamicVariable(DynamicVariableId id) const;
    // Host dispatch throws NomadException for invalid IDs or absent callbacks without logging.
    // Callback exceptions and side effects propagate unchanged.
    void setDynamicVariable(VirtualMachine* interpreter, DynamicVariableId id, const RuntimeValue& value);
    // Rejected reads leave the output unchanged before a callback is invoked.
    void getDynamicVariableValue(VirtualMachine* interpreter, DynamicVariableId id, RuntimeValue& value) const;
    // Requires a compiler-resolved ID with a setter; performs no validation.
    void setUncheckedDynamicVariable(
        VirtualMachine* machine,
        const DynamicVariableId id,
        const RuntimeValue& value
    ) {
        m_dynamicVariables[toNomadIndex(id.id)].setFn(machine, value);
    }
    // Requires a compiler-resolved ID with a getter; performs no validation.
    void getUncheckedDynamicVariableValue(
        VirtualMachine* machine,
        const DynamicVariableId id,
        RuntimeValue& value
    ) const {
        m_dynamicVariables[toNomadIndex(id.id)].getFn(machine, value);
    }
    // Releases the temporary even when the setter throws.
    void setStringDynamicVariable(VirtualMachine* interpreter, DynamicVariableId id, const NomadString& value);


    [[nodiscard]] DynamicVariableId getFirstDynamicVariableId() const;
    [[nodiscard]] DynamicVariableId getNextDynamicVariableId(DynamicVariableId currentId) const;

    // Variable contexts
    VariableContextId registerVariableContext(
        const NomadString& name,
        const NomadString& prefix,
        std::unique_ptr<VariableContext> context
    );
    [[nodiscard]] VariableContextId getContextId(const NomadString& name) const;
    ContextVariableId registerContextVariable(VariableContextId contextId, const NomadString& variableName, const Type* type);
    ContextVariableId registerOrUpdateContextVariable(const NomadString& variableName, const Type* type);
    // Borrows a mutable context even from a const Runtime; its values and metadata are not frozen.
    [[nodiscard]] VariableContext* getVariableContext(VariableContextId contextId) const;
    // Throws NomadException for invalid or unregistered IDs without logging.
    [[nodiscard]] NomadString getContextName(VariableContextId id) const;
    ContextVariableId getContextVariableId(const NomadString& variableName);
    [[nodiscard]] VariableContextId getVariableContextIdByPrefix(const NomadString& variableName) const;
    [[nodiscard]] NomadString getContextVariableName(ContextVariableId contextVariableId) const;
    [[nodiscard]] const Type* getContextVariableType(ContextVariableId contextVariableId) const;
    // Const context dispatch intentionally permits live value and read/write-tracking changes.
    void setContextVariableWritten(ContextVariableId contextVariableId, bool written) const;
    void setContextVariableRead(ContextVariableId contextVariableId, bool read) const;
    void setContextVariableValue(ContextVariableId contextVariableId, const RuntimeValue& value) const;
    void getContextVariableValue(ContextVariableId contextVariableId, RuntimeValue& value) const;
    // Releases the temporary even when the context throws; retained values remain context-owned.
    void setStringContextVariableValue(ContextVariableId contextVariableId, const NomadString& value) const;
    void getStringContextVariableValue(ContextVariableId contextVariableId, NomadString& value) const;

    [[nodiscard]] VariableContextId getFirstVariableContextId() const;
    [[nodiscard]] VariableContextId getNextVariableContextId(VariableContextId currentId) const;

    // Script functions. Enumeration and source/bytecode lookups exclude native callables.
    CallableId registerScriptFunction(const NomadString& name, const NomadString& path, const NomadString& source);
    [[nodiscard]] CallableId getScriptFunctionId(const NomadString& name) const;
    // Null unless `functionId` identifies a script function.
    [[nodiscard]] ScriptFunction* getScriptFunction(CallableId functionId);
    [[nodiscard]] const ScriptFunction* getScriptFunction(CallableId functionId) const;
    [[nodiscard]] ScriptCallableBase* getScriptCallable(CallableId callableId);
    [[nodiscard]] const ScriptCallableBase* getScriptCallable(CallableId callableId) const;
    [[nodiscard]] std::optional<NomadString> getScriptFunctionName(CallableId functionId) const;
    [[nodiscard]] NomadIndex getScriptFunctionCount() const;
    void getScriptFunctions(std::vector<ScriptFunction*>& functions);
    void getScriptFunctions(std::vector<const ScriptFunction*>& functions) const;
    [[nodiscard]] const ScriptCallableBase* getScriptCallableAtLine(
        const NomadString& sourceName,
        NomadIndex line
    ) const;
    [[nodiscard]] const ScriptCallableBase* getScriptCallableByStartAddress(NomadIndex startAddress) const;
    [[nodiscard]] const ScriptCallableBase* getScriptCallableAtInstructionIndex(NomadIndex instructionIndex) const;
    void getScriptCallables(std::vector<const ScriptCallableBase*>& callables) const;

    std::unique_ptr<Compiler> createCompiler();
    void registerCompilerSetup(std::function<void(Compiler*)> setup);

    std::unique_ptr<VirtualMachine> createVirtualMachine();

private:
    // Nested types
    struct UnaryOperatorRegistration {
        UnaryOperator op;
        const Type* operand;
        const Type* result;
        OpCodeId opCodeId;
        UnaryFoldingFn fn;
    };

    struct BinaryOperatorRegistration {
        BinaryOperator op;
        const Type* lhs;
        const Type* rhs;
        const Type* result;
        OpCodeId opCodeId;
        BinaryFoldingFn fn;
    };

    struct FormatStringRegistration {
        FormatStringId id;
        CallableId functionId;
        std::unique_ptr<FormatString> formatString;
    };

    struct TypeFamilyNamespace {
        NomadString name;
        NomadIndex argumentCount;
    };

    struct MemberTemplateSignatureRegistration {
        NomadIndex familyIndex;
        NomadString familyName;
    };

    enum class ScriptInitializationState {
        Pending = 1,
        Running,
        Complete,
        Failed
    };

    struct ScriptInitialization {
        CallableId functionId = CallableId{};
        ScriptInitializationState state = ScriptInitializationState::Pending;
        NomadString error;
    };

    // Callable registration and resolution
    [[nodiscard]] CallableId registerNativeConstructor(
        CompositeType* owner,
        const Type* createdType,
        NativeConstructorDefinition definition
    );
    [[nodiscard]] CallableId registerNativeMethod(
        CompositeType* owner,
        NativeMethodDefinition definition
    );
    [[nodiscard]] CallableId registerScriptConstructor(
        CompositeType* owner,
        std::vector<Parameter> parameters,
        NomadString path,
        NomadString source
    );
    [[nodiscard]] CallableId registerScriptMethod(
        CompositeType* owner,
        NomadString name,
        std::vector<Parameter> parameters,
        NomadString path,
        NomadString source
    );
    CallableId registerNativeFunctionEntry(
        const NomadString& name,
        NativeFunctionFn function,
        const std::vector<Parameter>& parameters,
        const Type* returnType,
        const NomadString& doc
    );
    CallableId addCallableToTable(std::unique_ptr<Function> function);
    void appendNativeMember(const NomadString& name, MemberReference member);
    [[nodiscard]] bool canOverloadCallable(
        const NomadString& name,
        const std::vector<Parameter>& parameters,
        const Type* returnType,
        NomadString& error,
        CallableId excludedId = {}
    ) const;
    [[nodiscard]] ResolveResult resolveAmong(
        std::vector<CallableId> candidates,
        const std::vector<const Type*>& argumentTypes
    ) const;
    // Type and member validation
    [[nodiscard]] bool ownsType(const Type* type) const;
    [[nodiscard]] bool canRegisterClass(const NomadString& name, ClassMode mode) const;
    [[nodiscard]] bool canPublishTypeNamespace(const Type& type) const;
    [[nodiscard]] TypeId publishClass(
        TypeId id,
        const NomadString& name,
        ClassMode mode,
        std::unique_ptr<ClassType> type
    );
    // Hub registration
    [[nodiscard]] std::optional<NomadString> qualifyHubMemberName(HubId hubId, const NomadString& name) const;
    void addHubMember(
        std::unordered_map<NomadString, HubMember>& members,
        const NomadString& name,
        HubMember member
    );
    void registerHubMember(const NomadString& name, HubMember member);
    [[nodiscard]] bool canRegisterHubMember(const NomadString& name, bool function = false) const;
    void rebuildHubMembers();

    // Context variables
    ContextVariableId internContextVariable(VariableContextId contextId, VariableId localVariableId);
    [[nodiscard]] const ContextVariable* getContextVariable(ContextVariableId contextVariableId) const;

    // Instruction registration
    void registerDefaultInstructions();

    // Data members
    std::vector<Instruction> m_instructions;
    std::vector<OpCodeDefinition> m_opCodes;
    TypeManager m_typeManager;
    // These views borrow their descriptors from m_typeManager.
    const Type* m_voidType = nullptr;
    const Type* m_booleanType = nullptr;
    const Type* m_integerType = nullptr;
    const Type* m_floatType = nullptr;
    const Type* m_stringType = nullptr;
    const Type* m_eventCallbackType = nullptr;
    const Type* m_eventDispatchType = nullptr;
    const Type* m_fileNameType = nullptr;
    const Type* m_functionNameType = nullptr;
    const Type* m_lineNumberType = nullptr;
    std::vector<const Type*> m_namespaceTypes;
    std::vector<TypeFamilyNamespace> m_namespaceTypeFamilies;
    std::vector<UnaryOperatorRegistration> m_unaryOperators;
    std::vector<BinaryOperatorRegistration> m_binaryOperators;
    std::vector<std::unique_ptr<Function>> m_functions;
    std::vector<Callable*> m_callables;
    std::unordered_map<NomadString, std::vector<CallableId>> m_functionIdsByName;
    std::vector<KeywordDefinition> m_keywords;
    VariableMap m_constantsMap;
    std::vector<RuntimeValue> m_constants;
    std::vector<std::unique_ptr<EventDefinition>> m_events;
    // Compile-time only; see `registerString`.
    std::vector<NomadStringObject*> m_strings;
    std::vector<DynamicVariableRegistration> m_dynamicVariables;
    std::vector<VariableContextRegistration> m_variables;
    std::vector<ContextVariable> m_contextVariables;
    std::vector<FormatStringRegistration> m_formatStrings;
    std::vector<MemberReference> m_nativeMembers;
    std::vector<std::unique_ptr<Hub>> m_hubs;
    ScriptInitialization m_scriptInitialization;
    std::vector<const NativeIndexerInfo*> m_nativeIndexers;
    std::unordered_map<NomadString, std::vector<MemberReference>> m_nativeMembersByName;
    std::unordered_map<NomadString, MemberTemplateSignatureRegistration> m_memberTemplateSignatures;
    bool m_debug = false;
    std::vector<std::function<void(Compiler*)>> m_compilerSetups;
};
} // namespace nomad
