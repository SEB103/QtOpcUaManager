#ifndef STRUCTUREDVALUEHIGHLIGHTER_H
#define STRUCTUREDVALUEHIGHLIGHTER_H

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class QTextDocument;

/**
 * Syntax highlighter for the structured Value panel (JSON or XML text).
 *
 * The highlighter attaches to the QTextDocument of the read-only TextArea that
 * shows the structured value produced by StructuredValueFormatter. It only layers
 * QTextCharFormat colors over character ranges; it never edits the document text,
 * so the plain text and clipboard copy stay clean.
 *
 * The active language mirrors OpcUaManager::ValueFormat and is switched with
 * setLanguage(). Two color palettes approximate the Visual Studio Code dark and
 * light themes and are switched with setDarkTheme() to follow the application
 * Material theme; characters not matched by any rule keep the TextArea base color.
 */
class StructuredValueHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    /** Highlighting grammar applied to each text block. */
    enum class Language {
        /** Highlight indented JSON. */
        Json,
        /** Highlight indented XML. */
        Xml
    };

    /** Creates a highlighter attached to \a document, highlighting JSON by default. */
    explicit StructuredValueHighlighter(QTextDocument *document = nullptr);

    /** Switches the active \a language and re-highlights when it changed. */
    void setLanguage(Language language);

    /** Returns the active highlighting language. */
    Language language() const;

    /**
     * Selects the dark (\a dark true) or light color palette and re-highlights
     * when the value changed, so token colors follow the application theme.
     */
    void setDarkTheme(bool dark);

protected:
    /** Highlights one text block (line) using the active language grammar. */
    void highlightBlock(const QString &text) override;

private:
    /** Applies the JSON grammar to \a text, distinguishing keys from string values. */
    void highlightJson(const QString &text);

    /** Applies the XML grammar to \a text (tags, attribute names, attribute values). */
    void highlightXml(const QString &text);

    /** Assigns the token format colors from the active dark/light palette. */
    void applyPalette();

    /** Active highlighting language. */
    Language m_language {Language::Json};

    /** Whether the dark color palette is active; drives applyPalette(). */
    bool m_darkTheme {true};

    /** Format for JSON property names / keys. */
    QTextCharFormat m_keyFormat;
    /** Format for string values (JSON) and attribute values (XML). */
    QTextCharFormat m_stringFormat;
    /** Format for numeric values. */
    QTextCharFormat m_numberFormat;
    /** Format for the JSON literals \c true, \c false, and \c null. */
    QTextCharFormat m_keywordFormat;
    /** Format for structural punctuation and brackets. */
    QTextCharFormat m_punctuationFormat;
    /** Format for XML tag delimiters and element names. */
    QTextCharFormat m_tagFormat;
    /** Format for XML attribute names. */
    QTextCharFormat m_attributeFormat;

    /** Matches a JSON/XML double-quoted string, honoring backslash escapes. */
    QRegularExpression m_stringRegex;
    /** Matches a JSON number. */
    QRegularExpression m_numberRegex;
    /** Matches the JSON literals \c true, \c false, and \c null. */
    QRegularExpression m_keywordRegex;
    /** Matches structural punctuation and brackets. */
    QRegularExpression m_punctuationRegex;
    /** Matches an XML tag start/end delimiter with its optional element name. */
    QRegularExpression m_xmlTagRegex;
    /** Matches an XML attribute name preceding an equals sign. */
    QRegularExpression m_xmlAttributeRegex;
    /** Matches a double-quoted XML attribute value. */
    QRegularExpression m_xmlValueRegex;
};

#endif // STRUCTUREDVALUEHIGHLIGHTER_H
