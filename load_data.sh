#!/usr/bin/env bash
set -euo pipefail

# Prepares real data for benchmark --file mode from ClickBench hits dataset
# https://github.com/ClickHouse/ClickBench:
# 1. downloads 100 partitioned parquet files (~15 GB) into ClickHouse user_files;
# 2. loads them into single part MergeTree hits table (single part is required:
#    _part_offset is used as row position in table order);
# 3. exports rank encoded columns into data/<column>.bin. Rank (dense_rank over column order)
#    is an order preserving UInt64 code: merge of rank codes performs exactly the same
#    comparisons as merge of original values.
#
# Requires running ClickHouse server and clickhouse client, override with CLICKHOUSE_CLIENT.

CLICKHOUSE_CLIENT="${CLICKHOUSE_CLIENT:-clickhouse client}"

COLUMNS="CounterID AdvEngineID TraficSourceID RegionID SearchPhrase URL UserID URLHash EventTime WatchID"

mkdir -p data

echo "Downloading hits parquet files"
USER_FILES_PATH=$($CLICKHOUSE_CLIENT --query "SELECT value FROM system.server_settings WHERE name = 'user_files_path'")
(cd "$USER_FILES_PATH" && seq 0 99 | xargs -P100 -I{} bash -c \
    "wget --continue --progress=dot:giga 'https://datasets.clickhouse.com/hits_compatible/athena_partitioned/hits_{}.parquet'")

echo "Creating hits table"
wget --continue --quiet https://raw.githubusercontent.com/ClickHouse/ClickBench/main/clickhouse/create.sql
$CLICKHOUSE_CLIENT --query "DROP TABLE IF EXISTS hits"
$CLICKHOUSE_CLIENT --queries-file create.sql

echo "Loading hits table"
$CLICKHOUSE_CLIENT --query "INSERT INTO hits SELECT * FROM file('hits_*.parquet')"

echo "Merging hits table into single part"
$CLICKHOUSE_CLIENT --query "OPTIMIZE TABLE hits FINAL"

ACTIVE_PARTS=$($CLICKHOUSE_CLIENT --query "SELECT count() FROM system.parts WHERE table = 'hits' AND active")
if [ "$ACTIVE_PARTS" != "1" ]; then
    echo "Expected single active part in hits table, got $ACTIVE_PARTS" >&2
    exit 1
fi

for column in $COLUMNS; do
    echo "Exporting $column"
    $CLICKHOUSE_CLIENT --query "
SELECT toUInt64(code) AS value
FROM
(
    SELECT _part_offset AS position, dense_rank() OVER (ORDER BY $column ASC) AS code
    FROM hits
)
ORDER BY position ASC
INTO OUTFILE 'data/$column.bin' TRUNCATE
FORMAT RowBinaryWithNamesAndTypes
SETTINGS max_bytes_before_external_sort = 20000000000"
done

echo "Done, columns are in data directory"
