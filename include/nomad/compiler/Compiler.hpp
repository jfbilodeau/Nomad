// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Operators.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Source.hpp>
#include <nomad/compiler/StatementParsers.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/Type.hpp>

#include <functional>
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

class FunctionVariableDefinition {
public:


private:
    NomadString m_name;
    const Type* m_type;
    AssignmentStatement* m_assignmentNode;
};

class Compiler {
public:
    explicit Compiler(Runtime* runtime, std::vector<Instruction>& instructions);
    Compiler(const Compiler& other) = delete;
    ~Compiler();

    [[nodiscard]] Runtime* getRuntime() const;

    // Error reporting
    [[noreturn]]
    void reportError(const NomadString& message) const;
    [[noreturn]]
    void reportError(const NomadString& message, const ScriptFunction* function, const Tokenizer* tokenizer) const;
    [[noreturn]]
    void reportError(const NomadString& message, const NomadString& functionName, NomadIndex line, NomadIndex column) const;
    [[noreturn]]
    [[noreturn]] void reportInternalError(const NomadString& message) const;

    // Statements
    void registerParseStatementFn(
        const NomadString& name,
        ParseStatementFn fn,
        PreParseStatementFn preFn = nullptr
    );
    [[nodiscard]] bool isStatement(const NomadString& name) const;
    [[nodiscard]] bool getParseStatementFn(const NomadString& name, ParseStatementFn& fn) const;
    [[nodiscard]] bool getGetPreParseStatementFn(const NomadString& name, PreParseStatementFn& fn) const;
    void getRegisteredStatements(std::vector<NomadString>& parsers) const;

    // Constant folding
    void registerUnaryOperator(UnaryOperator op, const Type* operand, const Type* result, const NomadString& opCodeName, UnaryFoldingFn fn);
    void registerBinaryOperator(BinaryOperator op, const Type* lhs, const Type* rhs, const Type* result, const NomadString& opCodeName, BinaryFoldingFn fn);
    [[nodiscard]] const Type* getUnaryOperatorResultType(UnaryOperator op, const Type* operandType) const;
    [[nodiscard]] const Type* getBinaryOperatorResultType(BinaryOperator op, const Type* lhsType, const Type* rhsType) const;
    [[nodiscard]] NomadId getUnaryOperatorOpCodeId(UnaryOperator op, const Type* operand) const;
    [[nodiscard]] NomadId getBinaryOperatorOpCodeId(BinaryOperator op, const Type* lhs, const Type* rhs) const;
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
    [[nodiscard]] NomadString resolveName(
        const NomadString& name,
        const ScriptFunction* function = nullptr,
        NomadString hub = {}
    ) const;
    IdentifierType getIdentifierType(const NomadString& name, const ScriptFunction* function) const;
    void getIdentifierDefinition(const NomadString& name, const ScriptFunction* function, IdentifierDefinition& definition) const;
    [[nodiscard]] bool getNativeMemberConflict(const NomadString& identifier, const Type* type,
                                                  const ScriptFunction* function,
                                                  NomadString& conflictingIdentifier) const;

    // Opcode generation
    [[nodiscard]] NomadIndex getOpCodeSize() const;

    NomadIndex addOpCode(NomadId opCode);
    NomadIndex addOpCode(const NomadString& opCodeName);
    NomadIndex addOpCode(InstructionFn opCode);
    void addFunctionReturn(const ScriptFunction* function);
    NomadIndex addId(NomadId id);
    NomadIndex addIndex(NomadIndex index);
    NomadIndex addInteger(NomadInteger value);
    NomadIndex addFloat(NomadFloat value);
    NomadIndex addLoadValue(const Type* type, const RuntimeValue& value, ExpressionTarget target);
    NomadIndex addLoadBooleanValue(NomadBoolean value, ExpressionTarget target);
    NomadIndex addLoadFloatValue(NomadFloat value, ExpressionTarget target);
    NomadIndex addLoadIntegerValue(NomadInteger value, ExpressionTarget target);
    NomadIndex addLoadStringValue(const NomadString& value, ExpressionTarget target);
    // Emit the opcode for `target` from `opCodes` followed by `operand`.
    NomadIndex addLoadTarget(const TargetOpCodes& opCodes, NomadId operand, ExpressionTarget target);
    NomadIndex addLoadFunctionVariable(NomadId variableId, const Type* type, ExpressionTarget target);
    NomadIndex addLoadDynamicVariable(NomadId variableId, const Type* type, ExpressionTarget target);
    NomadIndex addLoadContextVariable(NomadId contextVariableId, const Type* type, ExpressionTarget target);
    NomadIndex addLoadParameter(NomadIndex parameterId, const Type* type, ExpressionTarget target);

    NomadIndex addPushResult(const Type* type);
    // Moves a freshly computed value from the `r` register to target. String buffers are moved, not copied.
    void addMoveResult(const Type* type, ExpressionTarget target);
    void addMoveIntermediateToResult(const Type* type);
    NomadIndex addPushIntermediate(const Type* type);
    NomadIndex addPopResult(const Type* type);
    NomadIndex addPopIntermediate(const Type* type);
    // Emit a free opcode taken from `TypeOpCodes`. Emits nothing when `freeOpCode` is `nullptr` (plain values).
    void addFreeValue(InstructionFn freeOpCode);
    // Same, for free opcodes taking an operand (stack offset from the top or function variable index).
    void addFreeValue(InstructionFn freeOpCode, NomadIndex operand);

    NomadIndex addFunctionCall(NomadId targetFunctionId, ScriptFunction* function, const ArgumentList* arguments);
    NomadIndex addNativeFunctionCall(NomadId nativeFunctionId, ScriptFunction* function, const ArgumentList* arguments);
    NomadIndex addConstructorCall(NomadId constructorId, ScriptFunction* function, const ArgumentList* arguments);
    NomadIndex addNativeMethodCall(NomadId memberId, ScriptFunction* function, const ArgumentList* arguments);
    // After a call returns, free the arguments still on the stack.
    void addFreeArguments(const ArgumentList* arguments);

    void setOpCode(NomadIndex index, const NomadString& opCodeName);
    void setOpCode(NomadIndex index, NomadId opCodeId);
    void setId(NomadIndex index, NomadId id);
    void setIndex(NomadIndex index, NomadIndex value);

    // The only script-input API. Copies snapshots and immediately reports loading and registration diagnostics.
    void registerSources(CompilerContext* context, const SourceBatch& batch);
    // Parser interface: creates a function belonging to an already registered script.
    // Returns NOMAD_INVALID_ID and reports to context if the function name is already used.
    [[nodiscard]] NomadId createFunction(CompilerContext* context, NomadId scriptId, const NomadString& functionName);
    [[nodiscard]] NomadId getScriptId(NomadId functionId) const;
    [[nodiscard]] const ScriptSource& getScript(NomadId scriptId) const;
    void setFunctionNode(NomadId functionId, std::unique_ptr<FunctionNode> ast);
    // Borrows an AST node until placeholder preparation, before type inference.
    void deferFormatString(FormatStringLiteral* literal, NomadId functionId, NomadString sourceContent);
    void registerPhaseHook(std::function<void(CompilerContext*, CompilationPhase)> hook);
    [[nodiscard]] bool compileScripts(
        CompilerContext* context,
        CompilationTarget target = CompilationTarget::Executable
    );
    [[nodiscard]] bool compileFunctions(CompilerContext* context);
    void addFunctionLink(NomadId functionId, NomadIndex callIndex);
    // Function calls used in expressions, recorded while resolving. Explains return types that cannot be inferred.
    void addFunctionCallDependency(NomadId callerFunctionId, NomadId calleeFunctionId);
    // True if calleeFunctionId leads back to callerFunctionId or to itself through recorded function calls.
    [[nodiscard]] bool isRecursiveFunctionCall(NomadId callerFunctionId, NomadId calleeFunctionId) const;
    // Records that a function contains a `return` statement. Functions without one return void.
    void addReturningFunction(NomadId functionId);
    void linkFunctions();
    void postCompile(CompilerContext* context);

    // Generate function names for internal functions
    NomadString generateFunctionName(const NomadString& generatedName, const NomadString& hostFunctionName, NomadIndex line);
    NomadString generateFunctionName(const NomadString& name, const ScriptFunction* function, NomadIndex line);

private:
    struct FunctionCompilation {
        NomadId scriptId = NOMAD_INVALID_ID;
        std::unique_ptr<FunctionNode> ast;
        ScriptFunction* function;
    };

    struct ScriptCompilation {
        ScriptSource script;
        // Only exists while compileScripts() runs so lexing errors are reported once.
        std::unique_ptr<Tokenizer> tokens;

        NomadId functionId = NOMAD_INVALID_ID;
    };

    struct PendingFormatString {
        FormatStringLiteral* literal;
        NomadId functionId;
        NomadString sourceContent;
    };

//    void compile_init_function(Function* function);
    void registerScript(CompilerContext* context, const ScriptSource& script);
    void preParseFunction(CompilerContext* context, ScriptCompilation& script, bool hubsOnly = false);
    void parseFunction(CompilerContext* context, ScriptCompilation& script);
    [[nodiscard]] bool resolveFunction(CompilerContext* context, const FunctionCompilation& function);
    // Reports functions returning a value that can reach their end without a `return`.
    void checkReturnPaths(CompilerContext* context, const FunctionCompilation& function) const;
    void inferTypes(CompilerContext* context);
    [[nodiscard]] std::vector<const Type*> collectInferredTypes() const;
    void compileFunction(const FunctionCompilation& function);

    Runtime* m_runtime;

    struct ParseStatementFnRegistration {
        PreParseStatementFn preFn;
        ParseStatementFn fn;
    };

    struct OpCodeRegistration {
        NomadId id;
        NomadString name;
        InstructionFn fn;
    };

    struct FunctionLink {
        NomadId functionId;
        NomadIndex callIndex;
    };

//    Function* m_current_function;
    std::unordered_map<NomadString, ParseStatementFnRegistration> m_statements;
    std::vector<OpCodeRegistration> m_opCodeRegistrations;
    std::vector<std::function<void(CompilerContext*, CompilationPhase)>> m_phaseHooks;
    std::vector<ScriptCompilation> m_scripts;
    std::vector<FunctionCompilation> m_functions;
    std::vector<PendingFormatString> m_pendingFormatStrings;
    std::vector<NomadString> m_hubVariableNames;
    std::vector<FunctionLink> m_functionLinks;
    std::unordered_map<NomadId, std::unordered_set<NomadId>> m_functionCallDependencies;
    std::unordered_set<NomadId> m_returningFunctions;
    std::vector<Instruction>& m_instructions;
};

} // nomad
