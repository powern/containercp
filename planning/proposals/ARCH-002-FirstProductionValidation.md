# ARCH-002: First Production Validation

Status: Implemented

## Problem

ContainerCP has never been deployed on a clean server. All development
has occurred in the same environment where the code was written. There
may be hidden dependencies, missing documentation, or broken workflows
that only appear on a fresh installation.

## Motivation

The Product Vision defines v1.0 success as deploying on a clean
Debian 13 (Trixie) server within 30 minutes. Without validating this
workflow, the product cannot be considered production-ready.

Which Product Vision goal it supports: Version 1.0 readiness.

## Strategy

The validation follows a four-phase approach:

1. **Preparation** — Architecture proposal, INSTALL.md, validation checklist
2. **Clean deployment** — Install on fresh Debian 13 (Trixie), document every step
3. **Validation execution** — Run all 137 checklist items, record results
4. **Bug fixing** — Fix discovered issues, iterate until all pass

## Current Architecture

The project has a daemon binary, a CLI client, a REST API server,
and a Web UI. It depends on Docker, CMake, Ninja, and g++.
Storage uses pipe-delimited files in `/srv/containercp/database/`.

## Proposed Validation Environment

- Debian 13 (Trixie) minimal installation
- No development tools pre-installed (except those needed to build)
- Docker CE from official Docker repository
- Standard Debian packages: git, cmake, ninja-build, g++, curl

## Risks

- Docker may not be installed or configured correctly
- Build may fail on a fresh system due to missing dependencies
- Daemon may fail to start due to missing directories
- API endpoints may return errors in edge cases
- Web UI may have JavaScript errors in different browsers

## Validation Plan

The full 137-item checklist in `planning/product-validation.md` will
be executed. Each item will be marked PASS, FAIL, or NOT TESTED.
Failed items will be documented as bugs and fixed before the next
Release Candidate iteration.

## Bug document template

```
## BUG-XXX: Title

### Description

### Reproduction steps

### Expected behavior

### Actual behavior

### Severity (critical/major/minor)

### Status (open/fixed)
```
