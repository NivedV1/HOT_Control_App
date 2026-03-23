#include "pythoncodeeditor.h"
#include <QTextCursor>
#include <QTextBlock>

static constexpr int kIndentSize = 4;
static const QString kIndent(kIndentSize, ' ');

// Pairs for auto-close
static const QMap<QChar, QChar> kAutoClose = {
    {'(', ')'},
    {'[', ']'},
    {'{', '}'},
    {'"', '"'},
    {'\'', '\''},
};

PythonCodeEditor::PythonCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    // Spaces per indent level visible in the ruler
    setTabStopDistance(kIndentSize * fontMetrics().horizontalAdvance(' '));
}

QString PythonCodeEditor::leadingWhitespace(const QString &line)
{
    int i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    return line.left(i);
}

void PythonCodeEditor::keyPressEvent(QKeyEvent *event)
{
    QTextCursor cursor = textCursor();

    // ── Tab → insert 4 spaces ──────────────────────────────────────────────
    if (event->key() == Qt::Key_Tab && !(event->modifiers() & Qt::ShiftModifier)) {
        cursor.insertText(kIndent);
        return;
    }

    // ── Shift+Tab → remove up to 4 leading spaces ─────────────────────────
    if (event->key() == Qt::Key_Tab && (event->modifiers() & Qt::ShiftModifier)) {
        const QString lineText = cursor.block().text();
        int spaces = 0;
        while (spaces < kIndentSize && spaces < lineText.size() && lineText[spaces] == ' ') {
            ++spaces;
        }
        if (spaces > 0) {
            QTextCursor c = cursor;
            c.movePosition(QTextCursor::StartOfBlock);
            c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, spaces);
            c.removeSelectedText();
        }
        return;
    }

    // ── Backspace: smart dedent ────────────────────────────────────────────
    if (event->key() == Qt::Key_Backspace && !cursor.hasSelection()) {
        const QString lineText = cursor.block().text();
        const int posInBlock = cursor.positionInBlock();

        // If cursor is at end of an indent block (all spaces before cursor)
        if (posInBlock > 0 && posInBlock <= lineText.size()) {
            const QString before = lineText.left(posInBlock);
            if (!before.trimmed().isEmpty() == false) { // all spaces
                const int remove = (posInBlock % kIndentSize == 0) ? kIndentSize
                                                                    : posInBlock % kIndentSize;
                cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, remove);
                cursor.removeSelectedText();
                return;
            }
        }
        // fallthrough to default
    }

    // ── Enter: auto-indent (+ extra indent after colon) ───────────────────
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const QString lineText = cursor.block().text();
        QString indent = leadingWhitespace(lineText);

        // Check if the trimmed line ends with ':'
        const QString trimmed = lineText.trimmed();
        if (!trimmed.isEmpty() && trimmed.back() == ':') {
            indent += kIndent; // add one indent level
        }

        cursor.insertText('\n' + indent);
        ensureCursorVisible();
        return;
    }

    // ── Auto-close brackets & quotes ──────────────────────────────────────
    if (!cursor.hasSelection() && event->text().size() == 1) {
        const QChar ch = event->text().front();

        // If typing a closing char that already exists at cursor → skip over it
        if (kAutoClose.values().contains(ch)) {
            const int pos = cursor.position();
            const QString docText = document()->toPlainText();
            if (pos < docText.size() && docText[pos] == ch) {
                cursor.movePosition(QTextCursor::Right);
                setTextCursor(cursor);
                return;
            }
        }

        // Insert opening char + matching closing char
        if (kAutoClose.contains(ch)) {
            const QChar closing = kAutoClose.value(ch);
            cursor.insertText(QString(ch) + closing);
            cursor.movePosition(QTextCursor::Left);
            setTextCursor(cursor);
            return;
        }
    }

    // Default behaviour for all other keys
    QPlainTextEdit::keyPressEvent(event);
}
