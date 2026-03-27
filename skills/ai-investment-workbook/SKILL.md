---
name: ai-investment-workbook
description: Apply the D:\AI_Investment / FinGuard workbook operating rules for coding, debugging, documentation, automation, phase planning, handoff, and workbook maintenance. Use when working in this repository, or when the user mentions workbook, FinGuard, finguard, phase*.md, next_plan.md, Shouzhuo, asset allocation, or the project's established operating style.
---

# AI Investment Workbook

Treat this skill as the default operating handbook for the `D:\AI_Investment` repository. The goal is not to repeat the full workbook, but to turn its stable and reusable rules into Codex-ready operating guidance.

## Default stance

- Work like a plain, reliable, traceable programmer agent.
- Prefer small, incremental, reversible, and verifiable changes.
- Prefer adding focused code over rewriting large existing areas.
- Read and edit files only when needed; avoid speculative broad changes.
- Keep automation traceable with commands, outcomes, and log locations.
- Mask secrets and request confirmation before risky or high-privilege actions.

## Reference selection

For any task in this repository, read `references/behavior-and-communication.md` first. Then load only the references needed for the current task:

- Docs, tutorials, explanations, Markdown rewrites: `references/docs-and-teaching.md`
- Phase planning, Spec convergence, handoff, `next_plan.md`: `references/phase-tracking.md`
- Starting, validating, or troubleshooting the finguard backend: `references/finguard-automation.md`
- Engineering implementation, frontend/backend integration, Tauri, external APIs, modular design: `references/engineering-rules.md`

Do not load every reference by default.

## Execution flow

1. Classify the task as implementation, debugging, documentation, teaching, automation, phase work, or workbook maintenance.
2. Read the general behavior rules first, then only the task-specific references.
3. If the user starts or advances a phase, create or update `phase{N}.md` before implementation.
4. If the task needs handoff, update `next_plan.md` so the next agent can continue immediately.
5. If the task is educational, create a standalone `.md` tutorial and follow the teaching structure.
6. If the task touches finguard runtime, acceptance, CORS, installers, or external APIs, follow the relevant checklist before concluding.
7. If the user asks to turn a solved issue into a workbook rule, follow the flow: classify, deduplicate, write, append changelog, report back.

## Hard constraints

- Keep one phase document as the single source of truth for one stream of work.
- Use delete-then-create for large document rewrites so old and new versions do not coexist.
- Default output should target real product work and real users, not interview-style writing.
- In Spec discussions, explain background and impact first, then compare options, then ask one key confirmation question.

## Mapping to the current Codex toolchain

- For multi-step tracking, use the planning tool available in the current harness; in this Codex environment, use `update_plan`.
- For rules that should survive beyond one chat, write them into this skill or into the workbook instead of leaving them only in conversation history.
