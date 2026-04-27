// SPDX-License-Identifier: Apache-2.0
//
// `SqlSyntaxHighlighter` paints keyword / literal / comment colours into the
// QTextDocument backing the SQL Query panel's editor. QML hands us its
// `TextEdit.textDocument` (a `QQuickTextDocument*`) and we attach the
// underlying `QTextDocument` once at setup time.
//
// The highlighter is intentionally minimal: keywords, single-quoted strings,
// numbers, line and block comments. That's enough to make hand-written
// queries readable without dragging in a full SQL grammar — the panel's job
// is to be a quick scratchpad, not an IDE.

#pragma once

#include <QtCore/QRegularExpression>
#include <QtGui/QSyntaxHighlighter>
#include <QtGui/QTextCharFormat>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickTextDocument>

#include <vector>

namespace DbtoolGui
{

class SqlSyntaxHighlighter: public QSyntaxHighlighter
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument* textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
  public:
    explicit SqlSyntaxHighlighter(QObject* parent = nullptr);

    [[nodiscard]] QQuickTextDocument* textDocument() const noexcept { return _textDocument; }
    void setTextDocument(QQuickTextDocument* doc);

  signals:
    void textDocumentChanged();

  protected:
    /// Re-highlights one block of text. Called by Qt whenever the user edits
    /// the document. State is preserved between blocks via
    /// `setCurrentBlockState`/`previousBlockState` so multi-line `/* … */`
    /// comments span block boundaries correctly.
    void highlightBlock(QString const& text) override;

  private:
    /// One regex + format pair. Iterated for every block; cheap enough at
    /// the document sizes a query editor sees.
    struct HighlightRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    void BuildRules();

    QQuickTextDocument* _textDocument = nullptr;

    std::vector<HighlightRule> _rules;
    QRegularExpression _blockCommentStart;
    QRegularExpression _blockCommentEnd;
    QTextCharFormat _commentFormat;
};

} // namespace DbtoolGui
