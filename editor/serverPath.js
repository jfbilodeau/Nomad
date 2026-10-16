// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const toml = require('@iarna/toml');

function findProjectRoot(scriptPath) {
    let directory = path.dirname(scriptPath);
    while (true) {
        if (fs.existsSync(path.join(directory, 'nomad.toml'))) return directory;
        const parent = path.dirname(directory);
        if (parent === directory) return undefined;
        directory = parent;
    }
}

function projectVersion(projectRoot) {
    const filename = path.join(projectRoot, 'nomad.toml');
    const configuration = toml.parse(fs.readFileSync(filename, 'utf8'));
    const version = configuration.nomad?.version;
    if (typeof version !== 'string' || !/^\d+\.\d+\.\d+$/.test(version)) {
        throw new Error(`${filename}: nomad.version must be a major.minor.patch string.`);
    }
    return version;
}

function compareVersions(left, right) {
    const leftParts = left.split('.').map(Number);
    const rightParts = right.split('.').map(Number);
    for (let index = 0; index < 3; index++) {
        if (leftParts[index] !== rightParts[index]) {
            return rightParts[index] - leftParts[index];
        }
    }
    return 0;
}

function sdkPlatformDirectory(platform, architecture) {
    if (platform === 'win32' && architecture === 'x64') return 'windows-x64';
    if (platform === 'linux' && architecture === 'x64') return 'linux-x64';
    if (platform === 'darwin' && architecture === 'x64') return 'macos-x64';
    if (platform === 'darwin' && architecture === 'arm64') return 'macos-arm64';
    return undefined;
}

function sdkHome(environment, platform) {
    if (environment.NOMAD_HOME) return environment.NOMAD_HOME;
    if (platform === 'win32') {
        return environment.LOCALAPPDATA
            ? path.join(environment.LOCALAPPDATA, 'Nomad')
            : undefined;
    }
    const dataHome = environment.XDG_DATA_HOME || path.join(os.homedir(), '.local', 'share');
    return path.join(dataHome, 'nomad');
}

function discoverSdkServer(environment, platform, architecture, minimumVersion) {
    const home = sdkHome(environment, platform);
    const sdkPlatform = sdkPlatformDirectory(platform, architecture);
    if (!home || !sdkPlatform) return undefined;

    const sdkVersions = path.join(home, 'sdks');
    if (!fs.existsSync(sdkVersions)) return undefined;

    const versions = fs.readdirSync(sdkVersions, { withFileTypes: true })
        .filter(entry => entry.isDirectory() && /^\d+\.\d+\.\d+$/.test(entry.name))
        .map(entry => entry.name)
        .sort(compareVersions);
    const executable = platform === 'win32' ? 'nomad-lsp.exe' : 'nomad-lsp';
    for (const version of versions) {
        if (minimumVersion && compareVersions(version, minimumVersion) > 0) continue;
        const candidate = path.join(sdkVersions, version, sdkPlatform, executable);
        if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) return candidate;
    }
    return undefined;
}

function resolveServerCommand(configuredPath, environment = process.env, platform = process.platform,
    architecture = process.arch, projectRoot) {
    if (configuredPath) return configuredPath;
    const minimumVersion = projectRoot ? projectVersion(projectRoot) : undefined;
    const command = discoverSdkServer(environment, platform, architecture, minimumVersion);
    if (command) return command;
    if (minimumVersion) {
        throw new Error(
            `No installed Nomad SDK with nomad-lsp satisfies ${minimumVersion} for ${projectRoot}. ` +
            'Install a compatible SDK or set nomad.languageServer.path.'
        );
    }
    return 'nomad-lsp';
}

module.exports = { findProjectRoot, resolveServerCommand };
