import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs

ApplicationWindow {
    id: root
    width: 1440
    height: 940
    minimumWidth: 1120
    minimumHeight: 760
    visible: true
    color: Theme.background
    title: (studio.dirty ? "● " : "") + studio.packTitle + " — Buzz! Question Studio"
    font.family: Qt.platform.os === "windows" ? "Segoe UI" : "Sans Serif"
    font.pixelSize: 13
    palette.window: Theme.panel
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.button: Theme.panel
    palette.buttonText: Theme.text
    palette.base: Theme.input
    palette.highlight: Theme.accentDark
    palette.highlightedText: Theme.text
    property var q: studio.current
    property bool hasQuestion: q.id !== undefined
    property string categoryFilter: ""
    property string kindFilter: ""
    property int editorTab: 0
    property bool allowClose: false
    property var pendingAction: null
    property var afterSave: null
    readonly property var subjects: [
        {
            name: qsTr("Nauka i przyroda"),
            id: 235,
            sub: 237
        },
        {
            name: qsTr("Sztuka i literatura"),
            id: 177,
            sub: 179
        },
        {
            name: qsTr("Historia i wydarzenia"),
            id: 182,
            sub: 184
        },
        {
            name: qsTr("Czas wolny"),
            id: 188,
            sub: 191
        },
        {
            name: qsTr("Film"),
            id: 199,
            sub: 208
        },
        {
            name: qsTr("Muzyka"),
            id: 175,
            sub: 224
        },
        {
            name: qsTr("Ludzie"),
            id: 230,
            sub: 232
        },
        {
            name: qsTr("Sport"),
            id: 174,
            sub: 247
        },
        {
            name: qsTr("Świat"),
            id: 250,
            sub: 253
        },
        {
            name: qsTr("Telewizja"),
            id: 257,
            sub: 271
        }
    ]
    function commitEdits() {
        editCommitFocus.forceActiveFocus();
    }
    Item {
        id: editCommitFocus
        // A sibling receives focus so editingFinished/activeFocusChanged run
        // before selection or save. Focusing the parent retains its focus child.
    }
    PackDialogs {
        id: packDialogs
        objectName: "packDialogs"
        controller: studio
        host: root
    }
    function filter() {
        studio.setFilter(search.text, categoryFilter, kindFilter);
    }
    function guard(action) {
        commitEdits();
        if (studio.busy)
            return;
        if (studio.dirty) {
            pendingAction = action;
            unsaved.open();
        } else
            action();
    }
    function save(done) {
        commitEdits();
        afterSave = done || null;
        if (studio.projectPath.length > 0) {
            if (studio.saveFile()) {
                if (afterSave)
                    afterSave();
                afterSave = null;
            }
        } else
            saveDialog.open();
    }
    function subcategoryItems(subject) {
        const values = {
            "235": [
                {
                    id: 237,
                    name: qsTr("Nauka")
                },
                {
                    id: 236,
                    name: qsTr("Internet i technologia")
                },
                {
                    id: 238,
                    name: qsTr("Człowiek")
                },
                {
                    id: 239,
                    name: qsTr("Fizyka i chemia")
                },
                {
                    id: 240,
                    name: qsTr("Przyroda")
                }
            ],
            "177": [
                {
                    id: 179,
                    name: qsTr("Książki i pisarze")
                },
                {
                    id: 178,
                    name: qsTr("Sztuka i artyści")
                },
                {
                    id: 181,
                    name: qsTr("Teatr")
                }
            ],
            "182": [
                {
                    id: 184,
                    name: qsTr("Historia")
                },
                {
                    id: 183,
                    name: qsTr("Aktualności")
                },
                {
                    id: 187,
                    name: qsTr("Trendy")
                }
            ],
            "188": [
                {
                    id: 191,
                    name: qsTr("Zainteresowania")
                },
                {
                    id: 189,
                    name: qsTr("Dom")
                },
                {
                    id: 196,
                    name: qsTr("Gry i zabawy")
                }
            ],
            "199": [
                {
                    id: 208,
                    name: qsTr("Kino")
                }
            ],
            "175": [
                {
                    id: 224,
                    name: qsTr("Muzyka popularna")
                }
            ],
            "230": [
                {
                    id: 232,
                    name: qsTr("Społeczeństwo")
                }
            ],
            "174": [
                {
                    id: 247,
                    name: qsTr("Dyscypliny sportowe")
                }
            ],
            "250": [
                {
                    id: 253,
                    name: qsTr("Geografia")
                },
                {
                    id: 251,
                    name: qsTr("Wierzenia")
                }
            ],
            "257": [
                {
                    id: 271,
                    name: qsTr("Programy TV")
                }
            ]
        };
        return values[String(subject)] || [];
    }
    onClosing: function (event) {
        if (!allowClose) {
            event.accepted = false;
            guard(function () {
                allowClose = true;
                root.close();
            });
        }
    }
    Shortcut {
        sequence: StandardKey.Save
        onActivated: root.save()
    }
    Shortcut {
        sequence: StandardKey.SaveAs
        onActivated: {
            root.commitEdits();
            root.afterSave = null;
            saveDialog.open();
        }
    }
    Shortcut {
        sequence: StandardKey.Open
        onActivated: root.guard(function () {
            openDialog.open();
        })
    }
    Shortcut {
        sequence: StandardKey.New
        onActivated: root.guard(function () {
            studio.newProject();
        })
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        onActivated: {
            root.commitEdits();
            studio.undo();
        }
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        onActivated: {
            root.commitEdits();
            studio.redo();
        }
    }
    Connections {
        target: studio
        function onErrorOccurred(message) {
            errorText.text = message;
            errorDialog.open();
        }
        function onImportLanguageRequested(choices, preferred) {
            importLanguageCombo.model = choices;
            importLanguageCombo.currentIndex = Math.max(0, importLanguageCombo.indexOfValue(preferred));
            importLanguageDialog.open();
        }
    }

    header: Rectangle {
        height: 76
        color: Theme.sidebar
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 14
            Image {
                source: "qrc:/studio.svg"
                Layout.preferredWidth: 35
                Layout.preferredHeight: 35
            }
            Column {
                spacing: 1
                Text {
                    text: "Buzz!"
                    color: Theme.text
                    font.pixelSize: 22
                    font.bold: true
                }
                Text {
                    text: "QUESTION STUDIO"
                    color: Theme.muted
                    font.pixelSize: 9
                    font.letterSpacing: 1.5
                }
            }
            Rectangle {
                Layout.leftMargin: 20
                Layout.rightMargin: 8
                Layout.preferredHeight: 30
                Layout.preferredWidth: 1
                color: Theme.border
            }
            Column {
                spacing: 3
                Layout.fillWidth: true
                Text {
                    text: studio.packTitle
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    width: parent.width
                }
                Text {
                    text: studio.dirty ? qsTr("Niezapisane zmiany") : studio.projectPath.length ? qsTr("Wszystkie zmiany zapisane") : qsTr("Projekt lokalny")
                    color: studio.dirty ? Theme.accent : Theme.muted
                    font.pixelSize: 11
                }
            }
            StudioButton {
                text: studio.packLanguage.toUpperCase()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Język pytań")
                enabled: !studio.busy
                onClicked: {
                    root.commitEdits();
                    projectLanguageDialog.open();
                }
            }
            StudioButton {
                objectName: "openLibraryButton"
                text: qsTr("Otwórz")
                enabled: !studio.busy
                onClicked: root.guard(function () {
                    openDialog.open();
                })
            }
            StudioButton {
                text: qsTr("↓  Importuj")
                enabled: !studio.busy
                onClicked: {
                    root.commitEdits();
                    importDialog.open();
                }
            }
            StudioButton {
                text: qsTr("Zapisz projekt")
                enabled: !studio.busy
                onClicked: root.save()
            }
            StudioButton {
                objectName: "buildPackButton"
                text: qsTr("Zbuduj paczkę")
                tone: "primary"
                enabled: !studio.busy && studio.totalCount > 0
                onClicked: packDialogs.openBuild()
            }
            StudioButton {
                text: "···"
                implicitWidth: 38
                enabled: !studio.busy
                onClicked: moreMenu.popup()
            }
            Menu {
                id: moreMenu
                MenuItem {
                    text: qsTr("Zbuduj paczkę .edat…")
                    enabled: studio.totalCount > 0
                    onTriggered: packDialogs.openBuild()
                }
                MenuItem {
                    text: qsTr("Zainstaluj paczkę .edat…")
                    onTriggered: packDialogs.chooseInstall()
                }
                MenuSeparator {}
                Menu {
                    title: qsTr("Język interfejsu")
                    MenuItem {
                        text: "Polski"
                        checkable: true
                        checked: i18n.language === "pl"
                        onTriggered: {
                            root.commitEdits();
                            i18n.language = "pl";
                        }
                    }
                    MenuItem {
                        text: "English"
                        checkable: true
                        checked: i18n.language === "en"
                        onTriggered: {
                            root.commitEdits();
                            i18n.language = "en";
                        }
                    }
                }
                MenuItem {
                    text: qsTr("Języki paczki…")
                    onTriggered: {
                        root.commitEdits();
                        projectLanguageDialog.open();
                    }
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Nowa paczka")
                    onTriggered: root.guard(function () {
                        studio.newProject();
                    })
                }
                MenuItem {
                    text: qsTr("Zapisz jako…")
                    onTriggered: {
                        root.commitEdits();
                        root.afterSave = null;
                        saveDialog.open();
                    }
                }
                MenuItem {
                    text: qsTr("Eksport JSON dla buzzpack…")
                    onTriggered: {
                        root.commitEdits();
                        exportDialog.open();
                    }
                }
                MenuItem {
                    text: qsTr("Zmień nazwę paczki…")
                    onTriggered: {
                        packName.text = studio.packTitle;
                        packDialog.open();
                    }
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Przykładowy projekt")
                    onTriggered: root.guard(function () {
                        studio.newProject(true);
                    })
                }
                MenuItem {
                    text: qsTr("Odzyskaj autozapis")
                    enabled: studio.recoveryAvailable
                    onTriggered: root.guard(function () {
                        studio.recover();
                    })
                }
                MenuItem {
                    text: qsTr("O aplikacji")
                    onTriggered: aboutDialog.open()
                }
            }
        }
    }
    footer: Rectangle {
        height: 34
        color: Theme.sidebar
        Rectangle {
            height: 1
            width: parent.width
            color: Theme.border
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            spacing: 9
            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: studio.busy ? Theme.accent : Theme.success
            }
            Text {
                text: studio.status
                color: Theme.muted
                font.pixelSize: 11
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: "UTF-8   ·   " + studio.totalCount + qsTr(" pytań   ·   ") + (Qt.platform.os === "windows" ? "Windows" : "Linux")
                color: Theme.muted
                font.pixelSize: 10
            }
        }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 214
            Layout.fillHeight: true
            color: Theme.sidebar
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 5
                Text {
                    text: qsTr("TWOJA BIBLIOTEKA")
                    color: Theme.muted
                    font.pixelSize: 10
                    font.letterSpacing: 1.3
                    Layout.topMargin: 10
                    Layout.bottomMargin: 12
                }
                Repeater {
                    model: [
                        {
                            title: qsTr("Wszystkie pytania"),
                            key: "",
                            count: studio.totalCount
                        },
                        {
                            title: qsTr("Z obrazkiem"),
                            key: "images",
                            count: studio.imageCount
                        },
                        {
                            title: qsTr("Z nagraniem"),
                            key: "audio",
                            count: studio.audioCount
                        },
                        {
                            title: qsTr("Szkice"),
                            key: "drafts",
                            count: -1
                        }
                    ]
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 38
                        contentItem: RowLayout {
                            Text {
                                text: modelData.title
                                color: root.kindFilter === modelData.key && root.categoryFilter === "" ? Theme.accent : Theme.muted
                                font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                            Text {
                                text: modelData.count >= 0 ? modelData.count : ""
                                color: Theme.muted
                                font.pixelSize: 11
                            }
                        }
                        background: Rectangle {
                            radius: 7
                            color: root.kindFilter === modelData.key && root.categoryFilter === "" ? Theme.accentDark : "transparent"
                        }
                        onClicked: {
                            root.commitEdits();
                            root.kindFilter = modelData.key;
                            root.categoryFilter = "";
                            root.filter();
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.border
                    Layout.topMargin: 17
                    Layout.bottomMargin: 16
                }
                Text {
                    text: qsTr("KATEGORIE")
                    color: Theme.muted
                    font.pixelSize: 10
                    font.letterSpacing: 1.3
                    Layout.bottomMargin: 9
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 3
                    model: studio.categories
                    delegate: ItemDelegate {
                        required property var modelData
                        width: ListView.view.width
                        height: 34
                        contentItem: RowLayout {
                            Rectangle {
                                width: 5
                                height: 5
                                radius: 2.5
                                color: root.categoryFilter === modelData.name ? Theme.accent : Theme.muted
                            }
                            Text {
                                text: modelData.name
                                color: root.categoryFilter === modelData.name ? Theme.text : Theme.muted
                                font.pixelSize: 11
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Text {
                                text: modelData.count
                                color: Theme.muted
                                font.pixelSize: 10
                            }
                        }
                        background: Rectangle {
                            radius: 6
                            color: root.categoryFilter === modelData.name ? "#242A35" : "transparent"
                        }
                        onClicked: {
                            root.commitEdits();
                            root.categoryFilter = modelData.name;
                            root.filter();
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.name
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 100
                    radius: 10
                    color: "#1D2230"
                    border.color: Theme.border
                    Column {
                        anchors.fill: parent
                        anchors.margins: 13
                        spacing: 7
                        Text {
                            text: qsTr("Zbuduj własny quiz")
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: qsTr("Dodaj pytania lub zaimportuj\nbibliotekę z KiP i Buzza.")
                            color: Theme.muted
                            font.pixelSize: 11
                            lineHeight: 1.3
                        }
                        Text {
                            text: "JSON  ·  PAK  ·  EDAT"
                            color: Theme.accent
                            font.pixelSize: 9
                            font.letterSpacing: 0.8
                        }
                    }
                }
                Text {
                    text: "QUESTION STUDIO   /   " + Qt.application.version
                    color: "#606B7D"
                    font.pixelSize: 9
                    font.letterSpacing: 0.6
                    Layout.topMargin: 14
                    Layout.bottomMargin: 4
                }
            }
        }
        ColumnLayout {
            Layout.preferredWidth: 410
            Layout.minimumWidth: 315
            Layout.maximumWidth: 470
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 22
                spacing: 15
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("Pytania")
                        color: Theme.text
                        font.pixelSize: 25
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                    }
                    StudioButton {
                        text: qsTr("+  Dodaj")
                        tone: "primary"
                        implicitHeight: 34
                        enabled: !studio.busy
                        onClicked: {
                            root.commitEdits();
                            studio.addQuestion();
                            root.editorTab = 0;
                        }
                    }
                }
                EditorField {
                    id: search
                    Layout.fillWidth: true
                    placeholderText: qsTr("Szukaj pytań i odpowiedzi…")
                    onTextChanged: root.filter()
                    enabled: !studio.busy
                }
                RowLayout {
                    Text {
                        text: studio.visibleCount + qsTr(" pytań") + (root.categoryFilter.length ? " · " + root.categoryFilter : "")
                        color: Theme.muted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    StudioButton {
                        text: qsTr("Wyczyść")
                        tone: "quiet"
                        implicitHeight: 24
                        visible: search.text.length > 0 || root.categoryFilter.length > 0 || root.kindFilter.length > 0
                        onClicked: {
                            search.text = "";
                            root.categoryFilter = "";
                            root.kindFilter = "";
                            root.filter();
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }
            ListView {
                id: questionList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                topMargin: 14
                bottomMargin: 18
                leftMargin: 14
                rightMargin: 14
                model: studio.questions
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    id: questionRow
                    objectName: "questionRow_" + index
                    required property string uid
                    required property string questionTitle
                    required property string categoryLabel
                    required property int level
                    required property bool hasImage
                    required property bool hasAudio
                    required property bool isValid
                    required property string sourceLabel
                    required property int index
                    width: questionList.width - 28
                    height: 116
                    property bool chosen: root.hasQuestion && root.q.id === uid
                    background: Rectangle {
                        radius: 10
                        color: questionRow.chosen ? "#28263B" : questionRow.hovered ? "#202632" : "#191E27"
                        border.color: questionRow.chosen ? "#71649E" : "#252D39"
                        Rectangle {
                            visible: questionRow.chosen
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 3
                            height: 38
                            radius: 1.5
                            color: Theme.accent
                        }
                    }
                    contentItem: ColumnLayout {
                        spacing: 9
                        RowLayout {
                            Text {
                                text: questionRow.categoryLabel
                                color: questionRow.chosen ? Theme.accent : Theme.muted
                                font.pixelSize: 10
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Text {
                                text: questionRow.hasImage ? qsTr("OBRAZ") : questionRow.hasAudio ? qsTr("AUDIO") : !questionRow.isValid ? qsTr("SZKIC") : qsTr("TEKST")
                                color: !questionRow.isValid ? "#E8B977" : Theme.muted
                                font.pixelSize: 8
                                font.letterSpacing: 0.7
                            }
                        }
                        Text {
                            text: questionRow.questionTitle.length ? questionRow.questionTitle : qsTr("Nowe pytanie")
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                        RowLayout {
                            Repeater {
                                model: 3
                                Rectangle {
                                    required property int index
                                    width: 12
                                    height: 3
                                    radius: 1.5
                                    color: index < questionRow.level ? (questionRow.chosen ? Theme.accent : "#72809A") : "#303744"
                                }
                            }
                            Text {
                                text: ["", qsTr("Łatwe"), qsTr("Średnie"), qsTr("Trudne")][questionRow.level]
                                color: Theme.muted
                                font.pixelSize: 9
                                Layout.leftMargin: 3
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            Text {
                                text: questionRow.sourceLabel
                                color: "#78849A"
                                font.pixelSize: 9
                            }
                        }
                    }
                    leftPadding: 16
                    rightPadding: 14
                    topPadding: 12
                    bottomPadding: 12
                    enabled: !studio.busy
                    onClicked: {
                        root.commitEdits();
                        studio.select(uid);
                    }
                }
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 55
                    spacing: 14
                    visible: studio.visibleCount === 0 && !studio.busy
                    Text {
                        text: studio.totalCount === 0 ? qsTr("Twój quiz zaczyna się tutaj") : qsTr("Brak pasujących pytań")
                        color: Theme.text
                        font.pixelSize: 19
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }
                    Text {
                        text: studio.totalCount === 0 ? qsTr("Dodaj pierwsze pytanie albo otwórz gotową bibliotekę. Możesz też poznać edytor na przykładowej paczce.") : qsTr("Zmień wyszukiwanie lub wybierz inną kategorię.")
                        color: Theme.muted
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        width: parent.width
                        lineHeight: 1.4
                    }
                    StudioButton {
                        visible: studio.totalCount === 0
                        text: qsTr("Otwórz przykład")
                        onClicked: root.guard(function () {
                            studio.newProject(true);
                        })
                    }
                }
            }
        }
        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: Theme.border
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 500
            Layout.preferredWidth: 650
            Layout.fillHeight: true
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 18
                Layout.bottomMargin: 12
                spacing: 3
                Repeater {
                    model: [qsTr("Edytor pytania"), qsTr("Podgląd")]
                    StudioButton {
                        required property string modelData
                        required property int index
                        text: modelData
                        tone: root.editorTab === index ? "normal" : "quiet"
                        onClicked: {
                            root.commitEdits();
                            root.editorTab = index;
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                StudioButton {
                    text: "↶"
                    implicitWidth: 34
                    tone: "quiet"
                    enabled: studio.canUndo && !studio.busy
                    onClicked: {
                        root.commitEdits();
                        studio.undo();
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Cofnij (Ctrl+Z)")
                }
                StudioButton {
                    text: "↷"
                    implicitWidth: 34
                    tone: "quiet"
                    enabled: studio.canRedo && !studio.busy
                    onClicked: {
                        root.commitEdits();
                        studio.redo();
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Ponów (Ctrl+Shift+Z)")
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }
            ScrollView {
                id: editorScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                visible: root.hasQuestion && root.editorTab === 0
                implicitWidth: 500
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AlwaysOn
                ColumnLayout {
                    width: editorScroll.availableWidth
                    spacing: 18
                    enabled: !studio.busy
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        Layout.topMargin: 22
                        Rectangle {
                            implicitWidth: readyLabel.implicitWidth + 18
                            height: 23
                            radius: 5
                            color: root.q.problems && root.q.problems.length ? "#3C3022" : "#1E352D"
                            Text {
                                id: readyLabel
                                anchors.centerIn: parent
                                text: root.q.problems && root.q.problems.length ? qsTr("DO UZUPEŁNIENIA") : qsTr("GOTOWE")
                                color: root.q.problems && root.q.problems.length ? "#E8B977" : Theme.success
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.6
                            }
                        }
                        Text {
                            text: root.q.sourceLabel || qsTr("Własne")
                            color: Theme.muted
                            font.pixelSize: 11
                            Layout.fillWidth: true
                        }
                        StudioButton {
                            text: qsTr("Duplikuj")
                            implicitHeight: 28
                            tone: "quiet"
                            onClicked: {
                                root.commitEdits();
                                studio.duplicateCurrent();
                            }
                        }
                        StudioButton {
                            text: qsTr("Usuń")
                            implicitHeight: 28
                            tone: "danger"
                            onClicked: deleteDialog.open()
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        spacing: 8
                        Text {
                            text: qsTr("TREŚĆ PYTANIA")
                            color: Theme.muted
                            font.pixelSize: 10
                            font.letterSpacing: 1
                        }
                        TextArea {
                            id: questionText
                            objectName: "questionText"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 108
                            text: root.q.question || ""
                            placeholderText: qsTr("O co chcesz zapytać?")
                            color: Theme.text
                            placeholderTextColor: Theme.muted
                            font.pixelSize: 19
                            font.weight: Font.Medium
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            padding: 14
                            background: Rectangle {
                                radius: 9
                                color: Theme.input
                                border.color: questionText.activeFocus ? Theme.accent : Theme.border
                            }
                            onActiveFocusChanged: if (!activeFocus && root.hasQuestion)
                                studio.updateCurrent({
                                    question: text
                                })
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        spacing: 14
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 7
                            Text {
                                text: qsTr("KATEGORIA QUIZ WORLD")
                                color: Theme.muted
                                font.pixelSize: 10
                                font.letterSpacing: 0.7
                            }
                            ComboBox {
                                id: subjectCombo
                                Layout.fillWidth: true
                                model: root.subjects
                                textRole: "name"
                                implicitHeight: 36
                                currentIndex: {
                                    for (let i = 0; i < root.subjects.length; i++)
                                        if (root.subjects[i].id === root.q.subject)
                                            return i;
                                    return -1;
                                }
                                displayText: currentIndex < 0 ? (root.q.category_name || "Wybierz kategorię") : currentText
                                onActivated: {
                                    const item = root.subjects[currentIndex];
                                    studio.updateCurrent({
                                        subject: item.id,
                                        category: item.sub,
                                        category_name: item.name
                                    });
                                }
                            }
                        }
                        ColumnLayout {
                            Layout.preferredWidth: 130
                            spacing: 7
                            Text {
                                text: qsTr("TRUDNOŚĆ")
                                color: Theme.muted
                                font.pixelSize: 10
                                font.letterSpacing: 0.7
                            }
                            ComboBox {
                                Layout.fillWidth: true
                                implicitHeight: 36
                                model: [qsTr("Łatwe"), qsTr("Średnie"), qsTr("Trudne")]
                                currentIndex: root.hasQuestion ? root.q.difficulty - 1 : 1
                                onActivated: studio.updateCurrent({
                                    difficulty: currentIndex + 1
                                })
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        spacing: 12
                        Text {
                            text: qsTr("Podkategoria")
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                        ComboBox {
                            id: subCombo
                            Layout.fillWidth: true
                            implicitHeight: 30
                            model: root.subcategoryItems(root.q.subject)
                            textRole: "name"
                            currentIndex: {
                                for (let i = 0; i < model.length; i++)
                                    if (model[i].id === root.q.category)
                                        return i;
                                return -1;
                            }
                            displayText: currentIndex < 0 ? "Zachowaj kategorię źródłową" : currentText
                            onActivated: studio.updateCurrent({
                                category: model[currentIndex].id
                            })
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        spacing: 8
                        RowLayout {
                            Text {
                                text: qsTr("ODPOWIEDZI")
                                color: Theme.muted
                                font.pixelSize: 10
                                font.letterSpacing: 1
                                Layout.fillWidth: true
                            }
                            Text {
                                text: qsTr("Zaznacz poprawną")
                                color: Theme.muted
                                font.pixelSize: 10
                            }
                        }
                        Repeater {
                            model: 4
                            Rectangle {
                                id: answerRow
                                required property int index
                                Layout.fillWidth: true
                                implicitHeight: 49
                                radius: 8
                                color: Theme.panel
                                border.color: root.q.correct_index === index ? "#638B76" : Theme.border
                                Rectangle {
                                    width: 4
                                    height: parent.height - 18
                                    anchors.left: parent.left
                                    anchors.leftMargin: 1
                                    anchors.verticalCenter: parent.verticalCenter
                                    radius: 2
                                    color: Theme.answers[answerRow.index]
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 10
                                    spacing: 10
                                    Text {
                                        text: String.fromCharCode(65 + answerRow.index)
                                        color: Theme.answers[answerRow.index]
                                        font.pixelSize: 12
                                        font.bold: true
                                    }
                                    TextField {
                                        Layout.fillWidth: true
                                        text: root.q.answers ? root.q.answers[answerRow.index] : ""
                                        color: Theme.text
                                        placeholderText: qsTr("Wpisz odpowiedź")
                                        placeholderTextColor: Theme.muted
                                        selectByMouse: true
                                        font.pixelSize: 14
                                        background: Item {}
                                        onEditingFinished: studio.updateCurrent({
                                            answer_index: answerRow.index,
                                            answer_text: text
                                        })
                                    }
                                    RadioButton {
                                        checked: root.q.correct_index === answerRow.index
                                        onClicked: studio.updateCurrent({
                                            correct_index: answerRow.index
                                        })
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Poprawna odpowiedź")
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        height: 1
                        color: Theme.border
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        spacing: 10
                        Text {
                            text: qsTr("MEDIA")
                            color: Theme.muted
                            font.pixelSize: 10
                            font.letterSpacing: 1
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Rectangle {
                                Layout.preferredWidth: 100
                                Layout.preferredHeight: 68
                                radius: 7
                                color: Theme.panel
                                border.color: Theme.border
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 3
                                    source: root.q.imageUrl || ""
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }
                                Text {
                                    visible: !root.q.image
                                    anchors.centerIn: parent
                                    text: qsTr("+ obrazek")
                                    color: Theme.muted
                                    font.pixelSize: 11
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: imageDialog.open()
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Text {
                                    text: root.q.image ? String(root.q.image).split(/[\\/]/).pop() : qsTr("Pytanie tekstowe")
                                    color: Theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: qsTr("Obraz zachowuje swoje proporcje")
                                    color: Theme.muted
                                    font.pixelSize: 10
                                }
                                RowLayout {
                                    StudioButton {
                                        text: root.q.image ? qsTr("Zmień obrazek") : qsTr("Dodaj obrazek")
                                        implicitHeight: 28
                                        onClicked: imageDialog.open()
                                    }
                                    StudioButton {
                                        visible: !!root.q.image
                                        text: qsTr("Usuń")
                                        tone: "quiet"
                                        implicitHeight: 28
                                        onClicked: studio.updateCurrent({
                                            image: ""
                                        })
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: root.q.speech ? String(root.q.speech).split(/[\\/]/).pop() : qsTr("Brak nagrania lektora")
                                color: Theme.muted
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            StudioButton {
                                text: qsTr("Generuj głos")
                                implicitHeight: 29
                                onClicked: {
                                    root.commitEdits();
                                    ttsDialog.open();
                                }
                            }
                            StudioButton {
                                text: qsTr("Dodaj audio")
                                implicitHeight: 29
                                onClicked: audioDialog.open()
                            }
                            StudioButton {
                                text: qsTr("Otwórz")
                                implicitHeight: 29
                                visible: !!root.q.speech
                                onClicked: studio.openAudio()
                            }
                            StudioButton {
                                text: "×"
                                implicitHeight: 29
                                visible: !!root.q.speech
                                tone: "quiet"
                                onClicked: studio.updateCurrent({
                                    speech: ""
                                })
                            }
                        }
                        Text {
                            visible: root.q.source_media !== undefined && root.q.source_media.length > 0
                            text: qsTr("Źródło zawiera media Buzza. Zachowano ich oryginalne odwołania.")
                            color: Theme.muted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    Rectangle {
                        visible: root.q.problems && root.q.problems.length > 0
                        Layout.fillWidth: true
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        implicitHeight: issueText.implicitHeight + 24
                        radius: 8
                        color: "#30291F"
                        Text {
                            id: issueText
                            anchors.fill: parent
                            anchors.margins: 12
                            text: root.q.problems ? root.q.problems.join("\n") : ""
                            color: "#E7BC85"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                    Text {
                        Layout.leftMargin: 26
                        Layout.rightMargin: 26
                        Layout.bottomMargin: 22
                        Layout.fillWidth: true
                        text: "ID  " + (root.q.id || "")
                        color: "#637087"
                        font.pixelSize: 9
                        elide: Text.ElideMiddle
                    }
                }
            }
            Item {
                visible: root.hasQuestion && root.editorTab === 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 28
                    spacing: 20
                    Text {
                        text: qsTr("TAK ZOBACZĄ JE GRACZE")
                        color: Theme.muted
                        font.pixelSize: 10
                        font.letterSpacing: 1.5
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 16
                        color: "#151B2B"
                        border.color: "#35405B"
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 24
                            spacing: 18
                            Text {
                                text: root.q.category_name || ""
                                color: Theme.accent
                                font.pixelSize: 11
                            }
                            Text {
                                text: root.q.question || qsTr("Treść pytania")
                                color: Theme.text
                                font.pixelSize: 25
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Image {
                                visible: !!root.q.image
                                source: root.q.imageUrl || ""
                                Layout.fillWidth: true
                                Layout.preferredHeight: 210
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }
                            Item {
                                Layout.fillHeight: true
                                visible: !root.q.image
                            }
                            Repeater {
                                model: 4
                                Rectangle {
                                    required property int index
                                    Layout.fillWidth: true
                                    implicitHeight: Math.max(50, previewAnswer.implicitHeight + 24)
                                    radius: 8
                                    color: Qt.darker(Theme.answers[index], 3.6)
                                    border.color: Theme.answers[index]
                                    Text {
                                        id: previewAnswer
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.margins: 16
                                        text: root.q.answers ? root.q.answers[parent.index] : ""
                                        color: Theme.text
                                        font.pixelSize: 16
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        text: qsTr("Podgląd układu pytania. Poprawna odpowiedź jest ukryta.")
                        color: Theme.muted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
            Item {
                visible: !root.hasQuestion
                Layout.fillWidth: true
                Layout.fillHeight: true
                Column {
                    anchors.centerIn: parent
                    width: 330
                    spacing: 16
                    Image {
                        source: "qrc:/studio.svg"
                        width: 56
                        height: 56
                        opacity: 0.75
                    }
                    Text {
                        text: qsTr("Miejsce na dobre pytania")
                        color: Theme.text
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }
                    Text {
                        text: qsTr("Wybierz pytanie z biblioteki, żeby zmienić treść, odpowiedzi i media. Wszystko pozostaje na Twoim komputerze.")
                        color: Theme.muted
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                        width: parent.width
                        lineHeight: 1.5
                    }
                }
            }
            Rectangle {
                visible: studio.importWarnings.length > 0
                Layout.fillWidth: true
                implicitHeight: 44
                color: "#30291F"
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Uwagi importu (") + studio.importWarnings.length + ")"
                    color: "#E7BC85"
                    font.pixelSize: 11
                }
                StudioButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Pokaż")
                    implicitHeight: 28
                    tone: "quiet"
                    onClicked: warningsDialog.open()
                }
            }
        }
    }
    Rectangle {
        visible: studio.busy
        anchors.fill: parent
        color: "#C0101318"
        z: 20
        MouseArea {
            anchors.fill: parent
        }
        Column {
            anchors.centerIn: parent
            width: Math.min(560, parent.width - 60)
            spacing: 15
            BusyIndicator {
                running: studio.busy
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: studio.status
                color: Theme.text
                font.pixelSize: 18
                width: parent.width
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
            ProgressBar {
                visible: studio.buildActive
                width: parent.width
                from: 0
                to: 100
                value: studio.buildProgress
            }
            StudioButton {
                visible: studio.buildActive
                text: qsTr("Anuluj")
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: studio.cancelBuild()
            }
        }
    }
    NativeDialogs.FileDialog {
        id: openDialog
        title: qsTr("Otwórz bibliotekę")
        nameFilters: [qsTr("Pytania (*.buzz.json *.json *.PAK *.pak *.edat)")]
        onAccepted: studio.prepareImport(selectedFile)
    }
    NativeDialogs.FileDialog {
        id: importDialog
        title: qsTr("Importuj pytania do bieżącej paczki")
        nameFilters: [qsTr("Pytania (*.json *.PAK *.pak *.edat)")]
        onAccepted: studio.prepareImport(selectedFile, true)
    }
    NativeDialogs.FileDialog {
        id: saveDialog
        title: qsTr("Zapisz projekt")
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: "buzz.json"
        nameFilters: [qsTr("Projekt Question Studio (*.buzz.json)")]
        onAccepted: {
            if (studio.saveFile(selectedFile)) {
                if (root.afterSave)
                    root.afterSave();
                root.afterSave = null;
            }
        }
        onRejected: root.afterSave = null
    }
    NativeDialogs.FileDialog {
        id: exportDialog
        title: qsTr("Eksport JSON dla buzzpack")
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON (*.json)")]
        onAccepted: studio.saveFile(selectedFile, true)
    }
    NativeDialogs.FileDialog {
        id: imageDialog
        title: qsTr("Wybierz obrazek")
        nameFilters: [qsTr("Obrazki (*.png *.jpg *.jpeg *.webp *.bmp *.svg)")]
        onAccepted: studio.updateCurrent({
            image: selectedFile.toString()
        })
    }
    NativeDialogs.FileDialog {
        id: audioDialog
        title: qsTr("Wybierz nagranie")
        nameFilters: [qsTr("Audio (*.wav *.mp3 *.ogg *.flac *.rsf *.wem)")]
        onAccepted: studio.updateCurrent({
            speech: selectedFile.toString()
        })
    }
    NativeDialogs.FileDialog {
        id: ttsSaveDialog
        title: qsTr("Zapisz wygenerowane nagranie")
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: "wav"
        nameFilters: [qsTr("Nagranie WAV (*.wav)")]
        onAccepted: studio.generateAudio(selectedFile, ttsText.text, voiceCombo.currentValue || "", Math.round(ttsRate.value))
    }
    Dialog {
        id: importLanguageDialog
        objectName: "importLanguageDialog"
        anchors.centerIn: parent
        modal: true
        title: qsTr("Wybierz język importu")
        width: 500
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            width: parent.width
            spacing: 14
            Label {
                text: qsTr("Języki wykryte w wybranym źródle:")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ComboBox {
                id: importLanguageCombo
                objectName: "importLanguageCombo"
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "code"
            }
            Label {
                text: qsTr("Pytania bez wybranego języka zostaną pominięte.")
                wrapMode: Text.WordWrap
                color: Theme.muted
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("Import JSON zachowuje także pozostałe tłumaczenia. W paczkach Buzza wczytany zostanie wybrany język; kolejne można zaimportować później.")
                wrapMode: Text.WordWrap
                color: Theme.muted
                Layout.fillWidth: true
            }
        }
        onAccepted: studio.finishImport(importLanguageCombo.currentValue)
        onRejected: studio.cancelImport()
    }
    Dialog {
        id: projectLanguageDialog
        objectName: "projectLanguageDialog"
        anchors.centerIn: parent
        modal: true
        title: qsTr("Języki paczki")
        width: 520
        standardButtons: Dialog.Close
        ColumnLayout {
            width: parent.width
            spacing: 14
            Label {
                text: qsTr("Edytowana wersja pytań")
                color: Theme.muted
            }
            ComboBox {
                id: projectLanguageCombo
                objectName: "projectLanguageCombo"
                Layout.fillWidth: true
                model: studio.projectLanguages
                textRole: "name"
                valueRole: "code"
                currentIndex: {
                    const choices = studio.projectLanguages;
                    for (let i = 0; i < choices.length; ++i)
                        if (choices[i].code === studio.packLanguage)
                            return i;
                    return -1;
                }
                onActivated: studio.setPackLanguage(currentValue)
            }
            Label {
                text: qsTr("Każdy język ma własną treść, cztery odpowiedzi i nagranie. Brakujące tłumaczenia pozostają szkicami. Zmiana języka interfejsu nie zmienia pytań.")
                wrapMode: Text.WordWrap
                color: Theme.muted
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: addLanguageCombo
                    Layout.fillWidth: true
                    model: studio.languageOptions
                    textRole: "name"
                    valueRole: "code"
                }
                StudioButton {
                    text: qsTr("Dodaj / wybierz")
                    onClicked: studio.setPackLanguage(addLanguageCombo.currentValue)
                }
            }
            EditorField {
                Layout.fillWidth: true
                placeholderText: qsTr("Inny kod języka, np. en-GB — Enter")
                onAccepted: {
                    studio.setPackLanguage(text);
                    text = "";
                }
            }
            Label {
                text: qsTr("Eksport dla buzzpack zapisuje tylko aktualnie wybrany język. Projekt .buzz.json zachowuje wszystkie wersje.")
                wrapMode: Text.WordWrap
                color: Theme.muted
                Layout.fillWidth: true
            }
        }
    }
    Dialog {
        id: ttsDialog
        objectName: "ttsDialog"
        anchors.centerIn: parent
        modal: true
        title: qsTr("Lektor pytania")
        width: 560
        onOpened: {
            ttsText.text = root.q.tts_text || root.q.question || "";
            for (let i = 0; i < studio.ttsVoices.length; ++i) {
                if (studio.ttsVoices[i].language.split("-")[0] === studio.packLanguage.split("-")[0]) {
                    voiceCombo.currentIndex = i;
                    break;
                }
            }
        }
        ColumnLayout {
            width: parent.width
            spacing: 14
            Label {
                text: Qt.platform.os === "windows" ? qsTr("Głosy zainstalowane w Windows · generowanie lokalne") : qsTr("eSpeak NG · generowanie lokalne")
                color: Theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ComboBox {
                id: voiceCombo
                Layout.fillWidth: true
                model: studio.ttsVoices
                textRole: "name"
                valueRole: "id"
            }
            Label {
                visible: voiceCombo.count === 0
                text: Qt.platform.os === "windows" ? qsTr("Nie znaleziono głosów SAPI. Dodaj głos systemowy w ustawieniach Windows.") : qsTr("Zainstaluj pakiet espeak-ng i uruchom edytor ponownie.")
                color: "#E7BC85"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("Tekst lektora — możesz zmienić zapis wymowy")
                color: Theme.muted
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                TextArea {
                    id: ttsText
                    objectName: "ttsText"
                    color: Theme.text
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    padding: 12
                    background: Rectangle {
                        color: Theme.input
                        radius: 8
                        border.color: Theme.border
                    }
                }
            }
            RowLayout {
                Label {
                    text: qsTr("Wolniej")
                    color: Theme.muted
                }
                Slider {
                    id: ttsRate
                    Layout.fillWidth: true
                    from: -5
                    to: 5
                    value: 0
                    stepSize: 1
                }
                Label {
                    text: qsTr("Szybciej")
                    color: Theme.muted
                }
            }
            Label {
                text: qsTr("Nagranie zostanie zapisane jako WAV i przypisane do pytania. Zmiana treści pytania później wymaga ponownego wygenerowania głosu.")
                color: Theme.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                StudioButton {
                    text: qsTr("Anuluj")
                    tone: "quiet"
                    onClicked: ttsDialog.close()
                }
                StudioButton {
                    text: qsTr("Generuj WAV…")
                    tone: "primary"
                    enabled: voiceCombo.count > 0 && ttsText.text.trim().length > 0
                    onClicked: {
                        ttsDialog.close();
                        ttsSaveDialog.open();
                    }
                }
            }
        }
    }
    Dialog {
        id: unsaved
        anchors.centerIn: parent
        modal: true
        title: qsTr("Zapisać zmiany?")
        width: 440
        ColumnLayout {
            anchors.fill: parent
            spacing: 20
            Label {
                text: qsTr("Bieżąca paczka zawiera niezapisane zmiany.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                StudioButton {
                    text: qsTr("Anuluj")
                    onClicked: {
                        unsaved.close();
                        root.pendingAction = null;
                    }
                }
                StudioButton {
                    text: qsTr("Pomiń")
                    onClicked: {
                        unsaved.close();
                        const action = root.pendingAction;
                        root.pendingAction = null;
                        if (action)
                            action();
                    }
                }
                StudioButton {
                    text: qsTr("Zapisz")
                    tone: "primary"
                    onClicked: {
                        unsaved.close();
                        const action = root.pendingAction;
                        root.pendingAction = null;
                        root.save(action);
                    }
                }
            }
        }
    }
    Dialog {
        id: errorDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Nie udało się wykonać operacji")
        width: 480
        standardButtons: Dialog.Ok
        Label {
            id: errorText
            width: parent.width
            wrapMode: Text.WordWrap
        }
    }
    Dialog {
        id: warningsDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Uwagi importu")
        width: 530
        standardButtons: Dialog.Ok
        Label {
            width: parent.width
            text: studio.importWarnings.join("\n\n")
            wrapMode: Text.WordWrap
        }
    }
    Dialog {
        id: deleteDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Usunąć pytanie?")
        width: 400
        standardButtons: Dialog.Yes | Dialog.Cancel
        Label {
            width: parent.width
            text: qsTr("Pytanie zostanie usunięte z tej paczki. Możesz cofnąć tę zmianę.")
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            root.commitEdits();
            studio.deleteCurrent();
        }
    }
    Dialog {
        id: packDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Nazwa paczki")
        width: 420
        standardButtons: Dialog.Save | Dialog.Cancel
        EditorField {
            id: packName
            width: parent.width
            onAccepted: packDialog.accept()
        }
        onAccepted: studio.setPackTitle(packName.text)
    }
    Dialog {
        id: aboutDialog
        anchors.centerIn: parent
        modal: true
        title: "Buzz! Question Studio " + Qt.application.version
        width: 500
        standardButtons: Dialog.Ok
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Natywny edytor pytań dla Windows i Linux.\n\nImportuje JSON, KiP JSON oraz tekst i nagrania z nieszyfrowanych paczek Quiz World / Quiz TV. Generuje WAV przez głosy Windows lub eSpeak NG. Buduje paczki EDAT z wybraną wersją językową, obrazkami i dźwiękiem oraz instaluje je z kopią zapasową.\n\nProjekt zapisuje odwołania do mediów. Przenośne archiwa projektów są kolejnym etapem.")
        }
    }
}
