# Documentation, teaching, and editing rules

## Core document requirements

- Write for real operators and users: short sentences, limited jargon, plain explanations.
- Save long-lived repository Markdown as UTF-8 and stay consistent with the file's existing encoding style.
- Commands and code blocks should be directly reproducible; state the working directory and environment when needed.

## Tutorial structure

When the user asks for teaching, explanation, a learning path, or a tutorial, create a standalone `.md` document that includes at least:

1. Title and goal
2. Learning path or staged progression
3. Core concept explanation with a real-world analogy
4. Key workflow or steps
5. Real code snippets with line-by-line commentary
6. Summary plus self-check questions with answers
7. Next-step suggestions

For teaching Q&A, follow the same rhythm: goal, plain explanation, example or analogy, summary, self-check.

## Rewrite and refactor rules

- For large rewrites, use delete-then-create instead of stacking a new version on top of the old one.
- After a rewrite, verify there is only one top-level title and no duplicated document body.
- Once temporary summaries or acceptance drafts are merged into the main document, delete or archive them so they do not become competing truth sources.

## Workbook metadata and changelogs

When maintaining `workbook/*.md`, preserve the YAML metadata at the top and append a changelog entry at the end:

```text
[YYYY-MM-DD] (auto-added | manual-update | needs-review) source summary
```

## Review threshold

- Documents containing system commands, permission changes, or network calls should be treated as high-risk content and should only be considered final after explicit user confirmation.
