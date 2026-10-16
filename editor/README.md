# Nomad language support for Visual Studio Code

This extension provides TextMate syntax highlighting and compiler diagnostics for
Nomad (`.nomad`) scripts. Requires VS Code 1.82 or newer.

## Installation

- Release builds include a `nomad-language-<version>.vsix` package. Install it with
  `code --install-extension nomad-language-<version>.vsix`.
- Install a Nomad SDK containing `nomad-lsp` and add its directory to `PATH`, or
  set `nomad.languageServer.path` to the full executable path. Keep the executable
  alongside its SDK dependencies.
- Open a saved `.nomad` file. Diagnostics appear in the editor and Problems panel.
  The **Nomad Language Server** output channel contains server messages.
- Set `nomad.languageServer.enabled` to `false` for highlighting only.
  Reload the window after changing either language server setting.

## Development

- Open the `editor` folder as the extension root and run `npm ci`.
- Run `npm run check`, then press F5 to launch an Extension Development Host.
- Point `nomad.languageServer.path` at your built `nomad-lsp` executable.

## Diagnostics

The server uses the compiler's existing source batch and filesystem adapter.
Editor text overrides disk content until the document closes; unsaved edits do
not need to be written to disk. Closing a document restores its disk content for
other open scripts in the same project.

The nearest `nomad.toml` determines project resources and script directories.
Project analysis includes scripts on disk, all open script buffers in that
project, the engine API, and entry-function validation. Standalone files without
a project are checked individually with language built-ins only.

This first version analyzes synchronously on full-text changes, saves, and file
watcher events. It does not execute scripts or validate sprite packing. Only
`file:` documents are supported; untitled buffers, completion, navigation, and
incremental compilation are not implemented. Some compiler diagnostics contain
only a line number and therefore highlight the start of that line.

## Syntax grammar

- The grammar is intentionally simple and based on TextMate regex rules.
- Regenerate `syntaxes/nomad.tmLanguage.json` with
  `nomadc textmate --output editor/syntaxes/nomad.tmLanguage.json` from the repository root.
  The compiler supplies registered keywords, statements, native functions, intrinsic members,
  and built-in types from the same runtime metadata used by the generated reference documentation.
