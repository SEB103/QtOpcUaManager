import QtQuick
import QtQuick.Controls
import QtTest
import Base

/*!
    \qmltype tst_attributes
    \brief Hosts behavior tests for the read-only, copyable Attributes panel.

    The cases verify that attribute rows can be selected and copied with the
    standard Copy shortcut, that they cannot be edited, and that a competing
    window-wide Copy shortcut (as registered by the address-space tree) only
    fires when the focused attribute field has no selection.
*/
Item {
    id: root

    /*! Number of times the competing window-wide Copy shortcut fired. */
    property int shortcutHits: 0

    width: 1000
    height: 700

    // Stands in for the node-id Copy shortcut of BsAddressSpaceTreePane.
    Shortcut {
        sequences: [ StandardKey.Copy ]
        onActivated: root.shortcutHits++
    }

    // Reads back and seeds the clipboard without touching the panel.
    TextEdit {
        id: clipboardProbe

        width: 200
        height: 20
    }

    // Carries the theme font the attribute rows must keep using.
    Label {
        id: themeLabel

        visible: false
    }

    Component {
        id: attributesComponent

        BsNodeAttributes {
            width: 600
            height: 600
        }
    }

    TestCase {
        id: testCase

        /*! Test case for selecting and copying Attributes panel text. */
        name: "Attributes"
        when: windowShown

        function init() {
            root.shortcutHits = 0;
        }

        /*!
            Creates the panel and returns the selectable text named \a objectName
            in the row whose attribute is \a attribute.
        */
        function attributeField(attribute, objectName) {
            const panel = createTemporaryObject(attributesComponent, root);
            verify(panel !== null);
            const view = findChild(panel, "attributesView");
            verify(view !== null);
            // Delegates and their row layouts are created on the next polish, so
            // wait until the panel is rendered before locating and clicking a row.
            waitForRendering(panel);

            let field = null;
            tryVerify(() => {
                for (let row = 0; row < view.count; ++row) {
                    const delegate = view.itemAtIndex(row);
                    if (delegate && delegate.attribute === attribute) {
                        field = findChild(delegate, objectName);
                        return field !== null;
                    }
                }
                return false;
            });
            return field;
        }

        /*! Gives keyboard focus to \a field the way a user click does. */
        function focusField(field) {
            root.Window.window.requestActivate();
            tryVerify(() => root.Window.active);
            mouseClick(field, 2, field.height / 2);
            tryVerify(() => field.activeFocus);
        }

        /*! Puts \a text on the clipboard through the probe editor. */
        function setClipboard(text) {
            clipboardProbe.text = text;
            clipboardProbe.selectAll();
            clipboardProbe.copy();
            clipboardProbe.deselect();
        }

        /*! Returns the current clipboard text by pasting it into the probe editor. */
        function clipboardText() {
            clipboardProbe.text = "";
            clipboardProbe.paste();
            return clipboardProbe.text;
        }

        /*! Verifies that typing, deleting, cutting, and pasting leave a value unchanged. */
        function test_valueIsReadOnly() {
            const field = attributeField("NodeId", "attributeValueText");
            verify(field !== null);
            verify(field.readOnly);
            setClipboard("junk");

            focusField(field);
            keyClick(Qt.Key_X);
            keyClick(Qt.Key_Backspace);
            keyClick(Qt.Key_Delete);
            keySequence(StandardKey.Paste);
            keySequence(StandardKey.SelectAll);
            keySequence(StandardKey.Cut);

            compare(field.text, "ns=2;s=Temp");
        }

        /*! Verifies that Ctrl+A and Ctrl+C copy the selected value, not the node id. */
        function test_copyShortcutCopiesSelectedValue() {
            const field = attributeField("DisplayName", "attributeValueText");
            verify(field !== null);
            setClipboard("junk");

            focusField(field);
            keySequence(StandardKey.SelectAll);
            compare(field.selectedText, "Temperature");
            keySequence(StandardKey.Copy);

            compare(root.shortcutHits, 0);
            compare(clipboardText(), "Temperature");
        }

        /*! Verifies that the selectable rows keep the Label theme font they replaced. */
        function test_rowsUseLabelFont() {
            const name = attributeField("NodeId", "attributeNameText");
            const value = findChild(name.parent, "attributeValueText");
            verify(value !== null);

            compare(value.font.family, themeLabel.font.family);
            compare(value.font.pixelSize, themeLabel.font.pixelSize);
            compare(value.font.bold, false);
            compare(name.font.pixelSize, themeLabel.font.pixelSize);
            compare(name.font.bold, true);
        }

        /*! Verifies that the attribute name column is selectable and copyable too. */
        function test_copyShortcutCopiesSelectedName() {
            const field = attributeField("NodeId", "attributeNameText");
            verify(field !== null);
            verify(field.readOnly);
            setClipboard("junk");

            focusField(field);
            keySequence(StandardKey.SelectAll);
            keySequence(StandardKey.Copy);

            compare(root.shortcutHits, 0);
            compare(clipboardText(), "NodeId");
        }

        /*! Verifies that Ctrl+C without a selection still reaches the window shortcut. */
        function test_copyWithoutSelectionFallsThrough() {
            const field = attributeField("NodeId", "attributeValueText");
            verify(field !== null);

            focusField(field);
            field.deselect();
            keySequence(StandardKey.Copy);

            compare(root.shortcutHits, 1);
        }
    }
}
