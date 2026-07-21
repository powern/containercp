export function createPageLifecycle(route) {
  const cleanups = [];
  let active = true;
  let generation = 0;

  const addCleanup = (fn) => {
    if (typeof fn === 'function') cleanups.push(fn);
    return fn;
  };

  const runCleanup = () => {
    if (!active) return;
    active = false;
    generation += 1;
    while (cleanups.length) {
      const cleanup = cleanups.pop();
      try { cleanup(); } catch(e) {}
    }
  };

  return {
    route,
    isActive() { return active; },
    generation() { return generation; },
    onCleanup: addCleanup,
    setTimeout(fn, delay) {
      const id = setTimeout(() => { if (active) fn(); }, delay);
      addCleanup(() => clearTimeout(id));
      return id;
    },
    setInterval(fn, delay) {
      const id = setInterval(() => { if (active) fn(); }, delay);
      addCleanup(() => clearInterval(id));
      return id;
    },
    addEventListener(target, type, listener, options) {
      if (!target || !target.addEventListener) return null;
      target.addEventListener(type, listener, options);
      addCleanup(() => target.removeEventListener(type, listener, options));
      return listener;
    },
    createAbortController() {
      const controller = new AbortController();
      addCleanup(() => controller.abort());
      return controller;
    },
    setRenderTable(fn) {
      const wrapped = (...args) => active ? fn(...args) : undefined;
      window.renderTable = wrapped;
      addCleanup(() => { if (window.renderTable === wrapped) window.renderTable = null; });
      return wrapped;
    },
    cleanup: runCleanup,
  };
}

export function definePage(mount, hooks) {
  return Object.assign({ mount }, hooks || {});
}
