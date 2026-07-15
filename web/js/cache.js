// Cache module — in-memory cache with TTL for DNS, Runtime, and Health data.
// All caches are keyed stores with automatic expiry.
// Health Score is currently frontend-calculated.
// TODO: Move Health Score calculation to backend as the single source of truth.
//       Frontend should only display the pre-computed value from the API.

window.DnsCache = {
  _store: {},

  get(domain) {
    const entry = this._store[domain];
    if (!entry) return null;
    if (entry.loading) return 'loading';
    if (Date.now() - entry.timestamp > 60000) {
      delete this._store[domain];
      return null;
    }
    return entry.data;
  },

  set(domain, data) {
    this._store[domain] = {data, timestamp: Date.now(), loading: false};
  },

  setLoading(domain) {
    this._store[domain] = {data: null, timestamp: Date.now(), loading: true};
  },

  isLoading(domain) {
    const entry = this._store[domain];
    return entry && entry.loading;
  },

  // Wait for in-flight request to complete
  async waitFor(domain) {
    while (this.isLoading(domain)) {
      await new Promise(r => setTimeout(r, 100));
    }
    return this.get(domain);
  },

  clear(domain) {
    delete this._store[domain];
  }
};

window.RuntimeCache = {
  _store: {},

  get(siteId) {
    const entry = this._store[siteId];
    if (!entry) return null;
    if (Date.now() - entry.timestamp > 30000) {
      delete this._store[siteId];
      return null;
    }
    return entry.data;
  },

  set(siteId, data) {
    this._store[siteId] = {data, timestamp: Date.now()};
  },

  clear(siteId) {
    delete this._store[siteId];
  }
};
