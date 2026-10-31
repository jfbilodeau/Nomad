// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/DeferredType.hpp>
#include <nomad/compiler/DeferredTypeTarget.hpp>
#include <nomad/compiler/Identifier.hpp>
#include <nomad/compiler/Operators.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Source.hpp>
#include <nomad/compiler/StatementParsers.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/OpCode.hpp>

#include <functional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nomad {

// Forward declarations
class Compiler;
enum class BuildPhase;
class Tokenizer;
class FunctionBuilder;

enum class BuildPhase {
    Init = 1,  // Pre-compile
    Compile,
};

enum class CompilationCallableTarget {
    Function = 1,
    Constructor,
    HubInitializer,
    ObjectInitializer
};

class Compiler : public OpCodeEmitter {
public:
    explicit Compiler(Runtime* runtime, std::vector<Instruction>& instructions);
    Compiler(const Compiler& other) = delete;
    ~Compiler() override;

    [[nodiscard]] Runtime* getRuntime() const;

    // Error reporting
    [[noreturn]]
    void reportError(const NomadString& message) const;
    [[noreturn]]
    void reportError(const NomadString& message, const ScriptCallableBase* function, const Tokenizer* tokenizer) const;
    [[noreturn]]
    void reportError(const NomadString& message, const NomadString& functionName, NomadIndex line, NomadIndex column) const;
    [[noreturn]]
    [[noreturn]] void reportInternalError(const NomadString& message) const;

    // Statements
    void registerParseStatementFn(
        const NomadString& name,
        StatementAvailability availability,
        ParseStatementFn fn,
        PreParseStatementFn preFn = nullptr
    );
    [[nodiscard]] bool isStatement(const NomadString& name) const;
    [[nodiscard]] bool getParseStatementFn(
        const NomadString& name,
        ParseStatementFn& fn,
        StatementAvailability& availability
    ) const;
    [[nodiscard]] bool getPreParseStatementFn(
        const NomadString& name,
        PreParseStatementFn& fn
    ) const;
    void getRegisteredStatements(std::vector<NomadString>& parsers) const;

    // Constant folding
    void registerUnaryOperator(UnaryOperator op, const Type* operand, const Type* result, const NomadString& opCodeName, UnaryFoldingFn fn);
    void registerBinaryOperator(BinaryOperator op, const Type* lhs, const Type* rhs, const Type* result, const NomadString& opCodeName, BinaryFoldingFn fn);
    [[nodiscard]] const Type* getUnaryOperatorResultType(UnaryOperator op, const Type* operandType) const;
    [[nodiscard]] const Type* getBinaryOperatorResultType(BinaryOperator op, const Type* lhsType, const Type* rhsType) const;
    [[nodiscard]] OpCodeId getUnaryOperatorOpCodeId(UnaryOperator op, const Type* operand) const;
    [[nodiscard]] OpCodeId getBinaryOperatorOpCodeId(BinaryOperator op, const Type* lhs, const Type* rhs) const;
    [[nodiscard]] bool foldUnary(
        UnaryOperator op,
        const Type* operandType,
        const RuntimeValue& value,
        RuntimeValue& result
    ) const;
    [[nodiscard]] bool foldBinary(
        BinaryOperator op,
        const Type* lhsType,
        const RuntimeValue& lhs,
        const Type* rhsType,
        const RuntimeValue& rhs,
        RuntimeValue& result
    ) const;

    // Identifier identification
    IdentifierType getIdentifierType(const NomadString& name, const ScriptCallableBase* function) const;
    void getIdentifierDefinition(const NomadString& name, const ScriptCallableBase* function, IdentifierDefinition& definition) const;
    [[nodiscard]] const Property* resolveClassField(
        CompilerContext* context,
        ScriptCallableBase* function,
        const NomadString& name,
        const Type* valueType,
        bool allowDeclaration,
        NomadIndex line,
        NomadIndex column
    );
    [[nodiscard]] const Property* resolveObjectField(
        CompilerContext* context,
        ScriptCallableBase* function,
        const ClassType* owner,
        const NomadString& name,
        const Type* valueType,
        bool allowDeclaration,
        NomadIndex line,
        NomadIndex column
    );
    void recordFieldRead(PropertyId id);
    [[nodiscard]] bool getNativeMemberConflict(const NomadString& identifier, const Type* type,
                                                  const ScriptCallableBase* function,
                                                  NomadString& conflictingIdentifier) const;

    // Opcode generation
    [[nodiscard]] NomadIndex getOpCodeSize() const;

    NomadIndex addOpCode(OpCodeId opCode);
    NomadIndex addOpCode(const NomadString& opCodeName);
    NomadIndex addOpCode(InstructionFn opCode) override;
    void addFunctionReturn(const ScriptCallableBase* function);
    void addFunctionEntry(const ScriptCallableBase* function);
    void addLoadReceiver(ScriptCallableBase* function, ExpressionTarget target);
    void addStoreParameter(NomadId parameterId, const Type* type);
    NomadIndex addId(NomadId id) override;
    NomadIndex addIndex(NomadIndex index) override;
    NomadIndex addInteger(NomadInteger value);
    NomadIndex addFloat(NomadFloat value);
    NomadIndex addLoadValue(const Type* type, const RuntimeValue& value, ExpressionTarget target);
    NomadIndex addLoadConstant(ConstantId constantId, ExpressionTarget target);
    NomadIndex addLoadBooleanValue(NomadBoolean value, ExpressionTarget target);
    NomadIndex addLoadFloatValue(NomadFloat value, ExpressionTarget target);
    NomadIndex addLoadIntegerValue(NomadInteger value, ExpressionTarget target);
    NomadIndex addLoadStringValue(const NomadString& value, ExpressionTarget target);
    NomadIndex addLoadFunctionVariable(VariableId variableId, const Type* type, ExpressionTarget target);
    NomadIndex addLoadDynamicVariable(DynamicVariableId variableId, const Type* type, ExpressionTarget target);
    NomadIndex addLoadContextVariable(ContextVariableId contextVariableId, const Type* type, ExpressionTarget target);
    NomadIndex addLoadParameter(NomadIndex parameterId, const Type* type, ExpressionTarget target);

    NomadIndex addPushResult(const Type* type);
    // Moves a freshly computed value from the `r` register to target. String buffers are moved, not copied.
    void addMoveResult(const Type* type, ExpressionTarget target);
    void addMoveIntermediateToResult(const Type* type);
    NomadIndex addPushIntermediate(const Type* type);
    NomadIndex addPopResult(const Type* type);
    NomadIndex addPopIntermediate(const Type* type);
    NomadIndex addScriptFunctionCall(CallableId targetFunctionId, ScriptCallableBase* function, const ArgumentList* arguments);
    NomadIndex addScriptMethodCall(CallableId memberId, ScriptCallableBase* function, const ArgumentList* arguments);
    NomadIndex addScriptConstructorCall(
        CallableId constructorId,
        TypeId typeId,
        ScriptCallableBase* function,
        const ArgumentList* arguments
    );
    NomadIndex addNativeFunctionCall(CallableId nativeFunctionId, ScriptCallableBase* function, const ArgumentList* arguments);
    NomadIndex addNativeConstructorCall(CallableId constructorId, ScriptCallableBase* function, const ArgumentList* arguments);
    NomadIndex addNativeMethodCall(CallableId memberId, ScriptCallableBase* function, const ArgumentList* arguments);
    // After a call returns, free the arguments still on the stack.
    void addFreeArguments(const ArgumentList* arguments);

    void setOpCode(NomadIndex index, const NomadString& opCodeName);
    void setOpCode(NomadIndex index, OpCodeId opCodeId);
    void setId(NomadIndex index, NomadId id);
    void setIndex(NomadIndex index, NomadIndex value);

    // Configure additional roots before registering snapshots. Snapshots override their backing files.
    void registerSourceRoots(std::span<const SourceRoot> roots);
    // Copies snapshots and immediately reports loading and registration diagnostics.
    void registerSources(CompilerContext* context, const SourceBatch& batch);
    // Parser interface: creates a function belonging to an already registered script.
    // Returns NOMAD_INVALID_ID and reports to context if the function name is already used.
    [[nodiscard]] CallableId createFunction(
        CompilerContext* context,
        NomadId scriptId,
        const NomadString& functionName,
        CompilationCallableTarget kind = CompilationCallableTarget::Function
    );
    [[nodiscard]] NomadId getScriptId(CallableId functionId) const;
    [[nodiscard]] const ScriptSource& getScript(NomadId scriptId) const;
    void setFunctionBody(
        CallableId functionId,
        std::unique_ptr<FunctionNode> ast
    );
    void registerMemberCompilation(
        CallableId memberId,
        CallableId sourceFunctionId,
        CompilationCallableTarget kind
    );
    [[nodiscard]] const Type* getReceiverType(const ScriptCallableBase* function) const;
    [[nodiscard]] bool isFieldInitializer(const ScriptCallableBase* function) const;
    void registerScriptInitializer(
        CompilerContext* context,
        NomadId scriptId,
        std::unique_ptr<HubInitializerStatement> initializer
    );
    [[nodiscard]] const TypedValue* getHubInitialValue(const NomadString& name) const;
    // Borrows an AST node until placeholder preparation, before type inference.
    void deferFormatString(
        FormatStringLiteral* literal,
        CallableId functionId,
        NomadString sourceContent,
        NomadString hub,
        bool hubInitializer
    );
    void registerPhaseHook(std::function<void(CompilerContext*, CompilationPhase)> hook);
    [[nodiscard]] const DeferredType* deferType(TypeReference reference);
    void registerDeferredTypeTarget(std::unique_ptr<DeferredTypeTarget> target);
    void resolveDeferredTypes();
    [[nodiscard]] bool compileScripts(
        CompilerContext* context,
        CompilationTarget target = CompilationTarget::Executable
    );
    [[nodiscard]] bool compileFunctions(CompilerContext* context);
    void addFunctionLink(CallableId functionId, NomadIndex callIndex);
    // Function calls used in expressions, recorded while resolving. Explains return types that cannot be inferred.
    void addFunctionCallDependency(CallableId callerFunctionId, CallableId calleeFunctionId);
    // True if calleeFunctionId leads back to callerFunctionId or to itself through recorded function calls.
    [[nodiscard]] bool isRecursiveFunctionCall(CallableId callerFunctionId, CallableId calleeFunctionId) const;
    // Records that a function contains a `return` statement. Functions without one return void.
    void addReturningFunction(CallableId functionId);
    void linkFunctions();
    void postCompile(CompilerContext* context);

    // Generate function names for internal functions
    NomadString generateFunctionName(const NomadString& generatedName, const NomadString& hostFunctionName, NomadIndex line);
    NomadString generateFunctionName(const NomadString& name, const ScriptCallableBase* function, NomadIndex line);

private:
    std::vector<std::unique_ptr<DeferredType>> m_deferredTypes;
    std::vector<std::unique_ptr<DeferredTypeTarget>> m_deferredTypeTargets;

    struct FunctionCompilation {
        NomadId scriptId = NOMAD_INVALID_ID;
        std::unique_ptr<FunctionNode> ast;
        ScriptCallableBase* function;
        CompilationCallableTarget kind = CompilationCallableTarget::Function;
    };

    struct ScriptCompilation {
        ScriptSource script;
        // Only exists while compileScripts() runs so lexing errors are reported once.
        std::unique_ptr<Tokenizer> tokens;

        CallableId functionId = CallableId{};
    };

    struct PendingFormatString {
        FormatStringLiteral* literal;
        CallableId functionId;
        NomadString sourceContent;
        NomadString hub;
        bool hubInitializer;
    };

    void prepareScriptInitialization(CompilerContext* context);
    void installScriptInitialValues();
    void registerScript(CompilerContext* context, const ScriptSource& script);
    void preParseFunction(CompilerContext* context, ScriptCompilation& script, bool hubsOnly = false);
    void parseFunction(CompilerContext* context, ScriptCompilation& script);
    [[nodiscard]] bool resolveFunction(CompilerContext* context, const FunctionCompilation& function);
    // Reports functions returning a value that can reach their end without a `return`.
    void checkReturnPaths(CompilerContext* context, const FunctionCompilation& function) const;
    void inferTypes(CompilerContext* context);
    void reportFieldDiagnostics(CompilerContext* context) const;
    [[nodiscard]] std::vector<const Type*> collectInferredTypes() const;
    void compileFunction(const FunctionCompilation& function);
    void compileObjectInitializer(const FunctionCompilation& initializer);
    void compileConstructor(const FunctionCompilation& constructor);

    Runtime* m_runtime;

    struct ParseStatementFnRegistration {
        PreParseStatementFn preFn;
        ParseStatementFn fn;
        StatementAvailability availability;
    };

    struct OpCodeRegistration {
        NomadId id;
        NomadString name;
        InstructionFn fn;
    };

    struct FunctionLink {
        CallableId functionId;
        NomadIndex callIndex;
    };

//    Function* m_current_function;
    std::unordered_map<NomadString, ParseStatementFnRegistration> m_statements;
    std::vector<OpCodeRegistration> m_opCodeRegistrations;
    std::vector<std::function<void(CompilerContext*, CompilationPhase)>> m_phaseHooks;
    std::vector<SourceRoot> m_sourceRoots;
    bool m_sourceRootsLoaded = false;
    std::vector<ScriptCompilation> m_scripts;
    std::vector<FunctionCompilation> m_functions;
    std::vector<PendingFormatString> m_pendingFormatStrings;
    struct ExternalFieldDeclaration {
        PropertyId id;
        NomadString sourceName;
        NomadIndex line;
        NomadIndex column;
    };
    std::vector<ExternalFieldDeclaration> m_externalFields;
    std::unordered_set<PropertyId, IdHash> m_internalFields;
    std::unordered_set<PropertyId, IdHash> m_readFields;
    std::vector<HubInitializerStatement*> m_scriptInitializers;
    std::unordered_set<NomadString> m_hubVariableDeclarations;
    bool m_allowInitialValueLookup = false;
    CallableId m_scriptInitializerId{};
    std::vector<FunctionLink> m_functionLinks;
    std::unordered_map<CallableId, std::unordered_set<CallableId, IdHash>, IdHash> m_functionCallDependencies;
    std::unordered_set<CallableId, IdHash> m_returningFunctions;
    std::vector<Instruction>& m_instructions;
};

} // nomad
