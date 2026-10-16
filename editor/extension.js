// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');
const { findProjectRoot, resolveServerCommand } = require('./serverPath');

const clients = new Map();
let pending = Promise.resolve();
let stopping = false;

async function activate(context) {
    stopping = false;
    const output = vscode.window.createOutputChannel('Nomad Language Server');
    const watcher = vscode.workspace.createFileSystemWatcher('**/*.{nomad,toml}');
    context.subscriptions.push(output, watcher);

    const startDocumentClient = async document => {
        if (stopping || document.uri.scheme !== 'file' || document.languageId !== 'nomad') return;
        const configuration = vscode.workspace.getConfiguration('nomad.languageServer', document.uri);
        if (!configuration.get('enabled', true)) return;
        const root = findProjectRoot(document.uri.fsPath);
        const key = root || '';
        if (clients.has(key)) return;
        const command = resolveServerCommand(
            configuration.get('path', ''), process.env, process.platform, process.arch, root
        );
        const ownsDocument = candidate => candidate.uri.scheme === 'file' &&
            candidate.languageId === 'nomad' && findProjectRoot(candidate.uri.fsPath) === root;
        const client = new LanguageClient(
            `nomad-${clients.size}`,
            root ? `Nomad: ${root}` : 'Nomad: standalone scripts',
            { command, transport: TransportKind.stdio },
            {
                documentSelector: [{ scheme: 'file', language: 'nomad' }],
                outputChannel: output,
                diagnosticCollectionName: `nomad-${root || 'standalone'}`,
                synchronize: { fileEvents: watcher },
                middleware: {
                    didOpen: (candidate, next) => ownsDocument(candidate) ? next(candidate) : undefined,
                    didChange: (event, next) => ownsDocument(event.document) ? next(event) : undefined,
                    didClose: (candidate, next) => ownsDocument(candidate) ? next(candidate) : undefined,
                    didSave: (candidate, next) => ownsDocument(candidate) ? next(candidate) : undefined
                }
            }
        );
        output.appendLine(`Starting language server for ${root || 'standalone scripts'}: ${command}`);
        clients.set(key, client);
        try {
            await client.start();
        } catch (error) {
            clients.delete(key);
            await client.dispose();
            throw error;
        }
    };

    const reportError = error => {
        output.appendLine(String(error));
        vscode.window.showErrorMessage(`Could not start the Nomad language server: ${error.message}`);
    };
    const enqueue = action => {
        pending = pending.then(action).catch(reportError);
        return pending;
    };
    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(document =>
        enqueue(() => startDocumentClient(document))));

    const restartProjects = uri => {
        if (!uri.path.endsWith('/nomad.toml')) return;
        enqueue(async () => {
            await Promise.all([...clients.values()].map(client => client.dispose()));
            clients.clear();
            for (const document of vscode.workspace.textDocuments) {
                try {
                    await startDocumentClient(document);
                } catch (error) {
                    reportError(error);
                }
            }
        });
    };
    context.subscriptions.push(
        watcher.onDidCreate(restartProjects),
        watcher.onDidChange(restartProjects),
        watcher.onDidDelete(restartProjects)
    );
    for (const document of vscode.workspace.textDocuments) {
        await enqueue(() => startDocumentClient(document));
    }
}

async function deactivate() {
    stopping = true;
    await pending;
    await Promise.all([...clients.values()].map(client => client.dispose()));
    clients.clear();
}

module.exports = { activate, deactivate };
