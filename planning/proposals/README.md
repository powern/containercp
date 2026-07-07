# Architecture Proposals

## Purpose

Architecture Proposals exist to prevent unnecessary refactoring.

Before any Epic is implemented, the architectural decisions must be
validated in writing. This reduces wasted implementation effort and
ensures every feature aligns with the Product Vision.

## Lifecycle

```
Draft
  ↓
Review
  ↓
Approved
  ↓
Implementation
  ↓
Review / Retrospective
  ↓
Merged into Architecture
```

### Draft

The author creates the proposal. All sections must be filled.
"Draft" status means the proposal is incomplete or unreviewed.

### Review

At least one architect or senior contributor reviews the proposal.
Reviews focus on:

- Alignment with Product Vision
- Architectural consistency
- Backward compatibility
- Risk assessment
- Validation plan

### Approved

The proposal is accepted. Implementation may begin.
Approved proposals become the source of truth for the Epic.

### Implementation

The Epic follows the approved proposal. Deviations must be documented
in a new revision of the proposal.

### Review / Retrospective

After implementation, the proposal is updated with lessons learned.
This becomes the permanent architecture record.

### Merged into Architecture

The final proposal is archived as a reference document.
It may be referenced by future proposals.

## Required sections

Every proposal must contain:

### Problem

What limitation currently exists? What is the pain point?

### Motivation

Why is this feature important? Which Product Vision goal does it
support? Which customer problem does it solve?

### Current Architecture

Describe the current implementation (or absence) of the subsystem.

### Proposed Architecture

Describe the new architecture in sufficient detail for implementation.

### New Resources

List all new resources (structs inheriting from `core::Resource`).

### Managers

List all new managers and their methods.

### Storage

Describe the storage format (file name, pipe-delimited fields).

### Providers

Describe any new Provider interfaces and their initial implementations.

### REST API

List all new or modified REST endpoints.

### Web UI

List all new or modified Web UI pages.

### CLI

List all new or modified CLI commands.

### Configuration

Describe any new configuration values (paths, ports, defaults).

### Migration Strategy

Will existing users be affected? Is a migration script needed?

### Backward Compatibility

Will existing data, CLI commands, or API endpoints be broken?

### Rejected Alternatives

Describe other approaches and why they were rejected.

### Risks

Technical risks, dependencies, unknowns.

### Validation Plan

How will the feature be validated? What tests are needed?

## Relationship with ADR documents

ADRs (Architecture Decision Records) document decisions that affect
the entire project architecture. Proposals document the architecture
of a specific feature.

- A proposal may be promoted to an ADR if it affects the project's
  foundational architecture.
- Proposals are more detailed than ADRs. They describe the full
  implementation plan, not just the decision.
- ADRs live in `docs/ADR/`. Proposals live in `planning/proposals/`.

## Approval requirements

A proposal is approved when:

1. All sections are complete
2. The architecture is consistent with Product Vision principles
3. Backward compatibility is addressed
4. Risks are identified and accepted
5. The validation plan is credible

Approval is indicated by moving the proposal file to `Approved` status
in its header, or by adding a review comment at the top of the file.

## Proposal naming convention

```
ARCH-XXX-ShortName.md
```

Examples:

- `ARCH-001-DNS.md`
- `ARCH-002-Mail.md`
- `ARCH-003-Monitoring.md`
- `ARCH-004-MultiNode.md`

The number is sequential. ShortName uses PascalCase.

## Naming

```
Status: Draft | Review | Approved | Implemented | Archived
```
