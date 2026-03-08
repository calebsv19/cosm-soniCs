# mem_console Demo DB

This directory holds the stable showcase database for `mem_console`.

It intentionally lives outside `build/` so `make clean` does not wipe the demo data.

Default showcase DB:
- `shared/showcase/mem_console/demo/demo_mem_console.sqlite`

The `mem_console` binary now uses this file by default when no `--db` argument is provided.

Large-list validation helpers:
- `shared/showcase/mem_console/demo/seed_large_list.sh`
- `shared/showcase/mem_console/demo/LARGE_LIST_AUDIT.md`

Reset helper:
- `shared/showcase/mem_console/demo/reset_demo_db.sh`

Example:
- `./shared/showcase/mem_console/demo/reset_demo_db.sh`
