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
    [ "Lightweight, a C++23 database library", "index.html", "index" ],
    [ "How to", "d1/dde/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2how-to.html", [
      [ "Rename column name", "d1/dde/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2how-to.html#rename-column-name", null ],
      [ "<tt>UPDATE ... RETURNING</tt> / fetching the result of a data-modifying statement", "d1/dde/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2how-to.html#update--returning--fetching-the-result-of-a-data-modifying-statement", null ]
    ] ],
    [ "Usage Examples", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html", [
      [ "Configure default connection information to the database", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#configure-default-connection-information-to-the-database", null ],
      [ "Connection encryption", "d9/d80/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2usage.html#connection-encryption", null ],
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
      [ "Create or Modify database schema", "d9/dbe/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sqlquery.html#create-or-modify-database-schema", [
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
      [ "Relationships", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#relationships", [
        [ "Several foreign keys into the same table", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#several-foreign-keys-into-the-same-table", [
          [ "Writing the rows", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#writing-the-rows", null ],
          [ "Reading them back", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#reading-them-back", null ],
          [ "Self-referential relationships", "d7/d97/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-to-lightweight.html#self-referential-relationships", null ]
        ] ]
      ] ],
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
      [ "<tt>SqlNumeric<Precision, Scale></tt> precision limits", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#sqlnumericprecision-scale-precision-limits", [
        [ "What each accessor delivers", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#what-each-accessor-delivers", null ],
        [ "How many of those digits survive a round-trip", "de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#how-many-of-those-digits-survive-a-round-trip", null ]
      ] ],
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
    [ "dbtool - Database Management CLI", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html", [
      [ "Overview", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#overview-1", null ],
      [ "Installation", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#installation", [
        [ "Pre-built installers", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#pre-built-installers", null ],
        [ "Building from source", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#building-from-source", null ],
        [ "Producing the installer locally", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#producing-the-installer-locally", null ]
      ] ],
      [ "Configuration", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#configuration", [
        [ "Connection String", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#connection-string", null ],
        [ "Configuration File Format", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#configuration-file-format", null ],
        [ "Inspecting configured profiles", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#inspecting-configured-profiles", null ],
        [ "Database-Specific Connection Strings", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#database-specific-connection-strings", null ]
      ] ],
      [ "Migration Commands", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#migration-commands", [
        [ "migrate", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#migrate", [
          [ "Custom default schema (<tt>--schema</tt>)", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#custom-default-schema---schema", null ]
        ] ],
        [ "migrate-to-release <VERSION>", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#migrate-to-release-version", null ],
        [ "list-pending", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#list-pending", null ],
        [ "list-applied", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#list-applied", null ],
        [ "status", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#status", null ],
        [ "apply <TIMESTAMP>", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#apply-timestamp", null ],
        [ "rollback <TIMESTAMP>", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#rollback-timestamp", null ],
        [ "rollback-to <TIMESTAMP>", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#rollback-to-timestamp", null ],
        [ "mark-applied <TIMESTAMP>", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#mark-applied-timestamp", null ],
        [ "rollback-to-release <VERSION>", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#rollback-to-release-version", null ],
        [ "releases", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#releases", null ],
        [ "rewrite-checksums", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#rewrite-checksums", null ],
        [ "hard-reset", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#hard-reset", null ],
        [ "unicode-upgrade-tables", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#unicode-upgrade-tables", null ],
        [ "exec <QUERY>", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#exec-query", null ],
        [ "list-profiles", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#list-profiles", null ],
        [ "resolve-secret <REF>", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#resolve-secret-ref", null ]
      ] ],
      [ "Backup & Restore", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#backup--restore", [
        [ "backup", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#backup", null ],
        [ "restore", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#restore", null ],
        [ "backup-diff", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#backup-diff", null ]
      ] ],
      [ "Command-Line Options Reference", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#command-line-options-reference", [
        [ "Size Suffixes", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#size-suffixes", null ]
      ] ],
      [ "Plugin System", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#plugin-system", [
        [ "Creating a Migration Plugin", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#creating-a-migration-plugin", null ],
        [ "Loading Plugins", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#loading-plugins", null ],
        [ "Optional Post-Init Hook", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#optional-post-init-hook", null ]
      ] ],
      [ "Workflow Examples", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#workflow-examples", [
        [ "Full Migration Workflow", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#full-migration-workflow", null ],
        [ "Backup Before Migration", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#backup-before-migration", null ],
        [ "Parallel Backup and Restore", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#parallel-backup-and-restore", null ]
      ] ],
      [ "Troubleshooting", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#troubleshooting", [
        [ "Connection Errors", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#connection-errors", null ],
        [ "Checksum Mismatches", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#checksum-mismatches", null ],
        [ "Lock Acquisition Failed", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#lock-acquisition-failed", null ]
      ] ],
      [ "See Also", "de/d60/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2dbtool.html#see-also-2", null ]
    ] ],
    [ "SQL Migrations", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html", [
      [ "Introduction", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#introduction-1", null ],
      [ "Creating Migrations", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#creating-migrations", [
        [ "Using the LIGHTWEIGHT_SQL_MIGRATION Macro", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#using-the-lightweight_sql_migration-macro", null ],
        [ "Using the Migration Class", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#using-the-migration-class", null ],
        [ "Timestamp Format", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#timestamp-format", null ],
        [ "Plugin Macro for Shared Libraries", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#plugin-macro-for-shared-libraries", null ]
      ] ],
      [ "Table Operations", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#table-operations", [
        [ "CreateTable", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#createtable", null ],
        [ "AlterTable", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#altertable", null ],
        [ "DropTable", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#droptable", null ]
      ] ],
      [ "Data Manipulation", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#data-manipulation", [
        [ "Insert", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#insert-1", null ],
        [ "Update", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#update-1", null ],
        [ "Delete", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#delete-1", null ],
        [ "CreateIndex", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#createindex", null ]
      ] ],
      [ "Raw SQL", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#raw-sql", null ],
      [ "SQL Column Types", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#sql-column-types", null ],
      [ "Migration Manager API", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#migration-manager-api", [
        [ "Custom Default Schema", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#custom-default-schema", null ],
        [ "Applying Migrations Programmatically", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#applying-migrations-programmatically", null ],
        [ "Status & Verification", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#status--verification", null ],
        [ "Preview (Dry-Run)", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#preview-dry-run", null ],
        [ "Rollback", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#rollback", null ],
        [ "Mark as Applied", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#mark-as-applied", null ]
      ] ],
      [ "Migration Tracking", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#migration-tracking", [
        [ "schema_migrations Table", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#schema_migrations-table", null ],
        [ "Concurrency Control", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#concurrency-control", null ]
      ] ],
      [ "Best Practices", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#best-practices-1", null ],
      [ "See Also", "d7/d9a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-migrations.html#see-also-3", null ]
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
    [ "Logging and tracing", "d1/d4f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2logging.html", [
      [ "The built-in loggers", "d1/d4f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2logging.html#the-built-in-loggers", null ],
      [ "Redirecting the output", "d1/d4f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2logging.html#redirecting-the-output", null ],
      [ "Writing a custom logger", "d1/d4f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2logging.html#writing-a-custom-logger", null ],
      [ "Restoring the previous logger", "d1/d4f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2logging.html#restoring-the-previous-logger", null ],
      [ "See also", "d1/d4f/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2logging.html#see-also-4", null ]
    ] ],
    [ "Schema introspection", "d4/d61/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2schema-introspection.html", [
      [ "Reading every table", "d4/d61/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2schema-introspection.html#reading-every-table", null ],
      [ "What you get back", "d4/d61/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2schema-introspection.html#what-you-get-back", null ],
      [ "Following relationships directly", "d4/d61/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2schema-introspection.html#following-relationships-directly", null ],
      [ "Turning a description back into DDL", "d4/d61/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2schema-introspection.html#turning-a-description-back-into-ddl", null ],
      [ "Event-driven reading", "d4/d61/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2schema-introspection.html#event-driven-reading", null ],
      [ "See also", "d4/d61/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2schema-introspection.html#see-also-5", null ]
    ] ],
    [ "Composite key support — design", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html", [
      [ "The design: <tt>CompositeForeignKey<Connection<...>, ...></tt>", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#the-design-compositeforeignkeyconnection-", null ],
      [ "Why the pairing matters", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#why-the-pairing-matters", null ],
      [ "What it derives, and what it rejects", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#what-it-derives-and-what-it-rejects", null ],
      [ "How it works", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#how-it-works", [
        [ "Column binding: nothing changes", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#column-binding-nothing-changes", null ],
        [ "The relation holds no key storage", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#the-relation-holds-no-key-storage", null ],
        [ "Loading reuses machinery that already takes N values", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#loading-reuses-machinery-that-already-takes-n-values", null ],
        [ "Ordering — settled by test", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#ordering--settled-by-test", null ],
        [ "Navigation", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#navigation", null ],
        [ "<tt>ddl2cpp</tt> generation is mechanical", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#ddl2cpp-generation-is-mechanical", null ]
      ] ],
      [ "The inverse", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#the-inverse", null ],
      [ "The other half: reflected identity", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#the-other-half-reflected-identity", null ],
      [ "Implementation plan", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#implementation-plan", null ],
      [ "Prototype", "d2/d3a/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2composite-keys-design.html#prototype", null ]
    ] ],
    [ "Relation generation in ddl2cpp", "d5/dd9/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2ddl2cpp-relation-generation.html", [
      [ "Status", "d5/dd9/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2ddl2cpp-relation-generation.html#status-1", null ],
      [ "Reference schema", "d5/dd9/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2ddl2cpp-relation-generation.html#reference-schema", [
        [ "Column types in use", "d5/dd9/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2ddl2cpp-relation-generation.html#column-types-in-use", null ]
      ] ],
      [ "What this schema needs that is not generated", "d5/dd9/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2ddl2cpp-relation-generation.html#what-this-schema-needs-that-is-not-generated", [
        [ "1. <tt>HasMany</tt> — the inverse of every single-column foreign key", "d5/dd9/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2ddl2cpp-relation-generation.html#autotoc_md1-hasmany--the-inverse-of-every-single-column-foreign-key", null ],
        [ "2. <tt>HasManyThrough</tt> — many-to-many across a join table", "d5/dd9/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2ddl2cpp-relation-generation.html#autotoc_md2-hasmanythrough--many-to-many-across-a-join-table", null ],
        [ "3. <tt>HasOneThrough</tt> / one-to-one", "d5/dd9/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2ddl2cpp-relation-generation.html#autotoc_md3-hasonethrough--one-to-one", null ]
      ] ],
      [ "What is not representable, and why", "d5/dd9/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2ddl2cpp-relation-generation.html#what-is-not-representable-and-why", null ],
      [ "Generation rules", "d5/dd9/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2ddl2cpp-relation-generation.html#generation-rules", null ]
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
"d2/dd0/structLightweight_1_1Field.html#a757ce7d3d9533493051cd9b402482b41",
"d4/de8/classLightweight_1_1SqlCreateTableQueryBuilder.html#a972ef8522f91fce43fd223357b709bae",
"d7/d13/structLightweight_1_1SqlRealName.html#ac06cf19889d9c829b639fe1da6c44048",
"d8/dad/classLightweight_1_1SqlAlterTableQueryBuilder.html#acb8f4c9ca3abc544b7c967a07163ae1c",
"da/da8/classLightweight_1_1SqlConnection.html#a7f4f112f86b9fd3fa94d171ddb449324",
"dd/d71/structLightweight_1_1SqlColumnDeclaration.html#adc6c2316125cc865139f3dae21badfb6",
"de/db0/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2data-binder.html#getcolumn",
"index.html#simple-one-record-example"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';