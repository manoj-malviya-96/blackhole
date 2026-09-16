#!/usr/bin/env node

import { execFileSync, spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const rootDir = join(__dirname, '..');
const distDir = join(rootDir, 'dist');

const appBundle = join(distDir, 'blackhole_exe.app');
const zipFile = join(distDir, 'blackhole-macos.zip');

if (!existsSync(appBundle)) {
  if (existsSync(zipFile)) {
    console.log('Extracting Black Hole application bundle...');
    const unzipResult = spawnSync('unzip', ['-q', '-o', zipFile, '-d', distDir], {
      stdio: 'inherit'
    });
    if (unzipResult.status !== 0) {
      console.error('Failed to unpack application bundle.');
      process.exit(unzipResult.status ?? 1);
    }
  }
}

if (process.platform === 'darwin') {
  if (existsSync(appBundle)) {
    console.log('Launching Black Hole...');
    execFileSync('open', [appBundle]);
    process.exit(0);
  }
}

const directBin = join(distDir, 'blackhole_exe');
if (existsSync(directBin)) {
  const result = spawnSync(directBin, process.argv.slice(2), { stdio: 'inherit' });
  process.exit(result.status ?? 0);
}

console.error('Error: Could not find executable or application bundle in dist/.');
process.exit(1);
