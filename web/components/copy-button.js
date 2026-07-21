import { escAttr } from '../core/utils.js';
export function copyButton(text, label) { return '<button class="btn btn-sm" data-copy="' + escAttr(text || '') + '">' + (label || 'Copy') + '</button>'; }
