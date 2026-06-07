---
name: Pull request
about: Submit a change to DriftWood
title: "[PR] "
labels: ''
assignees: ''
---

## Summary

One or two sentences on what this PR does and why.

## Linked issues

Closes #<issue>, fixes #<bug>, or relates to #<discussion>.

## Type of change

- [ ] Bug fix (non-breaking change that fixes an issue)
- [ ] New feature (non-breaking change that adds functionality)
- [ ] Breaking change (existing behavior changes — call it out in the description)
- [ ] Documentation only
- [ ] Refactor (no behavior change)

## How was this tested?

- [ ] Existing tests pass (`ctest --output-on-failure`)
- [ ] New tests added (link or describe)
- [ ] Manual perft / search smoke test
- [ ] Web UI smoke test (if applicable)

For eval/search changes, include before/after on at least one tactical position
and one quiet endgame.

## Checklist

- [ ] No new compiler warnings (`-Wall -Wextra -Wpedantic`)
- [ ] Public API changes are documented in `docs/DESIGN.md` or the relevant header
- [ ] Commit history is clean (squash fixups, rebase onto master)
- [ ] Commit messages follow the project convention (see `CONTRIBUTING.md`)
