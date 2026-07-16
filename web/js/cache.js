// Cache module — in-memory cache with TTL for DNS, Runtime, and Health data.
// Health Score is currently frontend-calculated.
// TODO: Move Health Score calculation to backend as the single source of truth.
//       Frontend should only display the API-provided health_score field.

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

  async waitFor(domain, types) {
    const key = cacheKey(domain, types);
    while (this.isLoading(domain, types)) {
      await new Promise(r => setTimeout(r, 100));
    }
    return this.get(domain, types);
  },

  clear(domain, types) {
    if (types) {
      const key = cacheKey(domain, types);
      delete this._store[key];
    } else {
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

// Health result cache — single source of truth for Health Score.
// Used by Domain List, Domain Detail header, and Health tab.
// TTL: 60 seconds. Force refresh clears cache and re-loads.
// Concurrent loads: deduplicated via pending promises.
// Stale loader protection: generation counter prevents old loaders from
// overwriting results from newer force-refreshes.
window.HealthCache = {
  _store: {},
  _loaders: {},
  _generation: {},
  TTL_MS: 60000,

  get(domain) {
    var entry = this._store[domain];
    if (!entry) return null;
    if (entry.loading) return 'loading';
    if (Date.now() - entry.timestamp > this.TTL_MS) {
      delete this._store[domain];
      return null;
    }
    return entry.result;
  },

  invalidate(domain) {
    delete this._store[domain];
    delete this._loaders[domain];
    if (this._generation[domain] === undefined) this._generation[domain] = 0;
    this._generation[domain]++;
  },

  // Load full health result for a domain. Called by Domain List, Header, Health tab.
  // Returns existing cached result, or starts loading (deduplicated).
  // Options: { force: true } — bypasses cache, forces fresh load.
  load: function(domainKey, domainRow, mailDomain, serverHostname, options) {
    options = options || {};
    var domain = domainKey || (domainRow && domainRow.domain);

    if (!domain) return Promise.resolve(null);

    // Return cached if fresh and not forced
    if (!options.force) {
      var cached = this.get(domain);
      if (cached && cached !== 'loading') return Promise.resolve(cached);
    }

    // Ensure generation counter exists
    if (this._generation[domain] === undefined) this._generation[domain] = 0;

    // Increment generation on force refresh (invalidates old loaders)
    if (options.force) {
      this._generation[domain]++;
      // Also invalidate store so get() returns null
      delete this._store[domain];
      delete this._loaders[domain];
    }

    // Dedup concurrent loaders (only within same generation)
    if (this._loaders[domain]) return this._loaders[domain];

    var gen = this._generation[domain];

    // Mark loading
    this._store[domain] = {result: null, timestamp: Date.now(), loading: true};

    // Async loader
    this._loaders[domain] = this._doLoad(domain, domainRow, mailDomain, serverHostname)
      .then(function(result) {
        // Stale loader protection: only store if generation matches
        if (window.HealthCache._generation[domain] === gen) {
          window.HealthCache._store[domain] = {result: result, timestamp: Date.now(), loading: false};
        } else {
          // Stale loader — discard result silently
          if (window.HealthCache._store[domain] && window.HealthCache._store[domain].loading) {
            delete window.HealthCache._store[domain];
          }
        }
        delete window.HealthCache._loaders[domain];
        return result;
      })
      .catch(function(err) {
        console.error('HealthCache.load failed for ' + domain, err);
        if (window.HealthCache._generation[domain] === gen) {
          delete window.HealthCache._store[domain];
        }
        delete window.HealthCache._loaders[domain];
        return null;
      });

    return this._loaders[domain];
  },

  // Internal: fetch all data, compute score, return result
  _doLoad: async function(domain, domainRow, mailDomain, serverHostname) {
    if (!domain) return null;

    // Per-resource fetch states: "pending" | "success" | "error"
    function makeState(promiseOrValue) {
      return {state: 'success', data: promiseOrValue};
    }

    var ctx = {
      domainRow: domainRow || {},
      mailDomain: mailDomain || null,
      serverHostname: serverHostname || '',
      // Fetch states for each resource — used by scoring engine
      fetchStates: {},
    };

    // Helper: attempt fetch, return {state, data} or {state:'error', error:...}
    async function tryFetch(label, fn) {
      ctx.fetchStates[label] = {state: 'pending', data: null, error: null};
      try {
        var data = await fn();
        ctx.fetchStates[label] = {state: 'success', data: data, error: null};
        return data;
      } catch (e) {
        console.error('Health ' + label + ' fetch failed for ' + domain, e);
        ctx.fetchStates[label] = {state: 'error', data: null, error: e.message || String(e)};
        return null;
      }
    }

    // 1. Root DNS
    ctx.rootDns = await tryFetch('rootDns', function() {
      return fetchDnsForFqdn(domain, 'A,AAAA,MX,TXT,NS,CAA');
    });

    // 2. MailDomain data (fresh via API if not provided)
    var mail = mailDomain;
    if (!mail && domainRow && domainRow.mail_domain_id && domainRow.mail_domain_id > 0) {
      await tryFetch('mailDomains', async function() {
        var mdRes = await api('/api/mail/domains');
        if (mdRes && mdRes.data) {
          mail = mdRes.data.find(function(m) { return m.domain === domain || m.domain_id === domainRow.id; }) || null;
        }
        return mail;
      });
    }

    // 3. Mail DNS sub-requests
    if (mail && mail.mode && mail.mode !== 'disabled') {
      ctx.mailDomain = mail;
      if (mail.dkim_public_key_dns) {
        var sel = mail.dkim_selector || 'dkim';
        ctx.dkimDns = await tryFetch('dkim', function() {
          return fetchDnsForFqdn(sel + '._domainkey.' + domain, 'TXT');
        });
      }
      ctx.dmarcDns = await tryFetch('dmarc', function() {
        return fetchDnsForFqdn('_dmarc.' + domain, 'TXT');
      });
      if (mail.mode === 'local-primary') {
        ctx.mtaStsDns = await tryFetch('mtaSts', function() {
          return fetchDnsForFqdn('_mta-sts.' + domain, 'TXT');
        });
        if (serverHostname) {
          ctx.autoDns = await tryFetch('autodiscover', function() {
            return fetchDnsForFqdn('autodiscover.' + domain, 'CNAME,A');
          });
        }
      }
    } else if (domainRow && domainRow.mail_domain_id && domainRow.mail_domain_id > 0) {
      // MailDomain exists but fetch failed — mark as error, don't fabricate data
      ctx.fetchStates['mailDomainFallback'] = {state: 'error', data: null, error: 'MailDomain fetch failed'};
    }

    // 4. SSL — refresh via fresh GET /api/domains on force reload
    await tryFetch('ssl', async function() {
      var domRes = await api('/api/domains');
      if (domRes && domRes.data) {
        var fresh = domRes.data.find(function(d) { return d.domain === domain || d.id === (domainRow && domainRow.id); });
        if (fresh) {
          ctx.sslStatus = fresh.ssl_status;
          // Update _domainDetailData if viewing this domain
          if (window._domainDetailData && window._domainDetailData.domainRow) {
            window._domainDetailData.domainRow.ssl_status = fresh.ssl_status;
          }
          return fresh.ssl_status;
        }
      }
      // Fallback to original
      ctx.sslStatus = domainRow ? domainRow.ssl_status : null;
      return ctx.sslStatus;
    });

    // 5. Runtime — site_id > 0 only
    if (domainRow && domainRow.site_id > 0) {
      ctx.runtimeStatus = await tryFetch('runtime', async function() {
        var rtRes = await api('/api/runtime/' + domainRow.site_id);
        var status = (rtRes && rtRes.data) ? rtRes.data.web : null;
        ctx.runtimeLoaded = true;
        return status;
      });
      if (!ctx.runtimeStatus) ctx.runtimeLoaded = false;
    }

    // Compute score
    var result = window.computeDomainHealthScore(ctx);
    return result;
  }
};
