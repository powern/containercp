# BUG-009: Failed site create leaves site record in storage

## Severity
Critical

## Description
When `site create` fails, the rollback does not properly remove the
site record from the site manager.

## First fix (insufficient)
First attempt added `s.save()` after rollback in daemon and API handlers
(commit 8e9a9c6). This was necessary but not sufficient because the
rollback itself was removing the wrong records.

## Actual root cause
In `SiteCreateOperation::execute()`:

```cpp
sites_.create(domain, owner, node.id);  // Returns ID 1, but return value discarded
```

The return value of `sites_.create()` was discarded. The `site.id` field
remained at its default value of 0. When rollback ran:

```cpp
for (const auto& d : databases_.list()) {
    if (d.site_id == site.id) {  // site.id is 0, never matches
```

No databases matched site.id==0. Same issue for domains.
And `sites_.remove(site.id)` removed ID 0 instead of ID 1.

The site record with ID 1 remained in the manager.

## Fix
Changed line to capture the returned ID:
```cpp
site.id = sites_.create(domain, owner, node.id);
```

Now rollback correctly removes the site with ID 1, databases with
site_id==1, and domains with site_id==1.

## Files changed
- libs/operations/SiteCreateOperation.cpp: capture site.id from create()
- tests/test_managers.cpp: verify created site has correct ID
- libs/daemon/DaemonApp.cpp: s.save() after rollback (first fix)
- libs/api/ApiServer.cpp: s.save() after rollback (first fix)

## Regression test
- SiteManager test now checks that created site has ID 1
- Verify that removing by correct ID works

## Fix commit
First fix: 8e9a9c6 (save after rollback)
Second fix: this commit (capture site.id from create)

## Status
Resolved
