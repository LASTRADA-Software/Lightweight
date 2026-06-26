/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "Lightweight", "index.html", [
    [ "Lightweight, an ODBC SQL API for C++23", "index.html", "index" ],
    [ "How to", "d1/dde/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2how-to.html", [
      [ "Rename column name", "d1/dde/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2how-to.html#rename-column-name", null ]
    ] ],
    [ "Usage Examples", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html", [
      [ "Configure default connection information to the database", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#configure-default-connection-information-to-the-database", null ],
      [ "Raw SQL Queries", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#raw-sql-queries", null ],
      [ "Transparent block-prefetch (fewer network round-trips)", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#transparent-block-prefetch-fewer-network-round-trips", null ],
      [ "Prepared Statements", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#prepared-statements", null ],
      [ "SQL Query Builder", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#sql-query-builder", null ],
      [ "High level Data Mapping", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#high-level-data-mapping", [
        [ "Batched insert and update", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#batched-insert-and-update", null ]
      ] ],
      [ "Simple row retrieval via structs", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#simple-row-retrieval-via-structs", null ]
    ] ],
    [ "SQL Query", "d9/dbe/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sqlquery.html", [
      [ "Create or Modife database schema", "d9/dbe/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sqlquery.html#create-or-modife-database-schema", [
        [ "Example", "d9/dbe/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sqlquery.html#example", null ]
      ] ],
      [ "Insert elements", "d9/dbe/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sqlquery.html#insert-elements", null ],
      [ "Select elements", "d9/dbe/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sqlquery.html#select-elements", [
        [ "Example", "d9/dbe/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sqlquery.html#example-1", null ],
        [ "Examples of SQL to DataMapper mappings", "d9/dbe/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sqlquery.html#examples-of-sql-to-datamapper-mappings", null ]
      ] ]
    ] ],
    [ "SQL to Lightweight", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html", [
      [ "The example schema", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#the-example-schema", null ],
      [ "SELECT", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#select", [
        [ "Select all rows", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#select-all-rows", null ],
        [ "Select specific columns", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#select-specific-columns", null ],
        [ "WHERE — a single condition", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#where--a-single-condition", null ],
        [ "WHERE — multiple conditions (AND / OR)", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#where--multiple-conditions-and--or", null ],
        [ "WHERE IN", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#where-in", null ],
        [ "WHERE — NULL / NOT NULL", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#where--null--not-null", null ],
        [ "Optional / conditional filters", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#optional--conditional-filters", null ],
        [ "ORDER BY", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#order-by", null ],
        [ "LIMIT / TOP (fetch the first row)", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#limit--top-fetch-the-first-row", null ],
        [ "OFFSET / LIMIT (pagination)", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#offset--limit-pagination", null ],
        [ "DISTINCT", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#distinct", null ],
        [ "COUNT and aggregates", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#count-and-aggregates", null ],
        [ "GROUP BY", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#group-by", null ]
      ] ],
      [ "JOIN", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#join", [
        [ "INNER JOIN", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#inner-join", null ],
        [ "LEFT OUTER JOIN", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#left-outer-join", null ],
        [ "Multi-condition / aliased joins", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#multi-condition--aliased-joins", null ]
      ] ],
      [ "INSERT", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#insert", [
        [ "Bulk insert", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#bulk-insert", null ]
      ] ],
      [ "UPDATE", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#update", null ],
      [ "DELETE", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#delete", null ],
      [ "Relationships", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#relationships", null ],
      [ "CREATE TABLE", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#create-table", null ],
      [ "Transactions", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#transactions", null ],
      [ "Mapping a custom result shape", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#mapping-a-custom-result-shape", null ],
      [ "Keeping these examples honest", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#keeping-these-examples-honest", null ],
      [ "See also", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#see-also", null ]
    ] ],
    [ "Best Practices", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html", [
      [ "Introduction", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#introduction", null ],
      [ "Common Best Practices", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#common-best-practices", [
        [ "Use the DataMapper API", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#use-the-datamapper-api", null ],
        [ "Keep Data Model and Business Logic Separate", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#keep-data-model-and-business-logic-separate", null ],
        [ "Use Transactions with Care", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#use-transactions-with-care", null ],
        [ "Binding Output Parameters", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#binding-output-parameters", null ]
      ] ],
      [ "SQL Driver-Related Best Practices", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#sql-driver-related-best-practices", [
        [ "Query Result Row Columns in Order", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#query-result-row-columns-in-order", null ]
      ] ],
      [ "Performance Is Key", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#performance-is-key", [
        [ "Use Native Column Types", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#use-native-column-types", null ],
        [ "Use Prepared Statements", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#use-prepared-statements", null ],
        [ "Use Pagination or Infinite Scrolling", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#use-pagination-or-infinite-scrolling", null ],
        [ "Let block-prefetch cut network round-trips", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#let-block-prefetch-cut-network-round-trips", null ]
      ] ],
      [ "SQL Server Variation Challenges", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#sql-server-variation-challenges", [
        [ "64-bit Integer Handling in Oracle Database", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#autotoc_md64-bit-integer-handling-in-oracle-database", null ]
      ] ]
    ] ],
    [ "Data Binder API", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html", [
      [ "Custom Column Data Type Binder Example", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#custom-column-data-type-binder-example", null ],
      [ "InputParameter()", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#inputparameter", null ],
      [ "OutputColumn()", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#outputcolumn", null ],
      [ "GetColumn()", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#getcolumn", null ],
      [ "Inspect()", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#inspect", null ],
      [ "How <tt>SqlVariant</tt> decides which alternative to fill", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#how-sqlvariant-decides-which-alternative-to-fill", null ],
      [ "Driver-specific connection-string requirements", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#driver-specific-connection-string-requirements", null ]
    ] ],
    [ "SQL Backup: How It Works", "dd/d39/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup.html", [
      [ "Overview", "dd/d39/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup.html#overview", null ],
      [ "Pipeline phases", "dd/d39/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup.html#pipeline-phases", null ],
      [ "Memory and disk profile", "dd/d39/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup.html#memory-and-disk-profile", null ],
      [ "Fault tolerance", "dd/d39/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup.html#fault-tolerance", null ],
      [ "Consistency caveats (online backup)", "dd/d39/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup.html#consistency-caveats-online-backup", null ],
      [ "Tuning", "dd/d39/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup.html#tuning", null ],
      [ "See also", "dd/d39/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup.html#see-also-1", null ]
    ] ],
    [ "Lightweight SQL Backup File Format", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html", [
      [ "1. Archive Structure", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md1-archive-structure", null ],
      [ "2. Metadata Manifest (<tt>metadata.json</tt>)", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md2-metadata-manifest-metadatajson", [
        [ "2.1 Server Identification", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md21-server-identification", null ],
        [ "2.2 Table Definition", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md22-table-definition", null ],
        [ "2.3 Column Definition", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md23-column-definition", null ],
        [ "2.4 Foreign Key Definition", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md24-foreign-key-definition", null ]
      ] ],
      [ "3. Data Chunk Format (<tt>.msgpack</tt>)", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md3-data-chunk-format-msgpack", [
        [ "3.1 Top-Level Structure", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md31-top-level-structure", null ],
        [ "3.2 Column Object", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md32-column-object", null ],
        [ "3.3 Data Types and Encoding", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md33-data-types-and-encoding", null ],
        [ "3.4 Null Handling (<tt>\"n\"</tt>)", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md34-null-handling-n", null ],
        [ "3.5 Packed Binary Format", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md35-packed-binary-format", null ]
      ] ],
      [ "4. File Extension", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md4-file-extension", null ],
      [ "5. Compression", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md5-compression", [
        [ "5.1 Supported Compression Methods", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md51-supported-compression-methods", null ],
        [ "5.2 Compression Levels", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md52-compression-levels", null ],
        [ "5.3 Runtime Availability", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md53-runtime-availability", null ],
        [ "5.4 Restoration Compatibility", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md54-restoration-compatibility", null ]
      ] ]
    ] ],
    [ "dbtool - Database Management CLI", "d5/dc4/dbtool.html", [
      [ "Overview", "d5/dc4/dbtool.html#overview-1", null ],
      [ "Installation", "d5/dc4/dbtool.html#installation", [
        [ "Pre-built installers", "d5/dc4/dbtool.html#pre-built-installers", null ],
        [ "Building from source", "d5/dc4/dbtool.html#building-from-source", null ],
        [ "Producing the installer locally", "d5/dc4/dbtool.html#producing-the-installer-locally", null ]
      ] ],
      [ "Configuration", "d5/dc4/dbtool.html#configuration", [
        [ "Connection String", "d5/dc4/dbtool.html#connection-string", null ],
        [ "Configuration File Format", "d5/dc4/dbtool.html#configuration-file-format", null ],
        [ "Inspecting configured profiles", "d5/dc4/dbtool.html#inspecting-configured-profiles", null ],
        [ "Database-Specific Connection Strings", "d5/dc4/dbtool.html#database-specific-connection-strings", null ]
      ] ],
      [ "Migration Commands", "d5/dc4/dbtool.html#migration-commands", [
        [ "migrate", "d5/dc4/dbtool.html#migrate", [
          [ "Custom default schema (<tt>--schema</tt>)", "d5/dc4/dbtool.html#custom-default-schema---schema", null ]
        ] ],
        [ "migrate-to-release <VERSION>", "d5/dc4/dbtool.html#migrate-to-release-version", null ],
        [ "list-pending", "d5/dc4/dbtool.html#list-pending", null ],
        [ "list-applied", "d5/dc4/dbtool.html#list-applied", null ],
        [ "status", "d5/dc4/dbtool.html#status", null ],
        [ "apply <TIMESTAMP>", "d5/dc4/dbtool.html#apply-timestamp", null ],
        [ "rollback <TIMESTAMP>", "d5/dc4/dbtool.html#rollback-timestamp", null ],
        [ "rollback-to <TIMESTAMP>", "d5/dc4/dbtool.html#rollback-to-timestamp", null ],
        [ "mark-applied <TIMESTAMP>", "d5/dc4/dbtool.html#mark-applied-timestamp", null ]
      ] ],
      [ "Backup & Restore", "d5/dc4/dbtool.html#backup--restore", [
        [ "backup", "d5/dc4/dbtool.html#backup", null ],
        [ "restore", "d5/dc4/dbtool.html#restore", null ],
        [ "backup-diff", "d5/dc4/dbtool.html#backup-diff", null ]
      ] ],
      [ "Command-Line Options Reference", "d5/dc4/dbtool.html#command-line-options-reference", [
        [ "Size Suffixes", "d5/dc4/dbtool.html#size-suffixes", null ]
      ] ],
      [ "Plugin System", "d5/dc4/dbtool.html#plugin-system", [
        [ "Creating a Migration Plugin", "d5/dc4/dbtool.html#creating-a-migration-plugin", null ],
        [ "Loading Plugins", "d5/dc4/dbtool.html#loading-plugins", null ],
        [ "Optional Post-Init Hook", "d5/dc4/dbtool.html#optional-post-init-hook", null ]
      ] ],
      [ "Workflow Examples", "d5/dc4/dbtool.html#workflow-examples", [
        [ "Full Migration Workflow", "d5/dc4/dbtool.html#full-migration-workflow", null ],
        [ "Backup Before Migration", "d5/dc4/dbtool.html#backup-before-migration", null ],
        [ "Parallel Backup and Restore", "d5/dc4/dbtool.html#parallel-backup-and-restore", null ]
      ] ],
      [ "Troubleshooting", "d5/dc4/dbtool.html#troubleshooting", [
        [ "Connection Errors", "d5/dc4/dbtool.html#connection-errors", null ],
        [ "Checksum Mismatches", "d5/dc4/dbtool.html#checksum-mismatches", null ],
        [ "Lock Acquisition Failed", "d5/dc4/dbtool.html#lock-acquisition-failed", null ]
      ] ],
      [ "See Also", "d5/dc4/dbtool.html#see-also-2", null ]
    ] ],
    [ "SQL Migrations", "d2/da6/sql-migrations.html", [
      [ "Introduction", "d2/da6/sql-migrations.html#introduction-1", null ],
      [ "Creating Migrations", "d2/da6/sql-migrations.html#creating-migrations", [
        [ "Using the LIGHTWEIGHT_SQL_MIGRATION Macro", "d2/da6/sql-migrations.html#using-the-lightweight_sql_migration-macro", null ],
        [ "Using the Migration Class", "d2/da6/sql-migrations.html#using-the-migration-class", null ],
        [ "Timestamp Format", "d2/da6/sql-migrations.html#timestamp-format", null ],
        [ "Plugin Macro for Shared Libraries", "d2/da6/sql-migrations.html#plugin-macro-for-shared-libraries", null ]
      ] ],
      [ "Table Operations", "d2/da6/sql-migrations.html#table-operations", [
        [ "CreateTable", "d2/da6/sql-migrations.html#createtable", null ],
        [ "AlterTable", "d2/da6/sql-migrations.html#altertable", null ],
        [ "DropTable", "d2/da6/sql-migrations.html#droptable", null ]
      ] ],
      [ "Data Manipulation", "d2/da6/sql-migrations.html#data-manipulation", [
        [ "Insert", "d2/da6/sql-migrations.html#insert-1", null ],
        [ "Update", "d2/da6/sql-migrations.html#update-1", null ],
        [ "Delete", "d2/da6/sql-migrations.html#delete-1", null ],
        [ "CreateIndex", "d2/da6/sql-migrations.html#createindex", null ]
      ] ],
      [ "Raw SQL", "d2/da6/sql-migrations.html#raw-sql", null ],
      [ "SQL Column Types", "d2/da6/sql-migrations.html#sql-column-types", null ],
      [ "Migration Manager API", "d2/da6/sql-migrations.html#migration-manager-api", [
        [ "Custom Default Schema", "d2/da6/sql-migrations.html#custom-default-schema", null ],
        [ "Applying Migrations Programmatically", "d2/da6/sql-migrations.html#applying-migrations-programmatically", null ],
        [ "Status & Verification", "d2/da6/sql-migrations.html#status--verification", null ],
        [ "Preview (Dry-Run)", "d2/da6/sql-migrations.html#preview-dry-run", null ],
        [ "Rollback", "d2/da6/sql-migrations.html#rollback", null ],
        [ "Mark as Applied", "d2/da6/sql-migrations.html#mark-as-applied", null ]
      ] ],
      [ "Migration Tracking", "d2/da6/sql-migrations.html#migration-tracking", [
        [ "schema_migrations Table", "d2/da6/sql-migrations.html#schema_migrations-table", null ],
        [ "Concurrency Control", "d2/da6/sql-migrations.html#concurrency-control", null ]
      ] ],
      [ "Best Practices", "d2/da6/sql-migrations.html#best-practices-1", null ],
      [ "See Also", "d2/da6/sql-migrations.html#see-also-3", null ]
    ] ],
    [ "Asynchronous API (C++23 coroutines)", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html", [
      [ "Why offloading (and not \"true\" async ODBC)", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#why-offloading-and-not-true-async-odbc", null ],
      [ "Concepts", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#concepts", null ],
      [ "Enabling async on a connection", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#enabling-async-on-a-connection", null ],
      [ "Querying asynchronously", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#querying-asynchronously", null ],
      [ "Record-level async methods", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#record-level-async-methods", null ],
      [ "Single-threaded vs multi-threaded", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#single-threaded-vs-multi-threaded", null ],
      [ "Transactions", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#transactions-1", null ],
      [ "Cancellation", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#cancellation", null ],
      [ "Errors", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#errors", null ],
      [ "Integrating with an external event loop / coroutine runtime", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#integrating-with-an-external-event-loop--coroutine-runtime", null ],
      [ "Build", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#build", [
        [ "Interop with <tt>std::execution</tt>", "db/d5f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2async.html#interop-with-stdexecution", null ]
      ] ]
    ] ],
    [ "Announcing Lightweight: Zero-overhead C++23 ODBC wrapper", "d4/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2announcement-reddit.html", [
      [ "</blockquote>", "d4/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2announcement-reddit.html#blockquote", null ],
      [ "Low-Level API: SqlStatement & SqlConnection", "d4/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2announcement-reddit.html#low-level-api-sqlstatement--sqlconnection", [
        [ "Bulk Insertions", "d4/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2announcement-reddit.html#bulk-insertions", null ]
      ] ],
      [ "DataMapper: High-Level ORM", "d4/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2announcement-reddit.html#datamapper-high-level-orm", null ],
      [ "Relationships with Lazy Loading", "d4/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2announcement-reddit.html#relationships-with-lazy-loading", null ],
      [ "Database Migrations in Pure C++", "d4/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2announcement-reddit.html#database-migrations-in-pure-c", null ],
      [ "Backup & Restore", "d4/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2announcement-reddit.html#backup--restore-1", null ],
      [ "Supported Databases", "d4/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2announcement-reddit.html#supported-databases-1", null ],
      [ "What's Next", "d4/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2announcement-reddit.html#whats-next", null ]
    ] ],
    [ "Deprecated List", "da/d58/deprecated.html", null ],
    [ "Topics", "topics.html", "topics" ],
    [ "Concepts", "concepts.html", "concepts" ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", "functions_vars" ],
        [ "Typedefs", "functions_type.html", null ],
        [ "Enumerations", "functions_enum.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"annotated.html",
"d2/dd0/structLightweight_1_1Field.html#a8429602c0af011bde772fa07baa6243e",
"d4/de8/classLightweight_1_1SqlCreateTableQueryBuilder.html#aba0b0aa6a40a801a06a53e274847bcf7",
"d7/d10/classLightweight_1_1SqlWhereClauseBuilder.html#ac4ae456dedf943abf9ef4f277ca35213",
"d8/df7/classLightweight_1_1SqlQueryFormatter.html#aa82cdee7e05bc78bca8345a4bebea3fd",
"da/df2/classLightweight_1_1SqlRequireLoadedError.html#a96304f8f159d5cdd3ef3a0065a464ca2",
"de/d33/classLightweight_1_1SqlSelectQueryBuilder.html#a9c013abb15b8ebeadb694c1ee463bff4",
"df/d7c/structLightweight_1_1SqlGuid.html#a3cf0cf7460e1bf1ac7e36b6153941544"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';