import { esc } from '../core/utils.js';
export function badge(label, className) { return '<span class="badge '+(className || '')+'">'+esc(label)+'</span>'; }
export function statusBadge(status, map) { const cls = map && map[status] ? map[status] : 'badge-info'; return badge(status, cls); }
