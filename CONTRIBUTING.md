# Contributing

Run the repository setup once after cloning:

```bash
./scripts/setup.sh
```

This configures the local Git hooks used by the repository.

## Commit Convention

Commit messages must follow this format:

```text
type: summary
type(scope): summary
```

Allowed types:

- `feat`
- `fix`
- `docs`
- `refactor`
- `test`
- `chore`
- `build`
- `ci`
- `perf`
- `revert`

Examples:

```text
feat(mqtt): add reconnect backoff
fix(sensor): guard invalid PZEM readings
docs: update node responsibilities
```

## Pull Requests

Use the pull request template when opening a PR. It asks for:

- the problem being solved
- the scope of the change
- test coverage, including hardware and MQTT validation when relevant
- regression and rollout risks

Keep PRs focused. Firmware changes that mix connectivity, sensor logic, buffering, and documentation are harder to review and validate safely.

## Issues

Use the GitHub issue templates when reporting bugs or proposing features.

- Bug reports capture node type, hardware revision, firmware version or commit, broker context, reproduction steps, and logs.
- Feature requests capture the problem, proposed change, affected subsystem, and acceptance criteria.
