#!/usr/bin/env node

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const sftp = fs.readFileSync(path.join(root, 'web/pages/sftp-access.js'), 'utf8');

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

assert(sftp.includes('id="download-generated-private-key"'),
  'Generated key modal must expose a dedicated download button');
assert(sftp.includes("downloadButton.addEventListener('click', () => downloadKey(filename, privateKey))"),
  'Private key download must use an event listener with the raw key outside HTML attributes');
assert(sftp.includes("copyButton.addEventListener('click', () => copyText(publicKey))"),
  'Public key copy must use an event listener instead of an inline key argument');
assert(!sftp.includes("downloadSftpKey('"),
  'Private key must not be interpolated into an inline onclick handler');
assert(sftp.includes('document.body.appendChild(a)'),
  'Download anchor must be attached to the document before clicking');
assert(sftp.includes('setTimeout(() =>'),
  'Download object URL cleanup must be deferred until after the click');

console.log('SFTP key download regression tests passed');
