# `.agent/` — Agent deep-dives

Supporting material for AI agents (Claude Code, Codex, etc.) working on Lightweight. The primary entry point is `../AGENT.md`; this directory keeps the longer deep-dives out of the top-level guide so it stays scannable.

| File | Topic |
|------|-------|
| [`architecture.md`](architecture.md) | Component map deep-dive, canonical examples for formatter dispatch, `SqlDataBinder<T>` specialization, `DataMapper` field/relationship mechanics, connection pool, migration model |
| [`databases.md`](databases.md) | Per-DBMS specifics: features, connection-string formats, Docker harness, known footguns (Unicode round-trips, `NULL` semantics, identifier quoting) |
| [`testing.md`](testing.md) | Catch2 conventions, `.test-env.yml` schema, `--test-env=<name>` flag, the `dbtool` Python integration suite, sanitizer/valgrind presets |
| [`cpp-guidelines.md`](cpp-guidelines.md) | Self-contained C++23 ruleset (general + Lightweight-specific). No external `cpp.md` required |

These files are loaded into agent context via the top-level `CLAUDE.md` (`@.agent/`).
