# ARCH-005: SSL/HTTPS Management

Status: Draft

## Overview

This proposal covers the complete SSL/HTTPS subsystem for ContainerCP,
including certificate issuance (Let's Encrypt + custom), automatic
renewal, proxy integration, and HTTPS enforcement.

Due to its length, the proposal is split into parts:

| Part | Topic | File |
|------|-------|------|
| A | Problem, Motivation, Current Architecture, Certificate States | `ARCH-005-partA-problem-and-states.md` |
| B | Proposed Architecture | `ARCH-005-partB-architecture.md` |
| C | Resources, Managers, Storage | `ARCH-005-partC-resources.md` |
| D | Providers | `ARCH-005-partD-providers.md` |
| E | REST API, Web UI, CLI | `ARCH-005-partE-api-ui.md` |
| F | Proxy Integration, Implementation, Migration, Risk | `ARCH-005-partF-integration.md` |

## Related documents

- `planning/proposals/README.md` — proposal template and instructions
- `planning/product-roadmap.md` — version milestones
- `docs/ADR/ADR-003-LetsEncrypt.md` — initial SSL decision
- `docs/runtime-architecture.md` — runtime subsystem (separate from SSL)
- `docs/development/single-source-of-truth.md` — SSL status owned by CertificateStore
