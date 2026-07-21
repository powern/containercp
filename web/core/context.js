import '../js/cache.js';
import '../js/utils.js';
export * from './api.js';
export * from './dom.js';
export * from './utils.js';
export * from './notifications.js';
export * from './modals.js';
export * from './clipboard.js';
export * from './jobs.js';
export * from './lifecycle.js';
export * from './router.js';
export * from '../components/cards.js';
export * from '../components/table.js';
export * from '../components/badges.js';
export * from '../components/filters.js';
export * from '../components/drawer.js';
export * from '../components/empty-state.js';
export * from '../components/copy-button.js';
export * from '../components/status-summary.js';

import { api, apiPost } from './api.js';
import { $, qs, qsa } from './dom.js';
import { esc, escAttr, jsString, dbJsArg } from './utils.js';
import { toast } from './notifications.js';
import { showModal, hideModal, destroyModal } from './modals.js';
import { copyText } from './clipboard.js';
import { navigate, navigateTo } from './router.js';
import { pollJobProgress, pollRotationJob, renderWordPressRotationDiagnostics, renderRotationJobTimeline } from './jobs.js';

window.searchTerm = window.searchTerm || '';
Object.assign(window, {
  $, qs, qsa, api, apiPost, esc, escAttr, jsString, dbJsArg, toast,
  showModal, hideModal, destroyModal, copyText, navigate, navigateTo, pollJobProgress,
  pollRotationJob, renderWordPressRotationDiagnostics, renderRotationJobTimeline
});
