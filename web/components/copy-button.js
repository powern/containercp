import { esc, escAttr } from '../core/utils.js';
export function copyButton(text, label, title) { return '<button class="btn btn-sm" data-copy="' + escAttr(text || '') + '"' + (title ? ' title="' + escAttr(title) + '"' : '') + '>' + esc(label || 'Copy') + '</button>'; }
