// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

test('routes documents to separate project clients and reselects on configuration changes', async () => {
    const roots = new Map();
    const document = filename => ({
        uri: { scheme: 'file', fsPath: filename, path: filename },
        languageId: 'nomad'
    });
    const first = document('/project/first.nomad');
    const nested = document('/project/nested/second.nomad');
    const standalone = document('/standalone.nomad');
    const broken = document('/broken/third.nomad');
    roots.set(first.uri.fsPath, '/project');
    roots.set(nested.uri.fsPath, '/project/nested');
    roots.set(broken.uri.fsPath, '/broken');
    const documents = [first, nested, standalone, broken];
    const instances = [];
    const errors = [];
    let onOpen;
    let onChange;
    const subscription = { dispose() {} };
    const watcher = {
        onDidCreate: () => subscription,
        onDidChange: callback => { onChange = callback; return subscription; },
        onDidDelete: () => subscription
    };
    const vscode = {
        workspace: {
            textDocuments: documents,
            getConfiguration: () => ({ get: (_key, fallback) => fallback }),
            createFileSystemWatcher: () => watcher,
            onDidOpenTextDocument: callback => { onOpen = callback; return subscription; }
        },
        window: {
            createOutputChannel: () => ({ appendLine() {}, dispose() {} }),
            showErrorMessage: message => errors.push(message)
        }
    };
    class LanguageClient {
        constructor(id, name, server, options) {
            Object.assign(this, { id, name, server, options, opened: [], disposed: false });
            instances.push(this);
        }
        async start() {
            for (const candidate of documents) {
                await this.options.middleware.didOpen(candidate, value => this.opened.push(value));
            }
        }
        async dispose() { this.disposed = true; }
    }
    const module = { exports: {} };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, 'extension.js'), 'utf8'), {
        module,
        process,
        require: name => {
            if (name === 'vscode') return vscode;
            if (name === 'vscode-languageclient/node') return { LanguageClient, TransportKind: { stdio: 0 } };
            if (name === './serverPath') return {
                findProjectRoot: filename => roots.get(filename),
                resolveServerCommand: (_configured, _environment, _platform, _architecture, root) => {
                    if (root === '/broken') throw new Error('No compatible SDK for /broken');
                    return `${root || '/latest'}/nomad-lsp`;
                }
            };
            throw new Error(`Unexpected dependency: ${name}`);
        }
    });
    const extension = module.exports;
    try {
        await extension.activate({ subscriptions: [] });
        assert.equal(instances.length, 3);
        assert.equal(errors.length, 1);
        assert.match(errors[0], /No compatible SDK/);
        for (const [index, candidate] of [first, nested, standalone].entries()) {
            const client = instances[index];
            assert.equal(client.opened.length, 1);
            assert.equal(client.opened[0], candidate);
            let changes = 0;
            for (const other of documents) {
                client.options.middleware.didChange({ document: other }, () => changes++);
            }
            assert.equal(changes, 1);
        }
        await onOpen(first);
        assert.equal(instances.length, 3);
        onChange({ path: '/project/nomad.toml' });
        await onOpen(first);
        assert.equal(instances.length, 6);
        assert(instances.slice(0, 3).every(client => client.disposed));
    } finally {
        await extension.deactivate();
    }
    assert(instances.every(client => client.disposed));
});
