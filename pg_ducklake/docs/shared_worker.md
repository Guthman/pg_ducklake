# Shared DuckDB Worker

By default every PostgreSQL backend embeds its own DuckDB instance, so each
connection pays for its own DuckDB threads, memory and caches. With
`ducklake.use_shared_worker = on`, eligible read-only queries are instead
dispatched to one background worker per database (`pg_ducklake duckdb worker`
in `pg_stat_activity`) that hosts a single DuckDB engine shared by all
backends. The worker starts on the first dispatch and keeps running. Dispatch
needs `pg_ducklake` in `shared_preload_libraries` (it registers shared memory
at startup).

The worker never reads PostgreSQL tables itself. The DuckLake metadata SQL a
query needs is sent back to the dispatching backend, which runs it inside its
own transaction and snapshot exactly as it would in-process, so permissions and
visibility are unchanged.

```sql
SET ducklake.use_shared_worker = on;
SELECT count(*) FROM my_ducklake_table;   -- runs in the worker
```

## What is dispatched

A query runs in the worker only when all of the following hold:

- it is a plain `SELECT` (no `INSERT`/`UPDATE`/`DELETE`, DDL, `COPY`, or
  data-modifying CTE);
- it references no PostgreSQL relation: heap tables, temporary tables, views
  over them and DuckLake foreign tables keep the whole query in-process;
- it calls no ducklake-only function (`ducklake.query()`, snapshot or
  maintenance functions, ...);
- it is not `EXPLAIN`ed; `EXPLAIN` output always comes from the backend's own
  engine;
- it runs outside a transaction block (including multi-statement pipelines) and
  the backend's own DuckDB instance has no open transaction. A dispatched
  statement sees the latest DuckLake snapshot; a transaction block pins one
  in-process.

Everything else behaves exactly as with `ducklake.use_shared_worker = off`.

## Settings

| GUC | Purpose |
| :-- | :------ |
| [`ducklake.use_shared_worker`](settings.md#ducklakeuse_shared_worker) | Enable dispatch for this session |
| [`ducklake.max_worker_sessions`](settings.md#ducklakemax_worker_sessions) | Concurrent dispatched queries; further dispatches wait |
| [`ducklake.worker_threads`](settings.md#ducklakeworker_threads) | DuckDB threads of the worker's engine |
| [`ducklake.worker_memory_limit`](settings.md#ducklakeworker_memory_limit) | Memory limit of the worker's engine |
| [`ducklake.worker_temp_directory`](settings.md#ducklakeworker_temp_directory) | Spill directory for the worker's engine |

The engine settings are read when the worker starts; after changing them,
terminate the worker (`pg_terminate_backend()` on its pid) and the next
dispatch starts a fresh one.

## Observing dispatch

`ducklake.worker_stats()` (superuser role only) reports the worker's pid, the
number of dispatched queries it is running right now and the number it has run
since it started. All columns are NULL while no worker is registered for the
database:

```sql
SELECT * FROM ducklake.worker_stats();
 worker_pid | active_sessions | total_sessions
------------+-----------------+----------------
      48213 |               1 |             37
```

A query that did not change `total_sessions` ran in-process.

## Limitations

- Only read-only `SELECT` statements outside transaction blocks are dispatched
  (see above).
- The worker's S3/Azure secrets come from the user mappings visible to the
  bootstrap superuser, i.e. `PUBLIC` mappings on the `ducklake_secret` servers.
  Per-user mappings are not honored for dispatched queries.
- The worker keeps a connection to its database, so `DROP DATABASE` needs
  `WITH (FORCE)` or terminating the worker first.
- `EXPLAIN` always runs in-process, so it shows the backend's own plan, not the
  worker's.
