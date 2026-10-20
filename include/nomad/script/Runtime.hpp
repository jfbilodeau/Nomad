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
class ObjectTypeBase;
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
class EnumType;
class OpaqueIdType;
struct EnumValueDefinition;

enum class Operand {
    Boolean,
    Integer,
    Float,
    String,
    Id,
    Function,
    NativeFunction,
    FunctionVariable,
    DynamicVariable,
    ContextVariableId,
    FormatString,
    Index,
};

//using CompilerStatementFn = void (*)(Compiler*, Tokenizer*, FunctionBuilder*);
struct OpCodeDefinition {
    NomadId id;
    NomadString name;
    InstructionFn fn;
    std::vector<Operand> operands;
    NomadDocField;
};

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
        std::unique_ptr<VariableContext> context;
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
    [[nodiscard]] NomadString getContainingHub(const NomadString& name) const;
    [[nodiscard]] NomadId registerHubVariable(const NomadString& name);
    [[nodiscard]] bool isHubVariable(const NomadString& name) const;
    void registerHubInitializer(NomadId functionId);
    void initializeHubs();

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
    [[nodiscard]] const ObjectTypeBase* getIteratorType(const Type* valueType);
    void registerIterableType(
        const Type* sourceType,
        const Type* valueType,
        const Method* factory
    );
    [[nodiscard]] std::optional<IterableDefinition> getIterableDefinition(const Type* sourceType) const;
    // [[nodiscard]] const Type* getType(NomadId id) const;
    [[nodiscard]] NomadId getTypeId(const NomadString& typeName) const;
    [[nodiscard]] std::optional<const Type*> getType(NomadId id) const;
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
    NomadId registerNativeConstructor(
        const ObjectTypeBase* objectType,
        const std::vector<Parameter>& parameters,
        const ObjectTypeBase* createdType,
        ConstructorFactory factory,
        NomadDocArg
    );
    // Shorthand for a constructor whose class and created type are the same.
    NomadId registerNativeConstructor(
        const ObjectTypeBase* objectType,
        const std::vector<Parameter>& parameters,
        ConstructorFactory factory,
        NomadDocArg
    );
    // Null unless `functionId` identifies a constructor.
    [[nodiscard]] const NativeConstructor* getNativeConstructor(NomadId functionId) const;
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
    NomadId registerNativeIndexer(
        const ObjectTypeBase* objectType,
        std::vector<const Type*> indexTypes,
        const Type* valueType,
        NativeFunctionFn getFn,
        NativeFunctionFn setFn,
        NomadDocArg
    );
    [[nodiscard]] NomadId getNativeIndexerId(const Type* objectType, NomadIndex indexCount) const;
    [[nodiscard]] bool getNativeIndexerDefinition(NomadId indexerId, NativeIndexerDefinition& definition) const;
    [[nodiscard]] const std::vector<NativeIndexerDefinition>& getNativeIndexers() const;

    NomadId registerNativeMethod(
        const ObjectTypeBase* objectType,
        const NomadString& name,
        NativeFunctionFn fn,
        const std::vector<Parameter>& parameters,
        const Type* returnType,
        NomadDocArg
    );
    NomadId registerNativeProperty(const ObjectTypeBase* objectType, const NomadString& name, const Type* valueType,
                                      NativePropertyGetFn getFn, NativePropertySetFn setFn, NomadDocArg);
    [[nodiscard]] NomadId getNativeMemberId(const Type* objectType, const NomadString& name) const;
    [[nodiscard]] MemberType getMemberType(NomadId memberId) const;
    [[nodiscard]] bool getNativePropertyDefinition(NomadId memberId, NativePropertyDefinition& definition) const;
    // The method behind a member id; `getId()` is its function table id. Null when `memberId` is not a method.
    [[nodiscard]] const NativeMethod* getNativeMemberMethod(NomadId memberId) const;
    void getNativeMembers(const Type* objectType, std::vector<NomadId>& members) const;
    void getNativeMembers(std::vector<NomadId>& members) const;
    void getNativeMemberOverloads(const NomadString& name, std::vector<NomadId>& overloads) const;
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
    [[nodiscard]] NomadId getUnaryOperatorOpCodeId(UnaryOperator op, const Type* operand) const;
    [[nodiscard]] NomadId getBinaryOperatorOpCodeId(BinaryOperator op, const Type* lhs, const Type* rhs) const;
    [[nodiscard]] bool foldUnary(UnaryOperator op, const Type* operandType, const RuntimeValue& value, RuntimeValue& result) const;
    [[nodiscard]] bool foldBinary(BinaryOperator op, const Type* lhsType, const RuntimeValue& lhs, const Type* rhsType, const RuntimeValue& rhs, RuntimeValue& result) const;

    // OpCodes
    NomadId registerInstruction(const NomadString& name, InstructionFn fn, NomadDocArg, std::vector<Operand> operands);
    [[nodiscard]] NomadId getInstructionId(const NomadString& name) const;
    [[nodiscard]] NomadId getInstructionId(InstructionFn fn) const;
    [[nodiscard]] InstructionFn getInstructionFn(NomadId id) const;
    [[nodiscard]] const NomadString& getInstructionName(NomadId id) const;
    [[nodiscard]] const std::vector<Operand>& getInstructionOperands(NomadId id) const;

    // NativeFunctions
    NomadId registerNativeFunction(const NomadString& name, NativeFunctionFn nativeFunction_fn, const std::vector<Parameter>& parameters, const Type* returnType, NomadDocArg);
    [[nodiscard]] NomadId getNativeFunctionId(const NomadString& name) const;
    [[nodiscard]] NativeFunctionFn getNativeFunctionFn(NomadId id) const;

    // Null unless `functionId` identifies a native function.
    [[nodiscard]] const NativeFunction* getNativeFunction(NomadId functionId) const;
    [[nodiscard]] const NativeFunction* getNativeFunction(const NomadString& name) const;
    [[nodiscard]] std::vector<const NativeFunction*> getNativeFunctions() const;

    [[nodiscard]] NomadId getFirstNativeFunctionId() const;
    [[nodiscard]] NomadId getNextNativeFunctionId(NomadId currentId) const;

    // Callables
    // Unified catalog of free functions. Native functions and compiled functions are
    // registered here under one namespace, so a name resolves the same way regardless of how it is implemented.
    // Signatures are read live from the callable because a script function's parameters and return type are
    // only known once it has been pre-parsed and its return type inferred.
    // First free-function overload, for signature-driven parsing; not an overload selection.
    [[nodiscard]] NomadId getCallableId(const NomadString& name) const;
    // Every free function registered under `name`. Holds more than one entry when the name is overloaded.
    void getCallableOverloads(const NomadString& name, std::vector<NomadId>& overloads) const;
    [[nodiscard]] NomadIndex getCallableOverloadCount(const NomadString& name) const;
    [[nodiscard]] ParameterShape getParameterShape(const Type* type) const;

    // Function table
    // Every function, method and constructor shares this ID space. Native member IDs are separate.
    [[nodiscard]] const Callable* getCallable(NomadId functionId) const;
    // Null unless `functionId` identifies a free function, native or script.
    [[nodiscard]] const Function* getFunction(NomadId functionId) const;
    // All category getters take callable-table IDs, not native member IDs.
    [[nodiscard]] const Method* getMethod(NomadId callableId) const;
    [[nodiscard]] const Constructor* getConstructor(NomadId callableId) const;
    [[nodiscard]] const NativeMethod* getNativeMethod(NomadId callableId) const;
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
    NomadId registerFormatString(const NomadString& formatString, NomadId functionId);
    [[nodiscard]] FormatString* getFormatString(NomadId id) const;
    [[nodiscard]] NomadId getFormatStringId(const NomadString& formatString, NomadId functionId) const;

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
    NomadId registerScriptFunction(const NomadString& name, const NomadString& path, const NomadString& source);
    [[nodiscard]] NomadId getFunctionId(const NomadString& name) const;
    // Null unless `functionId` identifies a script function.
    [[nodiscard]] ScriptFunction* getScriptFunction(NomadId functionId);
    [[nodiscard]] const ScriptFunction* getScriptFunction(NomadId functionId) const;
    // Script return types are inferred after compilation. Returns false if `functionId` is not a script function.
    bool setScriptFunctionReturnType(NomadId functionId, const Type* returnType);
    [[nodiscard]] ScriptFunction* getFunctionAtLine(
        const NomadString& sourceName,
        NomadIndex line
    ) const;
    [[nodiscard]] std::optional<NomadString> getFunctionName(NomadId functionId) const;
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
    void executeFunction(NomadId functionId);
    void executeFunction(NomadId functionId, const std::vector<RuntimeValue> &args);
    void executeFunction(NomadId functionId, const std::vector<RuntimeValue> &args, RuntimeValue& returnValue);
    void executeNativeFunction(NomadId nativeFunctionId, const std::vector<RuntimeValue>& args, InterpreterContext& context);

    // Debug
    void dumpInstructions(std::ostream& out) const;
    void dumpInstructions(std::ostream& out, const ScriptFunction* function) const;
    void dumpDocumentation(std::ostream& out) const;

private:
    // Runs the closure's function and returns it, or nullptr when the closure is invalid.
    const ScriptFunction* runClosure(const Closure* closure, const std::vector<RuntimeValue>& args);

    struct UnaryOperatorRegistration {
        UnaryOperator op;
        const Type* operand;
        const Type* result;
        NomadId opCodeId;
        UnaryFoldingFn fn;
    };

    struct BinaryOperatorRegistration {
        BinaryOperator op;
        const Type* lhs;
        const Type* rhs;
        const Type* result;
        NomadId opCodeId;
        BinaryFoldingFn fn;
    };

    struct FormatStringRegistration {
        NomadId id;
        NomadId functionId;
        std::unique_ptr<FormatString> formatString;
    };

    struct CallableOverloadSet {
        NomadString name;
        std::vector<NomadId> overloads;
    };

    struct NativeMemberRegistration {
        MemberType type;
        NomadIndex definitionIndex;
    };

    void registerDefaultInstructions();
    NomadId internContextVariable(NomadId contextId, NomadId localVariableId);
    [[nodiscard]] const ContextVariable* getContextVariable(NomadId contextVariableId) const;
    // Adds `callable` to the overload set for `name`, creating the set when needed.
    void addCallable(const NomadString& name, NomadId callable);
    [[nodiscard]] const CallableOverloadSet* findCallableOverloadSet(const NomadString& name) const;
    // Checks that a native function with these parameters may join `overloadSet` as an overload.
    [[nodiscard]] bool canOverloadCallable(const CallableOverloadSet& overloadSet,
                                           const std::vector<Parameter>& parameters,
                                           const Type* returnType, NomadString& error) const;
    NomadId addCallableToTable(std::unique_ptr<Callable> callable);
    [[nodiscard]] ResolveResult resolveAmong(
        std::vector<NomadId> candidates,
        const std::vector<const Type*>& argumentTypes
    ) const;
    [[nodiscard]] bool ownsType(const Type* type) const;
    [[nodiscard]] bool canRegisterNativeMember(const Type* objectType, const NomadString& name, MemberType type,
                                                  const std::vector<Parameter>& parameters,
                                                  NomadString& error) const;
    [[nodiscard]] bool canRegisterMemberSignature(
        const NomadString& name,
        MemberType type,
        const std::vector<Parameter>& parameters,
        NomadString& error
    ) const;

    std::vector<Instruction> m_instructions;
    std::vector<OpCodeDefinition> m_opCodes;
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
    std::vector<std::unique_ptr<Type>> m_types;
    std::vector<ParameterizedTypeRegistration> m_parameterizedTypes;
    std::vector<TypeSpecialization> m_typeSpecializations;
    struct ParameterizedConstructorRegistration {
        NomadString name;
        NomadIndex argumentCount;
        ParameterizedConstructorInitializer initializer;
    };
    std::vector<ParameterizedConstructorRegistration> m_parameterizedConstructors;
    std::vector<NomadString> m_constructorSpecializations;
    std::vector<UnaryOperatorRegistration> m_unaryOperators;
    std::vector<BinaryOperatorRegistration> m_binaryOperators;
    NomadId registerNativeCallable(
        const ObjectTypeBase* objectType,
        const NomadString& name,
        NativeFunctionFn fn,
        const std::vector<Parameter>& parameters,
        const Type* returnType,
        const NomadString& doc
    );
    std::vector<std::unique_ptr<Callable>> m_functionTable;
    std::unordered_map<NomadString, std::vector<NomadId>> m_functionsByName;
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
    std::vector<CallableOverloadSet> m_callables;
    // Maps a callable name to its index in `m_callables`.
    std::unordered_map<NomadString, NomadIndex> m_callablesByName;
    std::vector<NativeMemberRegistration> m_nativeMembers;
    std::unordered_map<const Type*, IterableDefinition> m_iterableTypes;
    std::vector<NativePropertyDefinition> m_nativeProperties;
    std::unordered_set<NomadString> m_hubs;
    std::unordered_set<NomadString> m_hubVariables;
    mutable std::unordered_set<NomadString> m_uninitializedHubVariables;
    NomadId m_hubVariableContextId = NOMAD_INVALID_ID;
    std::vector<NomadId> m_hubInitializers;
    bool m_initializingHubs = false;
    bool m_initializedHubs = false;
    NomadString m_hubInitializationError;
    std::vector<NativeIndexerDefinition> m_nativeIndexers;
    std::unordered_map<NomadString, std::vector<NomadId>> m_nativeMembersByName;
    bool m_debug = false;
    std::unique_ptr<VirtualMachine> m_virtualMachine = nullptr;
    std::vector<std::function<void(Compiler*)>> m_compilerSetups;
};
} // namespace nomad
