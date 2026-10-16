// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');
const { resolveServerCommand } = require('./serverPath');

let client;

async function activate(context) {
    const configuration = vscode.workspace.getConfiguration('nomad.languageServer');
    if (!configuration.get('enabled', true)) {
        return;
    }

    const output = vscode.window.createOutputChannel('Nomad Language Server');
    const command = resolveServerCommand(configuration.get('path', ''));
    const watcher = vscode.workspace.createFileSystemWatcher('**/*.{nomad,toml}');
    context.subscriptions.push(output, watcher);
    output.appendLine(`Starting language server: ${command}`);

    client = new LanguageClient(
        'nomad',
        'Nomad Language Server',
        {
            command,
            transport: TransportKind.stdio
        },
        {
            documentSelector: [{ scheme: 'file', language: 'nomad' }],
            outputChannel: output,
            synchronize: { fileEvents: watcher }
        }
    );

    try {
        await client.start();
    } catch (error) {
        output.appendLine(String(error));
        vscode.window.showErrorMessage(
            'Could not start the Nomad language server. Install a Nomad SDK, add nomad-lsp to PATH, ' +
            'or set nomad.languageServer.path. See the Nomad Language Server output for details.'
        );
    }
}

async function deactivate() {
    if (client) {
        await client.stop();
        client = undefined;
    }
}

module.exports = { activate, deactivate };
