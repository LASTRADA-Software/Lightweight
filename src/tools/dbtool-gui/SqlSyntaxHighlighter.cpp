// SPDX-License-Identifier: Apache-2.0

#include "SqlSyntaxHighlighter.hpp"

#include <QtCore/QStringList>
#include <QtGui/QColor>
#include <QtGui/QTextDocument>

namespace DbtoolGui
{

namespace
{

    /// Curated SQL keyword set. Limited to common DDL/DML/clause words shared
    /// across SQLite, PostgreSQL, and SQL Server; engine-specific keywords
    /// (e.g. SQL Server's `MERGE` extensions) are intentionally omitted to
    /// keep the highlighter dialect-neutral.
    constexpr std::array<char const*, 64> kKeywords = {
        "SELECT",   "FROM",    "WHERE",   "JOIN",     "LEFT",   "RIGHT",  "INNER",    "OUTER",       "FULL",    "CROSS",
        "ON",       "AS",      "AND",     "OR",       "NOT",    "NULL",   "IS",       "IN",          "LIKE",    "BETWEEN",
        "EXISTS",   "ALL",     "ANY",     "SOME",     "GROUP",  "BY",     "ORDER",    "HAVING",      "LIMIT",   "OFFSET",
        "DISTINCT", "UNION",   "INSERT",  "INTO",     "VALUES", "UPDATE", "SET",      "DELETE",      "CREATE",  "TABLE",
        "INDEX",    "VIEW",    "DROP",    "ALTER",    "ADD",    "COLUMN", "PRIMARY",  "KEY",         "FOREIGN", "REFERENCES",
        "UNIQUE",   "DEFAULT", "CASCADE", "RESTRICT", "BEGIN",  "COMMIT", "ROLLBACK", "TRANSACTION", "CASE",    "WHEN",
        "THEN",     "ELSE",    "END",     "WITH",
    };

    /// Curated SQL column-type word list. Covers the type words common to
    /// SQLite, PostgreSQL, and SQL Server. Size/precision specifiers (e.g.
    /// `VARCHAR(255)`) are not part of the match — the parenthesised number
    /// is picked up by the number rule.
    constexpr std::array<char const*, 38> kColumnTypes = {
        "INT",   "INTEGER",   "BIGINT",   "SMALLINT",  "TINYINT",        "NUMERIC", "DECIMAL",   "FLOAT",
        "REAL",  "DOUBLE",    "BIT",      "BOOLEAN",   "BOOL",           "CHAR",    "VARCHAR",   "NVARCHAR",
        "NCHAR", "TEXT",      "NTEXT",    "CLOB",      "BLOB",           "BINARY",  "VARBINARY", "DATE",
        "TIME",  "TIMESTAMP", "DATETIME", "DATETIME2", "DATETIMEOFFSET", "UUID",    "GUID",      "UNIQUEIDENTIFIER",
        "JSON",  "JSONB",     "XML",      "MONEY",     "SMALLMONEY",     "SERIAL",
    };

} // namespace

SqlSyntaxHighlighter::SqlSyntaxHighlighter(QObject* parent):
    QSyntaxHighlighter(static_cast<QTextDocument*>(nullptr))
{
    setParent(parent);
    BuildRules();
}

void SqlSyntaxHighlighter::BuildRules()
{
    _rules.clear();

    // Identifier formatting (table / column names). Violet, distinct from
    // the keyword blue, the type cyan, the string green, and the number
    // yellow so the four token classes stay visually separable.
    QTextCharFormat identifierFormat;
    identifierFormat.setForeground(QColor(QStringLiteral("#c084fc")));

    // Qualified-name parts: the left side of `schema.table` / `table.column`.
    // Capture group 1 is the identifier token; group 0 also includes the
    // trailing whitespace lookahead which we don't want to colour.
    _rules.push_back({
        QRegularExpression(QStringLiteral("([A-Za-z_][A-Za-z0-9_]*)\\s*(?=\\.)")),
        identifierFormat,
        1,
    });
    // Qualified-name parts: the right side of `schema.table` / `table.column`.
    _rules.push_back({
        QRegularExpression(QStringLiteral("\\.\\s*([A-Za-z_][A-Za-z0-9_]*)")),
        identifierFormat,
        1,
    });

    // Unquoted identifier directly after a table-position keyword. Captures
    // only the identifier (group 1) so the leading keyword stays owned by
    // the keyword rule below.
    QRegularExpression tableContextRx(
        QStringLiteral("\\b(?:FROM|JOIN|UPDATE|INTO|TABLE|REFERENCES|INDEX|VIEW)\\s+([A-Za-z_][A-Za-z0-9_]*)"));
    tableContextRx.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    _rules.push_back({ tableContextRx, identifierFormat, 1 });

    QTextCharFormat keywordFormat;
    // Slate-blue accent matches `Theme.accent` ("#0a66d6"). Hardcoded here
    // because QSyntaxHighlighter has no access to the QML Theme singleton —
    // the Theme palette would need to be mirrored in C++ to plumb through,
    // which is more machinery than this small surface justifies.
    keywordFormat.setForeground(QColor(QStringLiteral("#4c8bf5")));
    keywordFormat.setFontWeight(QFont::DemiBold);
    QStringList keywordPatterns;
    keywordPatterns.reserve(kKeywords.size());
    for (auto const* kw: kKeywords)
        keywordPatterns << QStringLiteral("\\b%1\\b").arg(QString::fromLatin1(kw));
    QRegularExpression keywordRx(keywordPatterns.join(QLatin1Char('|')));
    keywordRx.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    _rules.push_back({ keywordRx, keywordFormat });

    // Column-type words (INT, VARCHAR, TIMESTAMP, …) get their own colour
    // so a DDL preview reads as "keyword | identifier | type | literal".
    QTextCharFormat typeFormat;
    typeFormat.setForeground(QColor(QStringLiteral("#22d3ee")));
    QStringList typePatterns;
    typePatterns.reserve(kColumnTypes.size());
    for (auto const* t: kColumnTypes)
        typePatterns << QStringLiteral("\\b%1\\b").arg(QString::fromLatin1(t));
    QRegularExpression typeRx(typePatterns.join(QLatin1Char('|')));
    typeRx.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    _rules.push_back({ typeRx, typeFormat });

    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(QStringLiteral("#fbbf24")));
    _rules.push_back({ QRegularExpression(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b")), numberFormat });

    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(QStringLiteral("#4ade80")));
    // Single-quoted SQL strings with `''` doubled-up escapes. Greedy match
    // up to the next unescaped `'`.
    _rules.push_back({ QRegularExpression(QStringLiteral("'(?:[^']|'')*'")), stringFormat });

    // Quoted identifiers: standard SQL `"name"`, MySQL/SQLite `` `name` ``,
    // and SQL Server `[name]`. Applied after keyword/type/string rules so
    // that an identifier accidentally spelling a keyword (e.g. `"ORDER"`)
    // stays coloured as an identifier — quoting is the user's explicit
    // declaration of intent.
    _rules.push_back({
        QRegularExpression(QStringLiteral("\"(?:[^\"]|\"\")*\"|`[^`]*`|\\[[^\\]]*\\]")),
        identifierFormat,
        0,
    });

    _commentFormat.setForeground(QColor(QStringLiteral("#6b7380")));
    // `--` line comments to end of line. Block comments are handled
    // separately because they may span multiple QTextBlocks. Applied last
    // so comments win over every other rule along their span.
    _rules.push_back({ QRegularExpression(QStringLiteral("--[^\n]*")), _commentFormat });

    _blockCommentStart = QRegularExpression(QStringLiteral("/\\*"));
    _blockCommentEnd = QRegularExpression(QStringLiteral("\\*/"));
}

void SqlSyntaxHighlighter::setTextDocument(QQuickTextDocument* doc)
{
    if (_textDocument == doc)
        return;
    _textDocument = doc;
    setDocument(doc ? doc->textDocument() : nullptr);
    emit textDocumentChanged();
    rehighlight();
}

void SqlSyntaxHighlighter::highlightBlock(QString const& text)
{
    for (auto const& rule: _rules)
    {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext())
        {
            auto const match = it.next();
            auto const start = match.capturedStart(rule.captureIndex);
            auto const length = match.capturedLength(rule.captureIndex);
            if (start < 0 || length <= 0)
                continue;
            setFormat(static_cast<int>(start), static_cast<int>(length), rule.format);
        }
    }

    // Multi-line `/* … */` comments. Block state 1 means "inside a comment
    // that started in an earlier block"; the next end marker terminates it.
    setCurrentBlockState(0);
    qsizetype startIndex = 0;
    if (previousBlockState() != 1)
        startIndex = text.indexOf(_blockCommentStart);

    while (startIndex >= 0)
    {
        auto const endMatch = _blockCommentEnd.match(text, startIndex);
        qsizetype commentLength = 0;
        if (!endMatch.hasMatch())
        {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        }
        else
        {
            commentLength = endMatch.capturedEnd() - startIndex;
        }
        setFormat(static_cast<int>(startIndex), static_cast<int>(commentLength), _commentFormat);
        startIndex = text.indexOf(_blockCommentStart, startIndex + commentLength);
    }
}

} // namespace DbtoolGui
