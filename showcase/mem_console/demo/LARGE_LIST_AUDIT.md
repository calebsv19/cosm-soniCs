# mem_console Large List Scroll Audit

Use this checklist to verify Phase 7 list scaling behavior.

## 1) Seed a large set

```sh
make -C shared/core/core_memdb all
./shared/showcase/mem_console/demo/seed_large_list.sh ./shared/showcase/mem_console/demo/demo_mem_console.sqlite 220
```

## 2) Launch mem_console

```sh
make -C shared/showcase/mem_console all
./shared/showcase/mem_console/build/mem_console --db ./shared/showcase/mem_console/demo/demo_mem_console.sqlite
```

## 3) Manual validation checklist

- Scroll in the left list until the final memory entry is visible.
- Continue scrolling so that final entry can become the top visible row.
- Click several rows while scrolling and confirm detail pane tracks selection.
- Type a filter and confirm scrolling still works across filtered results.
- Clear filter and confirm list returns to full dataset without dead-end rows.

## Expected outcome

- The list is not limited to a small fixed first page.
- Scrolling reaches the full match set.
- Selection remains stable as windows refresh.
