# ensure_central_proxy() — Architecture Analysis

## 1. Current decision tree (as of latest deploy)

```
START
  │
  ├── Does container exist? ─── No ──→ CREATE (docker run)
  │                                  │
  │                                  └── Wait up to 15s for running state
  │
  └── Yes
      │
      ├── Is container Running? ─── Yes ──→ VALIDATE
      │   │                                  │
      │   │                           Network mode == host? → recreate
      │   │                           SSL mount missing?     → recreate
      │   │                           Port 80 mapping gone?  → recreate
      │   │                           All OK → return success
      │   │
      │   └── needs_recreate? ─── Yes ──→ rm -f → CREATE
      │
      └── Not running (Exited)
          │
          └── docker start
              │
              ├── start OK → wait for running → return success
              └── start fails → rm -f → CREATE
```

## 2. Why recovery fails today

The daemon running on the server still has the version from commit
`bb45bac` (before the Exited-handling fix).  In that version,
`ensure_central_proxy()` only has:

```
container running → validate
(anything else)   → docker run --name containercp-proxy ...
```

`docker run --name` **fails when the container already exists**
(exit code != 0), even if the container is stopped.  Docker returns
`Conflict. The container name "/containercp-proxy" is already in use`.

Hence: `"Failed to create central proxy container"`.

## 3. What defines the canonical state?

**Question:** Should recovery restore the running container, or the
correctly configured running container?

**Answer:** The canonical state is:

> A running containercp-proxy container with:
>   - bridge network mode (not host)
>   - correct SSL volume mount
>   - ports 80/443 mapped
>   - `--add-host host.docker.internal:host-gateway`
>   - nginx configuration matching current ContainerCP state

Simply calling `docker start` restores a running container, but it
does NOT verify that the container's configuration matches the
current ContainerCP expectations.  If the ContainerCP configuration
changed since the container was created (e.g. SSL root moved,
network changed), starting the old container would restore an
**obsolete** configuration.

## 4. Options for Exited containers

### Option A: docker start (no validation)

```
Exited container → docker start → return success
```

**Advantages:**
- Fastest recovery (milliseconds)
- Preserves existing container (logs, metadata)
- No port conflict risk (ports already mapped)

**Disadvantages:**
- Container may have stale mounts, wrong network, or missing SSL
- If ContainerCP configuration changed since creation, old container
  is wrong

### Option B: Remove and recreate

```
Exited container → docker rm -f → docker run (new) → return success
```

**Advantages:**
- Always gets the current configuration
- No stale config risk

**Disadvantages:**
- Slower (docker run + image pull)
- Destructive (loses container metadata)
- Port 80/443 briefly released during rm→run (race window)

### Option C: Validate first, then start or recreate

```
Exited container → VALIDATE (same checks as running containers)
  ├── Config correct → docker start → return success
  └── Config wrong   → docker rm -f → docker run → return success
```

**Advantages:**
- Correct by default (validates before starting)
- Only recreates when needed
- Same validation logic already used for running containers
- No configuration drift

**Disadvantages:**
- Slightly more code (but reuses existing validation)
- Validation takes ~100ms (three docker inspect calls)

## 5. Recommendation

**Option C** is the architecturally correct approach.

Recovery must restore the canonical state, meaning:
- A running container, AND
- With the correct configuration

The validation already exists for running containers (network mode,
SSL mount, port mapping).  Applying the same checks to stopped
containers before deciding to start vs recreate is the right
architecture.

The decision tree becomes:

```
Container exists
  ├── Running?
  │   ├── Yes → VALIDATE → OK? → return success
  │   │                    └── Wrong? → rm -f → CREATE
  │   └── No (Exited/stopped)
  │       └── VALIDATE (same checks)
  │           ├── Config correct → docker start → verify → return
  │           └── Config wrong   → rm -f → CREATE
  └── Doesn't exist → CREATE
```

This ensures that:
- A stopped container with correct config starts quickly (no recreation)
- A stopped container with stale config gets recreated (no drift)
- The same validation logic is used for both running and stopped cases
- Recovery always converges to the canonical state

## 6. Implementation impact

The fix is already written in commit `b10cc2f` and just needs to be
deployed.  The only architectural change to review is whether to add
validation before `docker start` (Option C) vs the current simple
start (Option A) in that commit.

Recommendation: add validation before start to match Option C.
The validation code is already written (lines 304-341 for running
containers) — it just needs to be factored out and reused for the
stopped-container path.

## Related documents

- `docs/recovery-architecture-vs-bootstrap.md` — Recovery design
- `docs/admin-recovery-architecture.md` — Admin panel recovery
- `docs/startup-architecture-review.md` — Startup audit
