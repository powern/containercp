import { toast } from './notifications.js';

export function copyText(text, msg) {
  navigator.clipboard.writeText(text || '');
  toast(msg || 'Copied to clipboard', 'success');
}
