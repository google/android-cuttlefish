# Workspace Guidelines

## Git Workflow
- Never push branches to `origin` (`google/android-cuttlefish`). Always push PR branches to the user's personal fork remote (for example, `git push -u <fork-remote> <branch>`) and open pull requests against `google/android-cuttlefish` from the fork (for example, `gh pr create --repo google/android-cuttlefish --head <fork-owner>:<branch>`).
- Please add an `Assisted-by:` tag to git commits you create with the format: `Assisted-by: AGENTNAME:MODELVERSION`.
