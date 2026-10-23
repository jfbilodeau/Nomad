// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <nomad/Nomad.hpp>

#include <nomad/compiler/Operators.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Documentation.hpp>
#include <nomad/script/DynamicVariable.hpp>
#include <nomad/script/Event.hpp>
#include <nomad/script/FunctionResolution.hpp>
#include <nomad/script/Hub.hpp>
#include <nomad/script/IteratorObject.hpp>
#include <nomad/script/Member.hpp>
#include <nomad/script/NativeCallable.hpp>
#include <nomad/script/NativeMember.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VariableContext.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/TempHeap.hpp>

namespace nomad {

// Forward declarations
class Compiler;
class Closure;
class FormatString;
class VirtualMachine;
class InterpreterContext;
class NomadStringObject;
class Parser;
class FunctionBuilder;
class Tokenizer;
class Type;
class ObjectType;
class EnumType;
class OpaqueIdType;
struct EnumValueDefinition;

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
        NomadId contextId;
        NomadId localVariableId;
    };

    struct VariableContextRegistration {
        NomadId id;
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

    [[nodiscard]] bool registerHub(const NomadString& name);
    [[nodiscard]] bool isHub(const NomadString& name) const;
    [[nodiscard]] NomadId getHubId(const NomadString& name) const;
    [[nodiscard]] const Hub* getHub(NomadId id) const;
    [[nodiscard]] NomadString getContainingHub(const NomadString& name) const;
    [[nodiscard]] NomadId registerHubVariable(const NomadString& name);
    [[nodiscard]] bool isHubVariable(const NomadString& name) const;
    void setScriptInitializer(CallableId functionId);
    void initializeScripts();

    // Types
    using ParameterizedTypeFactory = std::function<std::unique_ptr<Type>(const std::vector<const Type*>&)>;
    using ParameterizedTypeInitializer = std::function<void(const Type*)>;
    [[nodiscard]] std::optional<NomadIndex> getParameterizedTypeArgumentCount(const NomadString& name) const;
    bool registerParameterizedType(
        const NomadString& name,
        NomadIndex argumentCount,
        ParameterizedTypeFactory factory,
        ParameterizedTypeInitializer initializer = {}
    );
    [[nodiscard]] const Type* getParameterizedType(
        const NomadString& name,
        const std::vector<const Type*>& arguments
    );
    bool registerConstructorTemplate(
        const NomadString& name,
        std::vector<TemplateParameter> parameters
    );
    [[nodiscard]] std::optional<std::vector<TemplateParameter>> getConstructorTemplate(
        const NomadString& name
    ) const;
    [[nodiscard]] ConstructorInferenceResult inferConstructorTypeArguments(
        const NomadString& name,
        const std::vector<const Type*>& argumentTypes
    ) const;
    void registerType(std::unique_ptr<Type>&& type);
    [[nodiscard]] const ObjectType* getIteratorType(const Type* valueType);
    void registerIterableType(
        const Type* sourceType,
        const Type* valueType,
        const Method* factory
    );
    [[nodiscard]] std::optional<IterableDefinition> getIterableDefinition(const Type* sourceType) const;
    [[nodiscard]] TypeId getTypeId(const NomadString& typeName) const;
    [[nodiscard]] std::optional<const Type*> getType(TypeId id) const;
    [[nodiscard]] const Type* getTypeByName(const NomadString& name) const;
    void getTypes(std::vector<const Type*>& types) const;
    [[nodiscard]] const OpaqueIdType* registerOpaqueIdType(const NomadString& name, NomadDocArg);
    [[nodiscard]] const EnumType* registerEnum(
        const NomadString& name,
        const std::vector<EnumValueDefinition>& values
    );
    [[nodiscard]] const Type* getCallbackType(const std::vector<const Type*>& parameterTypes, const Type* returnType);
    [[nodiscard]] const Type* getEventType(const NomadString& name) const;
    [[nodiscard]] const Type* getPredicateType();

    // `objectType` is the class being constructed and the constructor is invoked by its name. `createdType` is the
    // type of the object that gets created; it differs from `objectType` when one class builds several kinds of object.
    CallableId registerNativeConstructor(
        const ObjectType* objectType,
        const std::vector<Parameter>& parameters,
        const ObjectType* createdType,
        ConstructorFactory factory,
        NomadDocArg
    );
    // Shorthand for a constructor whose class and created type are the same.
    CallableId registerNativeConstructor(
        const ObjectType* objectType,
        const std::vector<Parameter>& parameters,
        ConstructorFactory factory,
        NomadDocArg
    );
    // Null unless `functionId` identifies a constructor.
    [[nodiscard]] const NativeConstructor* getNativeConstructor(CallableId functionId) const;
    [[nodiscard]] std::vector<const NativeConstructor*> getConstructors() const;
    [[nodiscard]] std::vector<const NativeConstructor*> getConstructors(const Type* objectType) const;
    using ParameterizedConstructorInitializer =
        std::function<void(const NomadString&, const std::vector<const Type*>&)>;
    // The initializer registers a class type named after the specialization and its constructors.
    bool registerNativeParameterizedConstructor(
        const NomadString& name,
        NomadIndex argumentCount,
        ParameterizedConstructorInitializer initializer
    );
    [[nodiscard]] std::optional<NomadIndex> getParameterizedConstructorArgumentCount(const NomadString& name) const;
    [[nodiscard]] NomadString getParameterizedConstructorName(
        const NomadString& name,
        const std::vector<const Type*>& arguments
    );

    // Member declarations
    bool registerMemberTemplate(
        const NomadString& family,
        const NomadString& name,
        MemberType type,
        const std::vector<TemplateParameter>& parameters = {}
    );
    [[nodiscard]] std::optional<MemberSignature> getMemberSignature(const NomadString& name) const;

    // Native members
    IndexerId registerNativeIndexer(
        const ObjectType* objectType,
        std::vector<const Type*> indexTypes,
        const Type* valueType,
        NativeFunctionFn getFn,
        NativeFunctionFn setFn,
        NomadDocArg
    );
    [[nodiscard]] IndexerId getNativeIndexerId(const Type* objectType, NomadIndex indexCount) const;
    [[nodiscard]] bool getNativeIndexerDefinition(IndexerId indexerId, NativeIndexerDefinition& definition) const;
    [[nodiscard]] const std::vector<NativeIndexerDefinition>& getNativeIndexers() const;

    CallableId registerNativeMethod(
        const ObjectType* objectType,
        const NomadString& name,
        NativeFunctionFn fn,
        const std::vector<Parameter>& parameters,
        const Type* returnType,
        NomadDocArg
    );
    PropertyId registerProperty(const ObjectType* objectType, const NomadString& name, const Type* valueType);
    [[nodiscard]] PropertyId catalogProperty(Property* property);
    [[nodiscard]] const Property* getProperty(PropertyId id) const;
    [[nodiscard]] const NativeProperty* getNativeProperty(PropertyId id) const;
    PropertyId registerNativeProperty(const ObjectType* objectType, const NomadString& name, const Type* valueType,
                                      NativePropertyGetFn getFn, NativePropertySetFn setFn, NomadDocArg);
    [[nodiscard]] MemberReference getNativeMemberId(const Type* objectType, const NomadString& name) const;
    [[nodiscard]] MemberType getMemberType(const MemberReference& member) const;
    [[nodiscard]] bool getNativePropertyDefinition(const MemberReference& member, NativePropertyDefinition& definition) const;
    [[nodiscard]] const NativeMethod* getNativeMemberMethod(const MemberReference& member) const;
    void getNativeMembers(const Type* objectType, std::vector<MemberReference>& members) const;
    void getNativeMembers(std::vector<MemberReference>& members) const;
    void getNativeMemberOverloads(const NomadString& name, std::vector<MemberReference>& overloads) const;
    [[nodiscard]] NomadIndex getNativeMemberOverloadCount(const NomadString& name) const;

    // Convenience type access:
    [[nodiscard]] const Type* getVoidType() const;
    [[nodiscard]] const Type* getBooleanType() const;
    [[nodiscard]] const Type* getIntegerType() const;
    [[nodiscard]] const Type* getFloatType() const;
    [[nodiscard]] const StringType* getStringType() const;
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
    OpCodeId registerInstruction(const NomadString& name, InstructionFn fn, NomadDocArg, std::vector<Operand> operands);
    [[nodiscard]] OpCodeId getInstructionId(const NomadString& name) const;
    [[nodiscard]] OpCodeId getInstructionId(InstructionFn fn) const;
    [[nodiscard]] InstructionFn getInstructionFn(OpCodeId id) const;
    [[nodiscard]] const NomadString& getInstructionName(OpCodeId id) const;
    [[nodiscard]] const std::vector<Operand>& getInstructionOperands(OpCodeId id) const;

    // NativeFunctions
    CallableId registerNativeFunction(
        const NomadString& name, 
        NativeFunctionFn nativeFunction_fn, 
        const std::vector<Parameter>& parameters, 
        const Type* returnType, 
        NomadDocArg
    );
    [[nodiscard]] CallableId getNativeFunctionId(const NomadString& name) const;
    [[nodiscard]] NativeFunctionFn getNativeFunctionFn(CallableId id) const;

    // Null unless `functionId` identifies a native function.
    [[nodiscard]] const NativeFunction* getNativeFunction(CallableId functionId) const;
    [[nodiscard]] const NativeFunction* getNativeFunction(const NomadString& name) const;
    [[nodiscard]] std::vector<const NativeFunction*> getNativeFunctions() const;

    [[nodiscard]] CallableId getFirstNativeFunctionId() const;
    [[nodiscard]] CallableId getNextNativeFunctionId(CallableId currentId) const;

    // Callables
    // Unified catalog of free functions. Native functions and compiled functions are
    // registered here under one namespace, so a name resolves the same way regardless of how it is implemented.
    // Signatures are read live from the callable because a script function's parameters and return type are
    // only known once it has been pre-parsed and its return type inferred.
    // First free-function overload, for signature-driven parsing; not an overload selection.
    [[nodiscard]] CallableId getCallableId(const NomadString& name) const;
    // Every free function registered under `name`. Holds more than one entry when the name is overloaded.
    void getCallableOverloads(const NomadString& name, std::vector<CallableId>& overloads) const;
    [[nodiscard]] NomadIndex getCallableOverloadCount(const NomadString& name) const;
    [[nodiscard]] ParameterShape getParameterShape(const Type* type) const;
    [[nodiscard]] CallableId catalogCallable(Callable* callable);

    // Function table
    // Every function, method and constructor shares this ID space. Native member IDs are separate.
    [[nodiscard]] const Callable* getCallable(CallableId functionId) const;
    // Null unless `functionId` identifies a free function, native or script.
    [[nodiscard]] const Function* getFunction(CallableId functionId) const;
    // All category getters take callable-table IDs, not native member IDs.
    [[nodiscard]] const Method* getMethod(CallableId callableId) const;
    [[nodiscard]] const Constructor* getConstructor(CallableId callableId) const;
    [[nodiscard]] const NativeMethod* getNativeMethod(CallableId callableId) const;
    [[nodiscard]] NomadIndex getFunctionTableSize() const;
    // `argumentTypes` is indexed by parameter and holds `nullptr` for parameters the compiler supplies. An overload
    // matching every argument exactly wins over one that merely accepts them.
    [[nodiscard]] ResolveResult resolveFunction(
        const NomadString& name,
        const std::vector<const Type*>& argumentTypes
    ) const;
    [[nodiscard]] ResolveResult resolveMethod(
        const Type* objectType,
        const NomadString& name,
        const std::vector<const Type*>& argumentTypes
    ) const;
    // `objectType` is the class being constructed.
    [[nodiscard]] ResolveResult resolveConstructor(
        const Type* objectType,
        const std::vector<const Type*>& argumentTypes
    ) const;
    // Describes why `result` is not resolved, e.g. "No overload of 'add' accepts arguments (string)".
    [[nodiscard]] NomadString describeResolveFailure(const NomadString& name, const ResolveResult& result) const;

    // Keywords
    NomadId registerKeyword(const NomadString& keyword, NomadDocArg);
    [[nodiscard]] NomadId getKeywordId(const NomadString& keyword) const;
    void getKeywords(std::vector<KeywordDefinition>& keywords) const;

    // Constants
    NomadId registerConstant(const NomadString& name, const RuntimeValue& value, const Type* type);
    // Borrows the constant's value. The runtime retains ownership, so callers must not free the
    // returned value; copy it with `Type::copyValue` first if they need to own it.
    void getConstantValue(NomadId id, RuntimeValue& value) const;
    [[nodiscard]] NomadId getConstantId(const NomadString& name) const;
    [[nodiscard]] const NomadString& getConstantName(NomadId id) const;
    [[nodiscard]] const Type* getConstantType(NomadId id) const;
    [[nodiscard]] NomadId getFirstConstantId() const;
    [[nodiscard]] NomadId getNextConstantId(NomadId currentId) const;

    // Events
    NomadId registerEvent(const NomadString& name, const std::vector<EventParameter>& parameterTypes);
    [[nodiscard]] NomadId getEventId(const NomadString& name) const;
    [[nodiscard]] std::optional<EventDefinition> getEventDefinition(NomadId id) const;
    [[nodiscard]] NomadId getFirstEventId() const;
    [[nodiscard]] NomadId getNextEventId(NomadId currentId) const;

    // Static strings
    // String literals and hidden `$file`/`$function` arguments are registered during compilation only. `$stringref`
    // values point directly into this table, so it must not grow or change once functions start executing.
    NomadId registerString(const NomadString& string);
    [[nodiscard]] NomadId getStringId(const NomadString& string) const;
    [[nodiscard]] NomadStringObject* getString(NomadId stringId) const;
    [[nodiscard]] NomadStringObject* getStringByName(const NomadString& name) const;

    // Format strings
    NomadId registerFormatString(const NomadString& formatString, CallableId functionId);
    [[nodiscard]] FormatString* getFormatString(NomadId id) const;
    [[nodiscard]] NomadId getFormatStringId(const NomadString& formatString, CallableId functionId) const;

    // Dynamic variables
    NomadId registerDynamicVariable(const NomadString& name, DynamicVariableSetFn setFn, DynamicVariableGetFn getFn, const Type* type, NomadDocArg);
    [[nodiscard]] NomadId getDynamicVariableId(const NomadString& name) const;
    [[nodiscard]] NomadString getDynamicVariableName(NomadId id) const;
    [[nodiscard]] const Type* getDynamicVariableType(NomadId id) const;
    [[nodiscard]] bool canSetDynamicVariable(NomadId id) const;
    [[nodiscard]] bool canGetDynamicVariable(NomadId id) const;
    void setDynamicVariable(VirtualMachine* interpreter, NomadId id, const RuntimeValue& value);
    void getDynamicVariableValue(VirtualMachine* interpreter, NomadId id, RuntimeValue& value) const;
    void setStringDynamicVariable(VirtualMachine* interpreter, NomadId id, const NomadString& value);


    [[nodiscard]] NomadId getFirstDynamicVariableId() const;
    [[nodiscard]] NomadId getNextDynamicVariableId(NomadId currentId) const;

    // Variable contexts
    NomadId registerVariableContext(const NomadString& name, const NomadString& prefix, std::unique_ptr<VariableContext> context);
    [[nodiscard]] NomadId getContextId(const NomadString& name) const;
    NomadId registerContextVariable(NomadId contextId, const NomadString& variableName, const Type* type);
    NomadId registerOrUpdateContextVariable(const NomadString& variableName, const Type* type);
    [[nodiscard]] VariableContext* getVariableContext(NomadId contextId) const;
    [[nodiscard]] NomadString getContextName(NomadId id) const;
    NomadId getContextVariableId(const NomadString& variableName);
    [[nodiscard]] NomadId getVariableContextIdByPrefix(const NomadString& variableName) const;
    [[nodiscard]] NomadString getContextVariableName(NomadId contextVariableId) const;
    [[nodiscard]] const Type* getContextVariableType(NomadId contextVariableId) const;
    void setContextVariableWritten(NomadId contextVariableId, bool written) const;
    void setContextVariableRead(NomadId contextVariableId, bool read) const;
    void setContextVariableValue(NomadId contextVariableId, const RuntimeValue& value) const;
    void getContextVariableValue(NomadId contextVariableId, RuntimeValue& value) const;
    void setStringContextVariableValue(NomadId contextVariableId, const NomadString& value) const;
    void getStringContextVariableValue(NomadId contextVariableId, NomadString& value) const;

    [[nodiscard]] NomadId getFirstVariableContextId() const;
    [[nodiscard]] NomadId getNextVariableContextId(NomadId currentId) const;

    // Script functions. Enumeration and source/bytecode lookups exclude native callables.
    CallableId registerScriptFunction(const NomadString& name, const NomadString& path, const NomadString& source);
    [[nodiscard]] CallableId getFunctionId(const NomadString& name) const;
    // Null unless `functionId` identifies a script function.
    [[nodiscard]] ScriptFunction* getScriptFunction(CallableId functionId);
    [[nodiscard]] const ScriptFunction* getScriptFunction(CallableId functionId) const;
    // Script return types are inferred after compilation. Returns false if `functionId` is not a script function.
    bool setScriptFunctionReturnType(CallableId functionId, const Type* returnType);
    [[nodiscard]] ScriptFunction* getFunctionAtLine(
        const NomadString& sourceName,
        NomadIndex line
    ) const;
    [[nodiscard]] std::optional<NomadString> getFunctionName(CallableId functionId) const;
    [[nodiscard]] ScriptFunction* getFunctionByStartAddress(NomadIndex startAddress) const;
    [[nodiscard]] NomadIndex getFunctionCount() const;
    void getFunctions(std::vector<ScriptFunction*>& functions) const;
    void getFunctions(TempVector<ScriptFunction*>& functions);
    [[nodiscard]] ScriptFunction* getFunctionAtInstructionIndex(NomadIndex instructionIndex) const;

    [[nodiscard]] NomadIndex getFunctionSize() const;

    std::unique_ptr<Compiler> createCompiler();
    void registerCompilerSetup(std::function<void(Compiler*)> setup);

    std::unique_ptr<VirtualMachine> createVirtualMachine();
    // Overloads taking `returnValue` hand the caller an owned copy of the result; the caller must free it
    // through the function's return type. Overloads without `returnValue` discard (and free) the result.
    void executeFunction(const Closure* closure, const std::vector<RuntimeValue>& args);
    void executeFunction(const Closure* closure, const std::vector<RuntimeValue>& args, RuntimeValue& returnValue);
    void executeFunction(CallableId functionId);
    void executeFunction(CallableId functionId, const std::vector<RuntimeValue> &args);
    void executeFunction(CallableId functionId, const std::vector<RuntimeValue> &args, RuntimeValue& returnValue);
    void executeNativeFunction(CallableId nativeFunctionId, const std::vector<RuntimeValue>& args, InterpreterContext& context);

    // Debug
    void dumpInstructions(std::ostream& out) const;
    void dumpInstructions(std::ostream& out, const ScriptFunction* function) const;
    void dumpDocumentation(std::ostream& out) const;

private:
    // Nested types
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

    struct ParameterizedConstructorRegistration {
        NomadString name;
        NomadIndex argumentCount;
        ParameterizedConstructorInitializer initializer;
    };

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
        NomadId id;
        CallableId functionId;
        std::unique_ptr<FormatString> formatString;
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
    CallableId registerNativeCallable(
        const ObjectType* objectType,
        const NomadString& name,
        NativeFunctionFn fn,
        const std::vector<Parameter>& parameters,
        const Type* returnType,
        const NomadString& doc
    );
    CallableId addCallableToTable(std::unique_ptr<Function> function);
    [[nodiscard]] bool canOverloadCallable(
        const NomadString& name,
        const std::vector<Parameter>& parameters,
        const Type* returnType,
        NomadString& error
    ) const;
    [[nodiscard]] ResolveResult resolveAmong(
        std::vector<CallableId> candidates,
        const std::vector<const Type*>& argumentTypes
    ) const;
    // Type and member validation
    [[nodiscard]] bool ownsType(const Type* type) const;
    [[nodiscard]] ObjectType* getMutableObjectType(const ObjectType* type);
    [[nodiscard]] bool canRegisterNativeMember(
        const Type* objectType,
        const NomadString& name,
        MemberType type,
        const std::vector<Parameter>& parameters,
        NomadString& error
    ) const;
    [[nodiscard]] bool canRegisterMemberSignature(
        const NomadString& name,
        MemberType type,
        const std::vector<Parameter>& parameters,
        NomadString& error
    ) const;

    // Hub registration
    void registerHubMember(const NomadString& name, HubMember member);
    [[nodiscard]] bool canRegisterHubMember(const NomadString& name, bool function = false) const;
    void rebuildHubMembers();

    // Context variables
    NomadId internContextVariable(NomadId contextId, NomadId localVariableId);
    [[nodiscard]] const ContextVariable* getContextVariable(NomadId contextVariableId) const;

    // Script execution
    void validateScriptExecution() const;
    // Runs the closure's function and returns it, or nullptr when the closure is invalid.
    const ScriptFunction* runClosure(const Closure* closure, const std::vector<RuntimeValue>& args);

    // Instruction registration
    void registerDefaultInstructions();

    // Data members
    std::vector<Instruction> m_instructions;
    std::vector<OpCodeDefinition> m_opCodes;
    std::vector<std::unique_ptr<Type>> m_types;
    std::vector<ParameterizedTypeRegistration> m_parameterizedTypes;
    std::vector<TypeSpecialization> m_typeSpecializations;
    std::vector<ParameterizedConstructorRegistration> m_parameterizedConstructors;
    std::vector<NomadString> m_constructorSpecializations;
    std::vector<UnaryOperatorRegistration> m_unaryOperators;
    std::vector<BinaryOperatorRegistration> m_binaryOperators;
    std::vector<std::unique_ptr<Function>> m_functions;
    std::vector<Callable*> m_callables;
    std::vector<KeywordDefinition> m_keywords;
    VariableMap m_constantsMap;
    std::vector<RuntimeValue> m_constants;
    std::vector<EventDefinition> m_events;
    // Compile-time only; see `registerString`.
    std::vector<NomadStringObject*> m_strings;
    std::vector<DynamicVariableRegistration> m_dynamicVariables;
    std::vector<VariableContextRegistration> m_variables;
    std::vector<ContextVariable> m_contextVariables;
    std::vector<FormatStringRegistration> m_formatStrings;
    std::vector<MemberReference> m_nativeMembers;
    std::unordered_map<const Type*, IterableDefinition> m_iterableTypes;
    std::vector<Property*> m_properties;
    std::vector<std::unique_ptr<Hub>> m_hubs;
    ScriptInitialization m_scriptInitialization;
    std::vector<NativeIndexerDefinition> m_nativeIndexers;
    std::unordered_map<NomadString, std::vector<MemberReference>> m_nativeMembersByName;
    bool m_debug = false;
    std::unique_ptr<VirtualMachine> m_virtualMachine = nullptr;
    std::vector<std::function<void(Compiler*)>> m_compilerSetups;
};
} // namespace nomad
