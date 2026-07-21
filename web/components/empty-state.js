import { esc } from '../core/utils.js';
export function emptyState(message) { return '<div class="empty-state">' + esc(message || 'No data') + '</div>'; }
export function loadingState(message) { return '<div class="empty-state">' + esc(message || 'Loading...') + '</div>'; }
export function errorState(message) { return '<div class="empty-state">' + esc(message || 'Failed to load data') + '</div>'; }
