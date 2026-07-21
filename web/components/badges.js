import { esc } from '../core/utils.js';
const semanticClass = {
  healthy: 'badge-ok', warning: 'badge-warn', critical: 'badge-err', unknown: 'badge-info',
  running: 'badge-ok', stopped: 'badge-err', connected: 'badge-ok', failed: 'badge-err',
  available: 'badge-ok', missing: 'badge-warn', invalid: 'badge-err', managed: 'badge-ok',
  imported: 'badge-warn', valid: 'badge-ok', expiring: 'badge-warn', expired: 'badge-err',
  enabled: 'badge-ok', disabled: 'badge-info', active: 'badge-ok', inactive: 'badge-info',
};

export function badge(label, className, title) {
  return '<span class="badge ' + (className || 'badge-info') + '"' + (title ? ' title="' + esc(title) + '"' : '') + '>' + esc(label) + '</span>';
}

export function statusBadge(status, map) {
  const label = status == null || status === '' ? 'Unknown' : String(status);
  const key = label.toLowerCase().replace(/[^a-z0-9]+/g, '_');
  const normalized = key.replace(/_/g, '-');
  const cls = (map && map[label]) || (map && map[key]) || semanticClass[key] || semanticClass[normalized] || 'badge-info';
  return badge(label, cls);
}

export function healthBadge(state) {
  const value = state || 'Unknown';
  const key = String(value).toLowerCase();
  const labels = { healthy: 'Healthy', warning: 'Warning', critical: 'Critical', unknown: 'Unknown' };
  return badge(labels[key] || value, semanticClass[key] || 'badge-info');
}
