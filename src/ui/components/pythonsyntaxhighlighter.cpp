#include "pythonsyntaxhighlighter.h"
#include <QColor>
#include <QFont>

PythonSyntaxHighlighter::PythonSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) {
    keywordFormat.setForeground(QColor(86, 156, 214));
    keywordFormat.setFontWeight(QFont::Bold);

    builtinFormat.setForeground(QColor(78, 201, 176));
    commentFormat.setForeground(QColor(106, 153, 85));
    stringFormat.setForeground(QColor(214, 157, 133));
    numberFormat.setForeground(QColor(181, 206, 168));
    decoratorFormat.setForeground(QColor(197, 134, 192));

    const QStringList keywords = {
        "False", "None", "True", "and", "as", "assert", "async", "await",
        "break", "class", "continue", "def", "del", "elif", "else", "except",
        "finally", "for", "from", "global", "if", "import", "in", "is",
        "lambda", "nonlocal", "not", "or", "pass", "raise", "return",
        "try", "while", "with", "yield"
    };
    for (const QString &word : keywords) {
        rules.append({QRegularExpression(QString("\\b%1\\b").arg(word)), keywordFormat});
    }

    const QStringList builtins = {
        "abs", "all", "any", "bool", "dict", "enumerate", "float", "int", "len",
        "list", "map", "max", "min", "print", "range", "round", "set", "sorted",
        "str", "sum", "tuple", "zip"
    };
    for (const QString &word : builtins) {
        rules.append({QRegularExpression(QString("\\b%1\\b").arg(word)), builtinFormat});
    }

    rules.append({QRegularExpression("#[^\\n]*"), commentFormat});
    rules.append({QRegularExpression("'[^'\\\\]*(?:\\\\.[^'\\\\]*)*'"), stringFormat});
    rules.append({QRegularExpression("\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\""), stringFormat});
    rules.append({QRegularExpression("\\b\\d+(?:\\.\\d+)?\\b"), numberFormat});
    rules.append({QRegularExpression("^\\s*@[A-Za-z_][A-Za-z0-9_\\.]*"), decoratorFormat});
}

void PythonSyntaxHighlighter::highlightBlock(const QString &text) {
    for (const HighlightRule &rule : rules) {
        QRegularExpressionMatchIterator matches = rule.pattern.globalMatch(text);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
