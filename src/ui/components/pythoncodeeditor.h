#pragma once
#include <QPlainTextEdit>
#include <QKeyEvent>

/**
 * PythonCodeEditor – a QPlainTextEdit subclass with IDE-like smart editing:
 *   - Auto-indent: pressing Enter carries forward the current line's indentation.
 *   - Smart indent after colon: if the trimmed line ends with ':', adds one
 *     extra indent level (4 spaces) on the next line.
 *   - Indent with Tab: Tab inserts 4 spaces instead of a tab character.
 *   - Smart dedent with Shift+Tab: removes up to 4 leading spaces.
 *   - Smart backspace: if the cursor is at the start of an indent block,
 *     backspace removes 4 spaces at once.
 *   - Auto-close brackets/quotes: typing (, [, {, ", ' inserts the matching
 *     closing character and places the cursor between them.
 *   - Matching closing char skip: if the next character is already the closing
 *     char you just typed, the cursor moves past it instead of inserting a dupe.
 */
class PythonCodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit PythonCodeEditor(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    // Returns the leading whitespace of the given line text.
    static QString leadingWhitespace(const QString &line);
};
