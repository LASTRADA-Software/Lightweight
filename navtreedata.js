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
      [ "Prepared Statements", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#prepared-statements", null ],
      [ "SQL Query Builder", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#sql-query-builder", null ],
      [ "High level Data Mapping", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#high-level-data-mapping", null ],
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
        [ "Use Pagination or Infinite Scrolling", "d2/d10/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2best-practices.html#use-pagination-or-infinite-scrolling", null ]
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
      [ "Inspect()", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#inspect", null ]
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
      [ "Overview", "d5/dc4/dbtool.html#overview", null ],
      [ "Installation", "d5/dc4/dbtool.html#installation", null ],
      [ "Configuration", "d5/dc4/dbtool.html#configuration", [
        [ "Connection String", "d5/dc4/dbtool.html#connection-string", null ],
        [ "Configuration File Format", "d5/dc4/dbtool.html#configuration-file-format", null ],
        [ "Database-Specific Connection Strings", "d5/dc4/dbtool.html#database-specific-connection-strings", null ]
      ] ],
      [ "Migration Commands", "d5/dc4/dbtool.html#migration-commands", [
        [ "migrate", "d5/dc4/dbtool.html#migrate", null ],
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
        [ "restore", "d5/dc4/dbtool.html#restore", null ]
      ] ],
      [ "Command-Line Options Reference", "d5/dc4/dbtool.html#command-line-options-reference", [
        [ "Size Suffixes", "d5/dc4/dbtool.html#size-suffixes", null ]
      ] ],
      [ "Plugin System", "d5/dc4/dbtool.html#plugin-system", [
        [ "Creating a Migration Plugin", "d5/dc4/dbtool.html#creating-a-migration-plugin", null ],
        [ "Loading Plugins", "d5/dc4/dbtool.html#loading-plugins", null ]
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
      [ "See Also", "d5/dc4/dbtool.html#see-also", null ]
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
        [ "Insert", "d2/da6/sql-migrations.html#insert", null ],
        [ "Update", "d2/da6/sql-migrations.html#update", null ],
        [ "Delete", "d2/da6/sql-migrations.html#delete", null ],
        [ "CreateIndex", "d2/da6/sql-migrations.html#createindex", null ]
      ] ],
      [ "Raw SQL", "d2/da6/sql-migrations.html#raw-sql", null ],
      [ "SQL Column Types", "d2/da6/sql-migrations.html#sql-column-types", null ],
      [ "Migration Manager API", "d2/da6/sql-migrations.html#migration-manager-api", [
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
      [ "See Also", "d2/da6/sql-migrations.html#see-also-1", null ]
    ] ],
    [ "Topics", "topics.html", "topics" ],
    [ "Concepts", "concepts.html", "concepts" ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", null ],
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
"d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a5f02a99faee92eb68b52a9cb49876c0e",
"d8/d65/classLightweight_1_1SqlMigrationQueryBuilder.html#a4290b5a70e3635cd356d83de3983c4b8",
"db/dfd/classLightweight_1_1SqlLogger.html#a89a7b4c6fd89c71bfdab6e81b7f15f35",
"dir_3de76f419617a4f3e6f17c058a2a695a.html"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';