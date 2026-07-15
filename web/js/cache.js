// Cache module — in-memory cache with TTL for DNS, Runtime, and Health data.
// Health Score is currently frontend-calculated.
// TODO: Move Health Score calculation to backend as the single source of truth.
//       Frontend should only display the pre-computed value from the API.

// Build canonical cache key from FQDN and optional types.
// Normalizes: lowercase FQDN, sorted unique uppercase types.
function cacheKey(fqdn, types) {
  const normDomain = fqdn.toLowerCase().replace(/\.+$/, '');
  if (!types) return normDomain;
  const typeList = types.split(',').map(t => t.trim().toUpperCase()).filter(Boolean);
  const unique = [...new Set(typeList)].sort();
  return normDomain + '|' + unique.join(',');
}

window.DnsCache = {
  _store: {},

  get(domain, types) {
    const key = cacheKey(domain, types);
    const entry = this._store[key];
    if (!entry) return null;
    if (entry.loading) return 'loading';
    if (Date.now() - entry.timestamp > 60000) {
      delete this._store[key];
      return null;
    }
    return entry.data;
  },

  set(domain, types, data) {
    const key = cacheKey(domain, types);
    this._store[key] = {data, timestamp: Date.now(), loading: false};
  },

  setLoading(domain, types) {
    const key = cacheKey(domain, types);
    this._store[key] = {data: null, timestamp: Date.now(), loading: true};
  },

  isLoading(domain, types) {
    const key = cacheKey(domain, types);
    const entry = this._store[key];
    return entry && entry.loading;
  },

  // Wait for in-flight request to complete
  async waitFor(domain, types) {
    const key = cacheKey(domain, types);
    while (this.isLoading(domain, types)) {
      await new Promise(r => setTimeout(r, 100));
    }
    return this.get(domain, types);
  },

  // Clear specific type variant, or all variants for a domain
  clear(domain, types) {
    if (types) {
      const key = cacheKey(domain, types);
      delete this._store[key];
    } else {
      // Clear ALL variants for this domain
      const prefix = domain.toLowerCase().replace(/\.+$/, '') + '|';
      for (const key of Object.keys(this._store)) {
        if (key === prefix.replace('|', '') || key.startsWith(prefix)) {
          delete this._store[key];
        }
      }
    }
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
