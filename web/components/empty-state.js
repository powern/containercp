import { esc } from '../core/utils.js';
export function emptyState(message, actionHtml) { return '<div class="empty-state" role="status">' + esc(message || 'No data') + (actionHtml ? '<div style="margin-top:12px;">' + actionHtml + '</div>' : '') + '</div>'; }
export function loadingState(message) { return '<div class="empty-state ui-state-loading" role="status" aria-live="polite">' + esc(message || 'Loading...') + '</div>'; }
export function errorState(message, actionHtml) { return '<div class="empty-state ui-state-error" role="alert">' + esc(message || 'Failed to load data') + (actionHtml ? '<div style="margin-top:12px;">' + actionHtml + '</div>' : '') + '</div>'; }
