# Nomad language support for Visual Studio Code

This extension provides TextMate syntax highlighting, API completion, hover documentation, and compiler diagnostics for
Nomad (`.nomad`) scripts. Requires VS Code 1.82 or newer.

## Installation

- Release builds include a `nomad-language-<version>.vsix` package. Install it with
  `code --install-extension nomad-language-<version>.vsix`.
- Install a Nomad SDK containing `nomad-lsp`. For scripts in a project, the extension
  reads the nearest `nomad.toml` and selects the newest installed SDK satisfying
  `nomad.version` (a minimum version, matching compiler compatibility rules).
  If none is installed, it reports an error with installation/configuration guidance
  rather than launching an older SDK. It does not download SDKs automatically.
  Without a project, it selects the newest installed SDK, then falls back to `PATH`.
  Set `nomad.languageServer.path` to the full executable path to
  override discovery. Keep the executable alongside its SDK dependencies.
  The default SDK roots are `%LOCALAPPDATA%\Nomad\sdks` on Windows and
  `$XDG_DATA_HOME/nomad/sdks` (or `~/.local/share/nomad/sdks`) on Linux.
  `NOMAD_HOME` overrides the SDK home, with versions located under its `sdks` directory.
  Custom installation locations require an explicit executable path.
- Open a saved `.nomad` file. Diagnostics appear in the editor and Problems panel.
  The **Nomad Language Server** output channel contains server messages.
- Set `nomad.languageServer.enabled` to `false` for highlighting only.
  Reload the window after changing either language server setting.

## Development

- Open the `editor` folder as the extension root and run `npm ci`.
- Run `npm run check`, then press F5 to launch an Extension Development Host.
- Point `nomad.languageServer.path` at your built `nomad-lsp` executable.

## Completion and hover

Press Ctrl+Space for registered keywords, types, constants, native functions,
script names, and project-defined functions.
On an empty prefix, suggestions include keywords, callables, registered/context
variables, and namespace prefixes such as `game.` and `this.`. Types and constants
appear when you type a matching prefix rather than cluttering the blank-line list.
Typing a dot also triggers suggestions, such as the available `game.` APIs.
Hover over a registered symbol to see its signature and documentation, including
function overloads and parameter descriptions. Completion details use the same
runtime metadata as the generated API reference.

Suggestions use the current editor text, including all open buffers in the project.
Closing an edited script restores its disk definitions. Project functions show
their parameters, inferred return type when available, and source location on hover.
The compiler's collected declarations remain available after compilation errors;
errors that prevent declaration discovery can limit the suggestions.
Project scripts include engine APIs and functions from the configured script roots;
standalone scripts use language built-ins and functions from that document only.
Registered engine variables (including `this.` and `other.`) and discovered script
context variables are included. Intrinsic properties and methods are suggested
after a receiver dot; compiler-inferred local and parameter types, as well as
registered variable types, filter the candidates. Local variables and parameters
are scoped to the current function and enclosing callbacks, and hover shows their
types. Intrinsic hover shows the member signature and documentation. Named
receivers can include chains of intrinsic properties, such as `this.name.length`.
For unknown receiver types, candidates list their receiver type in the details.
When an error is on the completion line, the server retries the compiler with that
line blanked in a temporary snapshot. This does not change editor text or published
diagnostics. Other errors can still prevent local type inference.
Member completion after string literals or arbitrary expressions is not yet supported.
It does not provide signature-help popups or inlay hints.

## Diagnostics

The server uses the compiler's existing source batch and filesystem adapter.
Editor text overrides disk content until the document closes; unsaved edits do
not need to be written to disk. Closing a document restores its disk content for
other open scripts in the same project.

The nearest `nomad.toml` determines project resources and script directories.
Separate project roots use separate server processes, including nested projects
and multi-root workspaces. Saving, creating, or deleting `nomad.toml` restarts the
clients and reselects their SDKs. Unsaved configuration edits do not change SDK selection.
Project analysis includes scripts on disk, all open script buffers in that
project, the engine API, and entry-function validation. Standalone files without
a project are checked individually with language built-ins only.

This first version analyzes synchronously on full-text changes, saves, and file
watcher events. It does not execute scripts or validate sprite packing. Only
`file:` documents are supported; untitled buffers, navigation, and
incremental compilation are not implemented. Some compiler diagnostics contain
only a line number and therefore highlight the start of that line.

## Syntax grammar

- The grammar is intentionally simple and based on TextMate regex rules.
- Regenerate `syntaxes/nomad.tmLanguage.json` with
  `nomadc textmate --output editor/syntaxes/nomad.tmLanguage.json` from the repository root.
  The compiler supplies registered keywords, statements, native functions, intrinsic members,
  and built-in types from the same runtime metadata used by the generated reference documentation.
