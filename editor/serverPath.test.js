// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const { findProjectRoot, resolveServerCommand } = require('./serverPath');

function createFixture(t) {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'nomad-lsp-project-'));
    t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
    const project = path.join(directory, 'project');
    fs.mkdirSync(project);
    return {
        directory,
        project,
        environment: { NOMAD_HOME: directory },
        configure: text => fs.writeFileSync(path.join(project, 'nomad.toml'), text),
        install: version => {
            const executable = path.join(directory, 'sdks', version, 'windows-x64', 'nomad-lsp.exe');
            fs.mkdirSync(path.dirname(executable), { recursive: true });
            fs.writeFileSync(executable, '');
            return executable;
        }
    };
}

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

test('finds the nearest project, including nested projects and unsaved files', t => {
    const fixture = createFixture(t);
    fixture.configure('[nomad]\nversion = "1.0.0"');
    const nested = path.join(fixture.project, 'nested');
    fs.mkdirSync(nested);
    fs.writeFileSync(path.join(nested, 'nomad.toml'), '[nomad]\nversion = "2.0.0"');
    assert.equal(findProjectRoot(path.join(nested, 'res', 'new.nomad')), nested);
    assert.equal(findProjectRoot(path.join(fixture.project, 'res', 'new.nomad')), fixture.project);
    assert.equal(findProjectRoot(path.join(fixture.directory, 'standalone.nomad')), undefined);
});

test('selects the newest SDK satisfying the project minimum version', t => {
    const fixture = createFixture(t);
    fixture.configure('[project]\nversion = "99.0.0"\n[nomad]\nversion = "1.10.0" # minimum');
    fixture.install('1.9.0');
    fixture.install('1.10.0');
    const newest = fixture.install('2.0.0');
    assert.equal(
        resolveServerCommand('', fixture.environment, 'win32', 'x64', fixture.project), newest
    );
});

test('accepts an exact matching SDK and valid TOML inline tables', t => {
    const fixture = createFixture(t);
    fixture.configure("nomad = { version = '1.10.0' }");
    fixture.install('1.9.0');
    const exact = fixture.install('1.10.0');
    assert.equal(
        resolveServerCommand('', fixture.environment, 'win32', 'x64', fixture.project), exact
    );
});

test('does not fall back to an older SDK or PATH for a project', t => {
    const fixture = createFixture(t);
    fixture.configure('[nomad]\nversion = "2.0.0"');
    fixture.install('1.9.0');
    assert.throws(
        () => resolveServerCommand('', fixture.environment, 'win32', 'x64', fixture.project),
        /No installed Nomad SDK.*satisfies 2\.0\.0/
    );
});

test('reports missing, invalid, and malformed project versions', t => {
    const fixture = createFixture(t);
    fixture.install('2.0.0');
    for (const text of ['[project]\nversion = "1.0.0"', '[nomad]\nversion = 2', '[nomad]\nversion = "latest"']) {
        fixture.configure(text);
        assert.throws(
            () => resolveServerCommand('', fixture.environment, 'win32', 'x64', fixture.project),
            /nomad.version must/
        );
    }
    fixture.configure('[nomad\nversion = "1.0.0"');
    assert.throws(() => resolveServerCommand('', fixture.environment, 'win32', 'x64', fixture.project));
});

test('explicit executable overrides project version selection', t => {
    const fixture = createFixture(t);
    fixture.configure('invalid toml');
    assert.equal(
        resolveServerCommand('custom-server', fixture.environment, 'win32', 'x64', fixture.project),
        'custom-server'
    );
});
