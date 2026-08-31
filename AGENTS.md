# Repository Notes

## PlatformIO

Before editing, fetch remotes and fast-forward the newest intended working
branch to its upstream. Stop and report dirty, divergent, or conflicted state;
never overwrite work to force a sync.

On Windows, use `.\scripts\pio.cmd <arguments>`; it selects the current user's
VS Code-managed installation. Never install another PlatformIO Core; if the
wrapper cannot find it, stop and report the missing installation.

- Required validation commands are listed in [CONTRIBUTING.md](CONTRIBUTING.md#validation).
