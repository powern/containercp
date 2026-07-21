import { qsa, $ } from './dom.js';

const routes = new Map();
let currentPage = 'dashboard';

export function registerRoute(route, handler) { routes.set(route, handler); }
export function getCurrentPage() { return currentPage; }
export function navigateTo(page) { navigate(page); }
export function navigate(page, params) {
  currentPage = page;
  qsa('.nav-link').forEach(l => l.classList.toggle('active', l.dataset.page === (page === 'site-detail' ? 'sites' : page === 'domain-detail' ? 'domains' : page)));
  const p = $('page');
  if (!p) return;
  p.scrollTop = 0;
  const handler = routes.get(page);
  if (handler) handler(p, params);
}
window.navigate = navigate;
window.navigateTo = navigateTo;
