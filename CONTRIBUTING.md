# Contributing

The workspace [preferences.md](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a)
is the governing architecture contract (locally `../../preferences.md`). Cite laws
by Title. Keep tests in the shared workspace `../../tests/b`, document platform gaps, and never claim a
planned command works merely because its parser accepts it.

Build C23 with `-Wall -Wextra -Werror`; run `python3 ../../tests/b/cli_test.py` before
committing. Keep changes cohesive and commit in this repository. Push only on
an explicit, one-off instruction under the Git Workflow Law.
