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
window.HealthCache = {
  _store: {},
  _loaders: {},
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

    // Dedup concurrent loaders
    if (this._loaders[domain]) return this._loaders[domain];

    // Mark loading
    this._store[domain] = {result: null, timestamp: Date.now(), loading: true};

    // Async loader
    this._loaders[domain] = this._doLoad(domain, domainRow, mailDomain, serverHostname)
      .then(function(result) {
        // Store result
        window.HealthCache._store[domain] = {result: result, timestamp: Date.now(), loading: false};
        delete window.HealthCache._loaders[domain];
        return result;
      })
      .catch(function(err) {
        console.error('HealthCache.load failed for ' + domain, err);
        delete window.HealthCache._store[domain];
        delete window.HealthCache._loaders[domain];
        return null;
      });

    return this._loaders[domain];
  },

  // Internal: fetch all data, compute score, return result
  _doLoad: async function(domain, domainRow, mailDomain, serverHostname) {
    if (!domain) return null;

    var ctx = {
      domainRow: domainRow || {},
      mailDomain: mailDomain || null,
      serverHostname: serverHostname || '',
    };

    // Fetch root DNS (A, AAAA, MX, TXT, NS, CAA)
    try {
      ctx.rootDns = await fetchDnsForFqdn(domain, 'A,AAAA,MX,TXT,NS,CAA');
    } catch(e) { console.error('Health root DNS failed', e); }

    // Fetch mail DNS
    var mail = mailDomain;
    if (!mail && domainRow && domainRow.mail_domain_id && domainRow.mail_domain_id > 0) {
      try {
        var mdRes = await api('/api/mail/domains');
        if (mdRes && mdRes.data) {
          mail = mdRes.data.find(function(m) { return m.domain === domain || m.domain_id === domainRow.id; }) || null;
        }
      } catch(e) {}
    }

    if (mail && mail.mode && mail.mode !== 'disabled') {
      ctx.mailDomain = mail;
      if (mail.dkim_public_key_dns) {
        var sel = mail.dkim_selector || 'dkim';
        try { ctx.dkimDns = await fetchDnsForFqdn(sel + '._domainkey.' + domain, 'TXT'); }
        catch(e) { console.error('DKIM fetch failed', e); }
      }
      try { ctx.dmarcDns = await fetchDnsForFqdn('_dmarc.' + domain, 'TXT'); }
      catch(e) { console.error('DMARC fetch failed', e); }
      if (mail.mode === 'local-primary') {
        try { ctx.mtaStsDns = await fetchDnsForFqdn('_mta-sts.' + domain, 'TXT'); }
        catch(e) { console.error('MTA-STS fetch failed', e); }
        if (serverHostname) {
          try { ctx.autoDns = await fetchDnsForFqdn('autodiscover.' + domain, 'CNAME,A'); }
          catch(e) { console.error('Autodiscover fetch failed', e); }
        }
      }
    } else if (domainRow && domainRow.mail_domain_id && domainRow.mail_domain_id > 0) {
      // mail exists but fetch failed — use basic data from domainRow
      ctx.mailDomain = {domain: domain, mode: domainRow.mail_domain_mode || 'local-primary', dkim_public_key_dns: domainRow.dkim_public_key_dns || ''};
    }

    // Mark DNS as attempted regardless of success
    ctx.allDnsLoaded = true;
    ctx.allMailDnsLoaded = true;

    // SSL
    ctx.sslStatus = domainRow ? domainRow.ssl_status : null;

    // Runtime
    if (domainRow && domainRow.site_id > 0) {
      try {
        var rtRes = await api('/api/runtime/' + domainRow.site_id);
        if (rtRes && rtRes.data) ctx.runtimeStatus = rtRes.data.web;
      } catch(e) { console.error('Runtime fetch failed for site ' + domainRow.site_id, e); }
      ctx.runtimeLoaded = true;
    }

    // Compute score
    var result = window.computeDomainHealthScore(ctx);
    return result;
  }
};
