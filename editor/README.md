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
function overloads and parameter descriptions.

Suggestions use the current editor text, including all open buffers in the project.
Closing an edited script restores its disk definitions. Project functions show
their parameters, inferred return type when available, and source location on hover.
Suggestions remain available with incomplete code, though some errors can limit
the available symbols and inferred types.
Project scripts include engine APIs and functions from the configured script roots;
standalone scripts use language built-ins and functions from that document only.
Registered engine variables (including `this.` and `other.`) and discovered script
context variables are included. Object properties and methods are suggested
after a receiver dot; inferred local and parameter types, as well as
registered variable types, filter the candidates. Local variables and parameters
are scoped to the current function and enclosing callbacks, and hover shows their
types. Member hover shows the member signature and documentation. Named
receivers can include chains of object properties, such as `this.name.length`.
For unknown receiver types, candidates list their receiver type in the details.
Completion also works on unfinished member expressions such as `name.`.
Other errors can still prevent local type inference.
Member completion after string literals or arbitrary expressions is not yet supported.
It does not provide signature-help popups or inlay hints.

## Diagnostics

Diagnostics reflect unsaved editor text; you do not need to save to see errors.
Closing a document restores its disk content for other open scripts in the same
project.

The nearest `nomad.toml` determines project resources and script directories.
Nested projects and multi-root workspaces are supported. Each project uses its
own configuration and SDK selection. Saving, creating, or deleting `nomad.toml`
updates SDK selection. Unsaved configuration edits do not change SDK selection.
Project analysis includes scripts on disk, all open script buffers in that
project, the engine API, and entry-function validation. Standalone files without
a project are checked individually with language built-ins only.

Diagnostics update as you edit, save, or change script files on disk.
Scripts are not executed, and sprite packing is not validated. Only saved files
are supported; untitled buffers and navigation are not implemented. Some diagnostics contain
only a line number and therefore highlight the start of that line.

## Syntax grammar

- Regenerate `syntaxes/nomad.tmLanguage.json` with
  `nomadc textmate --output editor/syntaxes/nomad.tmLanguage.json` from the repository root.
