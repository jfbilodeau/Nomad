Nomad language support for Visual Studio Code

This extension provides basic TextMate-based syntax highlighting for Nomad (.nomad) scripts.

Installation

- Open this repository in VS Code.
- Open the `editor` folder as the extension root and press F5 to launch an Extension Development Host.
- Open a `.nomad` file to see the syntax highlighting.
- Release builds include a `nomad-language-<version>.vsix` package. Install it with
  `code --install-extension nomad-language-<version>.vsix`.

Notes

- The grammar is intentionally simple and based on TextMate regex rules.
- Regenerate `syntaxes/nomad.tmLanguage.json` with
  `nomadc textmate --output editor/syntaxes/nomad.tmLanguage.json` from the repository root.
  The compiler supplies registered keywords, statements, native functions, intrinsic members,
  and built-in types from the same runtime metadata used by the generated reference documentation.
