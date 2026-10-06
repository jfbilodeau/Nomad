// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Operators.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/StatementParsers.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/Type.hpp>

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

class FunctionDefinition {
public:
    FunctionDefinition(Function* function);



private:
    Function* m_function;
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
    void reportError(const NomadString& message, const Function* function, const Tokenizer* tokenizer) const;
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
    IdentifierType getIdentifierType(const NomadString& name, const Function* function) const;
    void getIdentifierDefinition(const NomadString& name, const Function* function, IdentifierDefinition& definition) const;

    // Opcode generation
    [[nodiscard]] NomadIndex getOpCodeSize() const;

    NomadIndex addOpCode(NomadId opCode);
    NomadIndex addOpCode(const NomadString& opCodeName);
    NomadIndex addOpCode(InstructionFn opCode);
    void addFunctionReturn(const Function* function);
    NomadIndex addId(NomadId id);
    NomadIndex addIndex(NomadIndex index);
    NomadIndex addInteger(NomadInteger value);
    NomadIndex addFloat(NomadFloat value);
    NomadIndex addLoadValue(const Type* type, const RuntimeValue& value, ExpressionTarget target);
    NomadIndex addLoadBooleanValue(NomadBoolean value, ExpressionTarget target);
    NomadIndex addLoadFloatValue(NomadFloat value, ExpressionTarget target);
    NomadIndex addLoadIntegerValue(NomadInteger value, ExpressionTarget target);
    NomadIndex addLoadStringValue(const NomadString& value, ExpressionTarget target);
    // Load a borrowed pointer to an interned copy of `value` (`$stringref`). Nothing needs to be freed.
    NomadIndex addLoadStringReference(const NomadString& value, ExpressionTarget target);
    // Emit the opcode for `target` from `opCodes` followed by `operand`.
    NomadIndex addLoadTarget(const TargetOpCodes& opCodes, NomadId operand, ExpressionTarget target);
    NomadIndex addLoadFunctionVariable(NomadId variableId, const Type* type, ExpressionTarget target);
    NomadIndex addLoadDynamicVariable(NomadId variableId, const Type* type, ExpressionTarget target);
    NomadIndex addLoadContextVariable(NomadId contextVariableId, const Type* type, ExpressionTarget target);
    NomadIndex addLoadParameter(NomadIndex parameterId, const Type* type, ExpressionTarget target);

    NomadIndex addPushResult(const Type* type);
    // Moves a freshly computed value from the `r` register to target. String buffers are moved, not copied.
    void addMoveResult(const Type* type, ExpressionTarget target);
    NomadIndex addPushIntermediate(const Type* type);
    NomadIndex addPopIntermediate(const Type* type);
    // Emit a free opcode taken from `TypeOpCodes`. Emits nothing when `freeOpCode` is `nullptr` (plain values).
    void addFreeValue(InstructionFn freeOpCode);
    // Same, for free opcodes taking an operand (stack offset from the top or function variable index).
    void addFreeValue(InstructionFn freeOpCode, NomadIndex operand);

    NomadIndex addFunctionCall(NomadId targetFunctionId, Function* function, const ArgumentList* arguments);
    NomadIndex addNativeFunctionCall(NomadId nativeFunctionId, Function* function, const ArgumentList* arguments);
    // After a call returns, free the owned arguments still on the stack. Lent arguments (`$stringref`) are skipped.
    void addFreeArguments(const ArgumentList* arguments);

    void setOpCode(NomadIndex index, const NomadString& opCodeName);
    void setOpCode(NomadIndex index, NomadId opCodeId);
    void setId(NomadIndex index, NomadId id);
    void setIndex(NomadIndex index, NomadIndex value);

    // Returns NOMAD_INVALID_ID if the function name is already used. The error is reported by compileFunctions().
    NomadId registerScriptFile(const NomadString& functionName, const NomadString& fileName, const NomadString& source);
    NomadId registerFunctionSource(const NomadString& functionName, const NomadString& fileName, const NomadString& source);
    void setFunctionNode(NomadId functionId, std::unique_ptr<FunctionNode> ast);
    void loadScriptsFromPath(const NomadString& path);
    // Compiles every registered script file. Errors and warnings are reported to context. Returns false if errors
    // were reported. A pass that reports errors stops the compilation before the next pass to avoid cascading errors.
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
    NomadString generateFunctionName(const NomadString& name, const Function* function, NomadIndex line);

private:
    struct FunctionSource {
        NomadString source;
        std::unique_ptr<FunctionNode> ast;
        Function* function;
    };

    struct ScriptFile {
        NomadString functionName;
        NomadString fileName;
        NomadString source;
        // Only exists while compileFunctions() runs so lexing errors are reported once.
        std::unique_ptr<Tokenizer> tokens;

        NomadId functionId;
    };

    // Errors found while loading script files, before a CompilerContext is available.
    struct LoadError {
        NomadString message;
        NomadString sourceName;
    };

//    void compile_init_function(Function* function);
    void preParseFunction(CompilerContext* context, ScriptFile& scriptFile);
    void parseFunction(CompilerContext* context, ScriptFile& file);
    [[nodiscard]] bool resolveFunction(CompilerContext* context, const FunctionSource& source);
    // Reports functions returning a value that can reach their end without a `return`.
    void checkReturnPaths(CompilerContext* context, const FunctionSource& source) const;
    void inferTypes(CompilerContext* context);
    [[nodiscard]] std::vector<const Type*> collectInferredTypes() const;
    void compileFunction(const FunctionSource& source);
    void scanDirectoryForScripts(const NomadString& basePath, const NomadString& sub_path, NomadIndex max_depth);

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
    std::vector<ScriptFile> m_files;
    std::vector<LoadError> m_loadErrors;
    std::vector<FunctionSource> m_sources;
    std::vector<FunctionLink> m_functionLinks;
    std::unordered_map<NomadId, std::unordered_set<NomadId>> m_functionCallDependencies;
    std::unordered_set<NomadId> m_returningFunctions;
    std::vector<Instruction>& m_instructions;
};

} // nomad
