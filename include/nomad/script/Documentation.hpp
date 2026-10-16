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
//
// Module integration uses SDK module.toml manifests and the ModuleHost/ProjectSession API.
// createProjectSession(project, mode, arguments) resolves development dependencies from the
// SDK's modules directory or uses a release project's resolved list beside nomad.toml.
// Nomad modules share one language library. Their C++ interface requires matching SDK/toolchain
// settings; extern "C" specifies export names, not a stable binary interface.
//
// Sole module entry point: NOMAD_MODULE_EXPORT void
// registerModule(ModuleContext*, ModuleCallbacks*) noexcept(false).
// Registration fills optional std::function callbacks; empty callbacks are skipped.
// Captures may own per-session state. The host destroys callback captures before unloading code.
// ModuleContext supplies Runtime*, const NomadConfiguration*, Logger*, ModuleMode, project,
// resource and module directories, arguments excluding argv[0], and host exit services.
// Registration must unwind its own partial state if it throws. Analysis registration must
// not run scripts or start application/background services. Packaging registration receives
// a null Runtime* and only configures packaging callbacks, without creating application state.
//
// Optional compiler callbacks have signature
// void hook(CompilerContext*), with names configureCompiler,
// preParse, parse, prepareFormatStrings, resolve, emit, link, verify, compilationCompleted.
// Each callback extends the corresponding standard compiler pass.
// Runtime::registerCompilerSetup() configures every compiler subsequently created by that runtime.
//
// Optional prepareCompilation() coordinates resource readiness and must
// call context->compileScripts exactly once, joining any work before returning. Only one module
// may coordinate compilation. Game loads sprites on the main thread while compilation and its
// phase hooks run on a worker; hooks must not call main-thread-only APIs or race runtime state.
// Without a coordinator, ModuleHost::compile() compiles synchronously.
//
// Optional int run() supplies the sole main-thread application loop.
// requestExit(status) is thread-safe and first-request-wins; pollExit() dispatches exit
// once, in reverse registration order, on the host/application thread. Workers request rather
// than dispatch exit. system.exit requests status zero after the current invocation.
// Optional exit() stops execution; shutdown() releases runtime-dependent script state,
// workers and log sinks before Runtime destruction. Callback captures release remaining module
// state after Runtime destruction.
// Libraries remain loaded through destruction, then unload in reverse dependency order.
//
// Optional NomadString checkResources() returns a tooling report or throws.
// Optional void package(ModulePackageContext&) contributes configuration and files without
// application initialization. It receives immutable source and mutable release documents, paths,
// dry-run state and host-controlled output operations. Module output paths must be package-relative;
// collisions and failures abort publication. The host owns the resolved release module list.

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
