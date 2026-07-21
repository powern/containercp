import { qsa, $ } from './dom.js';
import { createPageLifecycle } from './lifecycle.js';
import { destroyModal } from './modals.js';

const routes = new Map();
let currentPage = 'dashboard';
let activeRoute = null;

export function registerRoute(route, page) {
  routes.set(route, typeof page === 'function' ? { mount: page } : page);
}
export function getCurrentPage() { return currentPage; }
export function navigateTo(page) { navigate(page); }
export function navigate(page, params) {
  leaveActiveRoute();
  currentPage = page;
  qsa('.nav-link').forEach(l => l.classList.toggle('active', l.dataset.page === (page === 'site-detail' ? 'sites' : page === 'domain-detail' ? 'domains' : page)));
  const p = $('page');
  if (!p) return;
  p.scrollTop = 0;
  const route = routes.get(page);
  if (!route) return;
  const lifecycle = createPageLifecycle(page);
  activeRoute = { page, route, lifecycle };
  const mount = route.mount || route;
  if (typeof mount === 'function') mount(p, params, lifecycle);
}

export function refreshCurrentPage(params) {
  navigate(currentPage, params);
}

export function leaveActiveRoute() {
  if (!activeRoute) return;
  const previous = activeRoute;
  activeRoute = null;
  if (previous.route && typeof previous.route.unmount === 'function') {
    try { previous.route.unmount(previous.lifecycle); } catch(e) {}
  }
  previous.lifecycle.cleanup();
  destroyModal();
}
