// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const { resolveServerCommand } = require('./serverPath');

test('explicit server path takes precedence over SDK discovery', () => {
    assert.equal(
        resolveServerCommand('C:\\custom\\nomad-lsp.exe', { NOMAD_HOME: 'missing' }, 'win32', 'x64'),
        'C:\\custom\\nomad-lsp.exe'
    );
});

test('discovers the newest installed platform SDK', t => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'nomad-lsp-sdk-'));
    t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
    const environment = { NOMAD_HOME: directory };
    for (const version of ['1.9.0', '1.10.0']) {
        const executable = path.join(directory, 'sdks', version, 'windows-x64', 'nomad-lsp.exe');
        fs.mkdirSync(path.dirname(executable), { recursive: true });
        fs.writeFileSync(executable, '');
    }

    assert.equal(
        resolveServerCommand('', environment, 'win32', 'x64'),
        path.join(directory, 'sdks', '1.10.0', 'windows-x64', 'nomad-lsp.exe')
    );
});

test('falls back to PATH when no installed SDK server is found', t => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'nomad-lsp-sdk-'));
    t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
    assert.equal(resolveServerCommand('', { NOMAD_HOME: directory }, 'linux', 'x64'), 'nomad-lsp');
});
