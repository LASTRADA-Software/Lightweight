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
        [ "2.1 Table Definition", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md21-table-definition", null ],
        [ "2.2 Column Definition", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md22-column-definition", null ],
        [ "2.3 Foreign Key Definition", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md23-foreign-key-definition", null ]
      ] ],
      [ "3. Data Chunk Format (<tt>.msgpack</tt>)", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md3-data-chunk-format-msgpack", [
        [ "3.1 Top-Level Structure", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md31-top-level-structure", null ],
        [ "3.2 Column Object", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md32-column-object", null ],
        [ "3.3 Data Types and Encoding", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md33-data-types-and-encoding", null ],
        [ "3.4 Null Handling (<tt>\"n\"</tt>)", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md34-null-handling-n", null ],
        [ "3.5 Packed Binary Format", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md35-packed-binary-format", null ]
      ] ],
      [ "4. File Extension", "d3/dac/md__2home_2runner_2work_2Lightweight_2Lightweight_2docs_2sql-backup-format.html#autotoc_md4-file-extension", null ]
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
"d6/dfa/structLightweight_1_1SqlDateTime.html#a580b08490f5eff7dec0a3e4534d191d5",
"db/dad/classLightweight_1_1SqlStatement.html#a5de8934a879ddef9062eef0d06342ff4",
"dir_3de76f419617a4f3e6f17c058a2a695a.html"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';