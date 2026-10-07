Nomad language support for Visual Studio Code

This extension provides basic TextMate-based syntax highlighting for Nomad (.nomad) scripts.

Installation

- Open this repository in VS Code.
- Open the `editor` folder as the extension root and press F5 to launch an Extension Development Host.
- Open a `.nomad` file to see the syntax highlighting.

Notes

- The grammar is intentionally simple and based on TextMate regex rules.
- For better semantic highlighting, consider building an LSP that reuses the Nomad compiler's symbol table.
