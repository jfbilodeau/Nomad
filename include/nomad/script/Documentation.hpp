// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <ostream>

namespace nomad {

// Native runtime integration:
// NomadConfiguration owns a mutable TOML document. Copies are independent. readFile() and
// parse() create documents; serialize() preserves arbitrary tables, arrays and values.
// Typed getters use dotted paths: an absent value returns std::nullopt, but a present value
// of the wrong type throws NomadConfigurationError. Required getters also reject absence.
// Setters, erase() and merge() support constructing release configuration without changing
// the source document. Initialization code should receive a const NomadConfiguration&.
//
// ModuleManifest describes an SDK module directory and its direct dependencies. resolveModules()
// reads module.toml manifests without loading libraries, returning each module once, dependencies
// first. Request and dependency array order determine traversal order. Invalid names, missing
// manifests, name mismatches, unsupported schemas, duplicate dependencies and cycles throw
// ModuleError. getModuleLibraryName() uses .dll, lib*.so or lib*.dylib for the host platform.
//
// Logger accepts concurrent submissions and serializes flushes and sink registration/removal.
// A sink must be removed before it is destroyed or its module is unloaded. Consumers must
// synchronize direct access to sink-owned data with delivery. fatal() flushes and throws
// NomadException; the application boundary reports failure after normal stack unwinding.
//
// Compiler::registerPhaseHook() appends a callback receiving CompilerContext* and CompilationPhase.
// Hooks run in registration order before Configure, PreParse, Parse, PrepareFormatStrings,
// Resolve, Emit, Link and Verify; Completed follows successful compilation. A reported error
// stops subsequent hooks and phases. NomadException becomes a diagnostic; NomadBug propagates.
// compileScripts(context, CompilationTarget::Analysis) stops after semantic checks and invokes
// Completed without emission, linking or executable verification. Executable is the default.
// compileFunctions() remains a compatibility entry point. analyzeSources() returns diagnostics
// from analysis without executing scripts or emitting function instructions.

// Forward declarations
class Runtime;

#define NomadDocField \
    NomadString doc;

#define NomadDocParamField \
    NomadString doc;

#define NomadDocArg \
    const NomadString& doc

#define NomadDoc(doc_text) \
    NomadString(doc_text)

#define NomadParamDoc(param_doc) \
    param_doc

// Generates the registered script API reference.
void generateDocumentation(const Runtime* runtime, std::ostream& out);
void generateTypeDocumentation(const Runtime* runtime, std::ostream& out);
void generateIntrinsicMemberDocumentation(const Runtime* runtime, std::ostream& out);
void generateKeywords(Runtime* runtime, std::ostream& out);
// Includes expression interpolation, nested format literals, and the format string operator.
void generateTextMateGrammar(Runtime* runtime, std::ostream& out);

} // namespace nomad
