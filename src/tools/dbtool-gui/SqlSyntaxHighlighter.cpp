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
        "SELECT", "FROM", "WHERE", "JOIN", "LEFT", "RIGHT", "INNER", "OUTER",
        "FULL", "CROSS", "ON", "AS", "AND", "OR", "NOT", "NULL",
        "IS", "IN", "LIKE", "BETWEEN", "EXISTS", "ALL", "ANY", "SOME",
        "GROUP", "BY", "ORDER", "HAVING", "LIMIT", "OFFSET", "DISTINCT", "UNION",
        "INSERT", "INTO", "VALUES", "UPDATE", "SET", "DELETE",
        "CREATE", "TABLE", "INDEX", "VIEW", "DROP", "ALTER", "ADD", "COLUMN",
        "PRIMARY", "KEY", "FOREIGN", "REFERENCES", "UNIQUE", "DEFAULT",
        "CASCADE", "RESTRICT", "BEGIN", "COMMIT", "ROLLBACK", "TRANSACTION",
        "CASE", "WHEN", "THEN", "ELSE", "END", "WITH",
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

    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(QStringLiteral("#fbbf24")));
    _rules.push_back({ QRegularExpression(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b")), numberFormat });

    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(QStringLiteral("#4ade80")));
    // Single-quoted SQL strings with `''` doubled-up escapes. Greedy match
    // up to the next unescaped `'`.
    _rules.push_back({ QRegularExpression(QStringLiteral("'(?:[^']|'')*'")), stringFormat });

    _commentFormat.setForeground(QColor(QStringLiteral("#6b7380")));
    _commentFormat.setFontItalic(true);
    // `--` line comments to end of line. Block comments are handled
    // separately because they may span multiple QTextBlocks.
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
            setFormat(static_cast<int>(match.capturedStart()),
                      static_cast<int>(match.capturedLength()),
                      rule.format);
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
