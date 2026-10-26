#pragma once

#include "pgddb/pg/declarations.hpp"

namespace pgddb {

// Returns the DuckDB catalog name for the consumer's table AM, or nullptr if `am` is not consumer-managed.
typedef const char *(*table_am_get_name_hook_t)(const TableAmRoutine *am);
extern table_am_get_name_hook_t table_am_get_name_hook;

// Set by a consumer whose DuckDB scans of its tables call into Postgres from worker threads.
extern bool table_am_scan_uses_postgres;

const char *TableAmGetName(const TableAmRoutine *am);
const char *TableAmGetName(Oid relid);

} // namespace pgddb
