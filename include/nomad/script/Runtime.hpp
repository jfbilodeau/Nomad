// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <optional>
#include <unordered_map>

#include <nomad/Nomad.hpp>

#include <nomad/compiler/Operators.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Documentation.hpp>
#include <nomad/script/DynamicVariable.hpp>
#include <nomad/script/Event.hpp>
#include <nomad/script/FunctionResolution.hpp>
#include <nomad/script/NativeMember.hpp>
#include <nomad/script/ScriptFunction.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VariableContext.hpp>

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
    void registerType(std::unique_ptr<Type>&& type);
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

    // `name` is how `new` refers to the constructor. `objectType` is the class being constructed and `returnType`
    // is the type of the object that gets created; they differ when one class builds several kinds of object.
    NomadId registerConstructor(
        const NomadString& name,
        const ObjectTypeBase* objectType,
        const std::vector<ParameterDefinition>& parameters,
        const ObjectTypeBase* returnType,
        ConstructorFactory factory,
        NomadDocArg
    );
    // Shorthand for a constructor whose class and created type are the same.
    NomadId registerConstructor(
        const NomadString& name,
        const std::vector<ParameterDefinition>& parameters,
        const ObjectTypeBase* returnType,
        ConstructorFactory factory,
        NomadDocArg
    );
    [[nodiscard]] bool getConstructorDefinition(NomadId constructorId, ConstructorDefinition& definition) const;
    // Null unless `functionId` identifies a constructor.
    [[nodiscard]] const ConstructorDefinition* getConstructorDefinition(NomadId functionId) const;
    [[nodiscard]] std::vector<const ConstructorDefinition*> getConstructors() const;
    // The class named by constructors registered under `name`; null when there are none.
    [[nodiscard]] const Type* getConstructorObjectType(const NomadString& name) const;
    [[nodiscard]] std::vector<const ConstructorDefinition*> getConstructors(const Type* objectType) const;
    using ParameterizedConstructorInitializer =
        std::function<void(const NomadString&, const std::vector<const Type*>&)>;
    bool registerParameterizedConstructor(
        const NomadString& name,
        NomadIndex argumentCount,
        ParameterizedConstructorInitializer initializer
    );
    [[nodiscard]] std::optional<NomadIndex> getParameterizedConstructorArgumentCount(const NomadString& name) const;
    [[nodiscard]] NomadString getParameterizedConstructorName(
        const NomadString& name,
        const std::vector<const Type*>& arguments
    );

    // Native members
    NomadId registerNativeIndexer1(
        const ObjectTypeBase* objectType,
        const Type* indexType,
        const Type* valueType,
        NativeIndexerGetFn1 getFn,
        NativeIndexerSetFn1 setFn,
        NomadDocArg
    );
    [[nodiscard]] NomadId getNativeIndexerId1(const Type* objectType) const;
    [[nodiscard]] bool getNativeIndexerDefinition1(NomadId indexerId, NativeIndexerDefinition1& definition) const;
    [[nodiscard]] const std::vector<NativeIndexerDefinition1>& getNativeIndexers1() const;

    NomadId registerNativeMethod(
        const ObjectTypeBase* objectType,
        const NomadString& name,
        NativeFunctionFn fn,
        const std::vector<ParameterDefinition>& parameters,
        const Type* returnType,
        NomadDocArg
    );
    NomadId registerNativeProperty(const ObjectTypeBase* objectType, const NomadString& name, const Type* valueType,
                                      NativePropertyGetFn getFn, NativePropertySetFn setFn, NomadDocArg);
    [[nodiscard]] NomadId getNativeMemberId(const Type* objectType, const NomadString& name) const;
    [[nodiscard]] NativeMemberType getNativeMemberType(NomadId memberId) const;
    [[nodiscard]] bool getNativePropertyDefinition(NomadId memberId, NativePropertyDefinition& definition) const;
    [[nodiscard]] bool getNativeMethodDefinition(NomadId memberId, NativeMethodDefinition& definition) const;
    // The table entry behind a method member id; its `id` is the table id. Null when `memberId` is not a method.
    [[nodiscard]] const NativeFunctionDefinition* getNativeMethodDefinition(NomadId memberId) const;
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
    NomadId registerNativeFunction(const NomadString& name, NativeFunctionFn nativeFunction_fn, const std::vector<ParameterDefinition>& parameters, const Type* returnType, NomadDocArg);
    [[nodiscard]] NomadId getNativeFunctionId(const NomadString& name) const;
    [[nodiscard]] NativeFunctionFn getNativeFunctionFn(NomadId id) const;

    bool getNativeFunctionDefinition(NomadId id, NativeFunctionDefinition& definition) const;
    // Null unless `functionId` identifies a native function or native method.
    [[nodiscard]] const NativeFunctionDefinition* getNativeFunctionDefinition(NomadId functionId) const;
    bool getNativeFunctionDefinition(const NomadString& name, NativeFunctionDefinition& definition) const;
    void getNativeFunctions(std::vector<NativeFunctionDefinition>& nativeFunctions) const;

    [[nodiscard]] NomadId getFirstNativeFunctionId() const;
    [[nodiscard]] NomadId getNextNativeFunctionId(NomadId currentId) const;

    // Callables
    // Unified catalog of everything callable from Nomad code. Native functions and compiled functions are
    // registered here under one namespace, so a name resolves the same way regardless of how it is implemented.
    // Signatures are read live from the owning definition because a function's parameters and return type are
    // only known once it has been pre-parsed and its return type inferred.
    [[nodiscard]] CallableId getCallableId(const NomadString& name) const;
    // Every callable registered under `name`. Holds more than one entry when the name is overloaded.
    void getCallableOverloads(const NomadString& name, std::vector<CallableId>& overloads) const;
    [[nodiscard]] NomadIndex getCallableOverloadCount(const NomadString& name) const;
    [[nodiscard]] const NomadString& getCallableName(CallableId callable) const;
    [[nodiscard]] NomadIndex getCallableParameterCount(CallableId callable) const;
    [[nodiscard]] const Type* getCallableParameterType(CallableId callable, NomadIndex parameterIndex) const;
    [[nodiscard]] const NomadString& getCallableParameterName(CallableId callable, NomadIndex parameterIndex) const;
    [[nodiscard]] const Type* getCallableReturnType(CallableId callable) const;
    [[nodiscard]] ParameterShape getParameterShape(const Type* type) const;
    // Selects the overload of `name` whose parameters match `argumentTypes`, which is indexed by parameter and holds
    // `nullptr` for parameters the compiler supplies. An overload matching every argument exactly wins over one that
    // merely accepts them. Returns an invalid id and fills `error` when nothing matches or when the call is ambiguous.
    [[nodiscard]] CallableId resolveCallableOverload(
        const NomadString& name,
        const std::vector<const Type*>& argumentTypes,
        NomadString& error
    ) const;

    // Function table
    // Every native function, native method and constructor, in one id space. The ids returned when registering
    // them are ids in this table. Script functions are not part of it yet.
    [[nodiscard]] const FunctionDefinition* getFunctionDefinition(NomadId functionId) const;
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

    // Functions
    NomadId registerFunction(const NomadString& name, const NomadString& path, const NomadString& source);
    [[nodiscard]] NomadId getFunctionId(const NomadString& name) const;
    [[nodiscard]] ScriptFunction* getFunction(NomadId function_id) const;
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

    struct NativeMemberRegistration {
        NativeMemberType type;
        NomadIndex definitionIndex;
    };

    void registerDefaultInstructions();
    NomadId internContextVariable(NomadId contextId, NomadId localVariableId);
    [[nodiscard]] const ContextVariable* getContextVariable(NomadId contextVariableId) const;
    // Adds `callable` to the overload set for `name`, creating the set when needed.
    void addCallable(const NomadString& name, CallableId callable);
    [[nodiscard]] const CallableOverloadSet* findCallableOverloadSet(const NomadString& name) const;
    // Checks that a callable of `candidateKind` with these parameters may join `overloadSet` as an overload.
    [[nodiscard]] bool canOverloadCallable(const CallableOverloadSet& overloadSet, CallableKind candidateKind,
                                           const std::vector<ParameterDefinition>& parameters,
                                           const Type* returnType, NomadString& error) const;
    NomadId addFunctionDefinition(std::unique_ptr<FunctionDefinition> definition);
    [[nodiscard]] ResolveResult resolveAmong(
        std::vector<NomadId> candidates,
        const std::vector<const Type*>& argumentTypes
    ) const;
    [[nodiscard]] bool ownsType(const Type* type) const;
    [[nodiscard]] bool canRegisterNativeMember(const Type* objectType, const NomadString& name,
                                                  const std::vector<ParameterDefinition>& parameters,
                                                  NomadString& error) const;

    std::vector<Instruction> m_instructions;
    std::vector<OpCodeDefinition> m_opCodes;
    struct ParameterizedTypeRegistration {
        NomadString name;
        NomadIndex argumentCount;
        ParameterizedTypeFactory factory;
        ParameterizedTypeInitializer initializer;
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
        const std::vector<ParameterDefinition>& parameters,
        const Type* returnType,
        const NomadString& doc
    );
    std::vector<std::unique_ptr<FunctionDefinition>> m_functionTable;
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
    std::vector<std::unique_ptr<ScriptFunction>> m_functions;
    std::vector<CallableOverloadSet> m_callables;
    // Maps a callable name to its index in `m_callables`.
    std::unordered_map<NomadString, NomadIndex> m_callablesByName;
    std::vector<NativeMemberRegistration> m_nativeMembers;
    std::vector<NativePropertyDefinition> m_nativeProperties;
    std::vector<NativeIndexerDefinition1> m_nativeIndexers1;
    std::unordered_map<NomadString, std::vector<NomadId>> m_nativeMembersByName;
    bool m_debug = false;
    std::unique_ptr<VirtualMachine> m_virtualMachine = nullptr;
    std::vector<std::function<void(Compiler*)>> m_compilerSetups;
};
} // namespace nomad
