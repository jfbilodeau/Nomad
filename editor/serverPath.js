// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

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

function discoverSdkServer(environment, platform, architecture) {
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
        const candidate = path.join(sdkVersions, version, sdkPlatform, executable);
        if (fs.existsSync(candidate)) return candidate;
    }
    return undefined;
}

function resolveServerCommand(configuredPath, environment = process.env, platform = process.platform,
    architecture = process.arch) {
    if (configuredPath) return configuredPath;
    return discoverSdkServer(environment, platform, architecture) || 'nomad-lsp';
}

module.exports = { resolveServerCommand };
