# Phase documents, tracking, and handoff

## When to apply

Use this reference when the user asks to:

- start a phase
- advance a phase
- converge Specs before implementation
- prepare a handoff
- update `next_plan.md`

## Required `phase{N}.md` structure

```text
# Phase N - Phase title

## Chapter 1: Spec pre-plan (frozen)
### 1) Spec item title
- Decision / recommendation:
- Why:
- Details:

## Chapter 2: Phase N plan
Steps:
1. ...
Acceptance items:
1. ...

## Chapter 3: Execution details
### Step execution log
### Problems and fixes

## Chapter 4: Acceptance details
### Acceptance checklist
### Artifact list
### Change log
```

## Execution order

1. At phase start, write Chapter 1 and Chapter 2 first.
2. During execution, keep Chapter 3 current.
3. At phase end, complete Chapter 4.

## Spec convergence rule

- List all key Specs that must be frozen for the phase.
- Discuss and record them one by one in Chapter 1.
- Start implementation only after the key Specs are converged.

## Logging and handoff

- Work logs should include a phase summary, todo status, and execution log.
- Update `next_plan.md` for handoff with current status, phase goal, task list, acceptance criteria, open questions, and reference files.
- The next agent should be able to continue after reading `next_plan.md`.

## Single source of truth

- Keep one phase document as the single source of truth for one workstream.
- Do not maintain parallel progress files, completion notes, or snapshot logs unless the user explicitly wants a separate artifact.
- Temporary notes must be folded back into the matching `phase{N}.md`.

## Mapping to the current Codex environment

- For multi-step execution tracking, use the plan tool available in the active harness; in this Codex session, use `update_plan`.
