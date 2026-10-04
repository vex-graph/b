# Contributing

The workspace [preferences.md](https://github.com/vex-graph/worktree/blob/main/preferences.md)
is the governing architecture contract (locally `../preferences.md`). Cite laws
by Title. Keep tests in the shared workspace `tests/b/`, document platform gaps, and never claim a
planned command works merely because its parser accepts it.

Build C23 with `-Wall -Wextra -Werror`; run `python3 ../tests/b/cli_test.py` before
committing. Keep changes cohesive and commit in this repository. Push only on
an explicit, one-off instruction under the Git Workflow Law.
