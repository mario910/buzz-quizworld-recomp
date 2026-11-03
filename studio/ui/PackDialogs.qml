import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import Qt.labs.platform 1.1 as Platform

Item {
    id: panels
    anchors.fill: parent
    required property var controller
    required property var host
    property var result: ({})
    property url installSource
    readonly property var titles: ["BCES00647", "BCES00644", "BCES00645", "BCUS98209"]

    function selectTitle(combo, value) {
        combo.currentIndex = Math.max(0, titles.indexOf(value));
    }
    function options() {
        return {
            number: packNumber.value,
            categoryBase: categoryBase.value,
            templatePath: templatePath.value,
            outputDirectory: outputPath.value,
            runtimeRoot: runtimePath.value,
            titleId: titles[gameTitle.currentIndex],
            ffmpeg: ffmpegPath.value
        };
    }
    function openBuild() {
        host.commitEdits();
        const values = controller.buildSettings();
        packNumber.value = values.number;
        categoryBase.value = values.categoryBase;
        templatePath.value = values.templatePath;
        outputPath.value = values.outputDirectory;
        runtimePath.value = values.runtimeRoot;
        ffmpegPath.value = values.ffmpeg;
        selectTitle(gameTitle, values.titleId);
        buildDialog.open();
    }
    function openInstall(source) {
        host.commitEdits();
        installSource = source;
        const values = controller.buildSettings();
        installRuntime.value = values.runtimeRoot;
        selectTitle(installTitle, values.titleId);
        installDialog.open();
    }
    function chooseInstall() {
        host.commitEdits();
        installFile.open();
    }
    component PathRow: ColumnLayout {
        id: pathControl
        property string label
        property alias value: field.text
        signal browse
        spacing: 5
        Layout.fillWidth: true
        Label {
            text: parent.label
            color: Theme.muted
            font.pixelSize: 11
        }
        RowLayout {
            Layout.fillWidth: true
            EditorField {
                id: field
                Layout.fillWidth: true
            }
            StudioButton {
                text: qsTr("Wybierz…")
                onClicked: pathControl.browse()
            }
        }
    }
    Connections {
        target: panels.controller
        function onPackFinished(value) {
            panels.result = value;
            resultDialog.open();
        }
    }
    Dialog {
        id: buildDialog
        objectName: "buildPackDialog"
        anchors.centerIn: parent
        modal: true
        width: Math.min(720, panels.width - 48)
        height: Math.min(panels.height - 48, advanced.checked ? 650 : 530)
        title: qsTr("Zbuduj paczkę quizów")
        contentItem: ScrollView {
            id: buildScroll
            contentHeight: buildContent.implicitHeight
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                id: buildContent
                width: buildScroll.availableWidth
                spacing: 12
                Label {
                    text: qsTr("Język pytań: %1 · %2 pytań w projekcie").arg(panels.controller.packLanguage).arg(panels.controller.totalCount)
                    color: Theme.accent
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Zapisz paczkę dla aktywnego języka. Pytania bez tej wersji zostaną pominięte; niekompletne pytania trzeba poprawić przed budowaniem.")
                    color: Theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                PathRow {
                    id: templatePath
                    objectName: "buildTemplatePath"
                    label: qsTr("Szablon Quiz World — PACK0100 w wybranym języku")
                    onBrowse: templatePicker.open()
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Numer paczki")
                        color: Theme.muted
                    }
                    SpinBox {
                        id: packNumber
                        objectName: "buildPackNumber"
                        from: 101
                        to: 9999
                        editable: true
                        onValueModified: if (value >= 101 && value <= 105)
                            categoryBase.value = 500 + (value - 101) * 100
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Label {
                        text: qsTr("Wersja gry")
                        color: Theme.muted
                    }
                    ComboBox {
                        id: gameTitle
                        model: panels.titles
                    }
                }
                PathRow {
                    id: outputPath
                    objectName: "buildOutputPath"
                    label: qsTr("Katalog gotowych paczek")
                    onBrowse: outputPicker.open()
                }
                PathRow {
                    id: runtimePath
                    label: qsTr("Środowisko gry (runtime) — do instalacji")
                    onBrowse: runtimePicker.open()
                }
                Label {
                    text: qsTr("Instalacja zapisze paczkę w runtime i zachowa kopię poprzedniej wersji.")
                    color: Theme.muted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                CheckBox {
                    id: advanced
                    objectName: "buildAdvanced"
                    text: qsTr("Ustawienia zaawansowane")
                }
                ColumnLayout {
                    visible: advanced.checked
                    Layout.fillWidth: true
                    RowLayout {
                        Label {
                            text: qsTr("Baza kategorii")
                            color: Theme.muted
                        }
                        SpinBox {
                            id: categoryBase
                            objectName: "buildCategoryBase"
                            from: 500
                            to: 999
                            editable: true
                        }
                        Label {
                            text: qsTr("Instalator sprawdzi kolizje.")
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }
                    PathRow {
                        id: ffmpegPath
                        label: qsTr("Silnik mediów (FFmpeg)")
                        onBrowse: ffmpegPicker.open()
                    }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    StudioButton {
                        text: qsTr("Anuluj")
                        tone: "quiet"
                        onClicked: buildDialog.close()
                    }
                    StudioButton {
                        text: qsTr("Zbuduj .edat")
                        objectName: "startPackBuild"
                        enabled: templatePath.value.length > 0 && outputPath.value.length > 0 && panels.controller.totalCount > 0
                        onClicked: {
                            const value = panels.options();
                            buildDialog.close();
                            panels.controller.buildPack(value, false);
                        }
                    }
                    StudioButton {
                        text: qsTr("Zbuduj i zainstaluj")
                        tone: "primary"
                        enabled: templatePath.value.length > 0 && outputPath.value.length > 0 && runtimePath.value.length > 0 && panels.controller.totalCount > 0
                        onClicked: {
                            const value = panels.options();
                            buildDialog.close();
                            panels.controller.buildPack(value, true);
                        }
                    }
                }
            }
        }
    }
    Dialog {
        id: installDialog
        objectName: "installPackDialog"
        anchors.centerIn: parent
        width: Math.min(640, panels.width - 48)
        modal: true
        title: qsTr("Zainstaluj paczkę")
        ColumnLayout {
            width: parent.width
            spacing: 14
            Label {
                text: panels.controller.localFile(panels.installSource)
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                color: Theme.accent
                Layout.fillWidth: true
            }
            PathRow {
                id: installRuntime
                label: qsTr("Środowisko gry (runtime)")
                onBrowse: installRuntimePicker.open()
            }
            RowLayout {
                Label {
                    text: qsTr("Wersja gry")
                    color: Theme.muted
                }
                ComboBox {
                    id: installTitle
                    model: panels.titles
                }
            }
            Label {
                text: qsTr("Sprawdzimy paczkę i kolizje kategorii. Jeśli zastępujesz wcześniejszą wersję, najpierw powstanie jej kopia zapasowa. Po instalacji uruchom grę ponownie.")
                wrapMode: Text.WordWrap
                color: Theme.muted
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                StudioButton {
                    text: qsTr("Anuluj")
                    tone: "quiet"
                    onClicked: installDialog.close()
                }
                StudioButton {
                    text: qsTr("Zainstaluj")
                    tone: "primary"
                    enabled: installRuntime.value.length > 0
                    onClicked: {
                        installDialog.close();
                        panels.controller.installPack(panels.installSource, installRuntime.value, panels.titles[installTitle.currentIndex]);
                    }
                }
            }
        }
    }
    Dialog {
        id: resultDialog
        objectName: "packResultDialog"
        anchors.centerIn: parent
        modal: true
        width: Math.min(680, panels.width - 48)
        title: panels.result.installedPath ? qsTr("Paczka zainstalowana") : qsTr("Paczka zbudowana")
        ColumnLayout {
            width: parent.width
            spacing: 14
            Label {
                text: qsTr("Język: %1 · Pytania: %2 · Kategorie: %3").arg(panels.result.language || "").arg(panels.result.questions || 0).arg(panels.result.categories || 0)
                color: Theme.accent
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: (panels.result.skipped || 0) > 0
                text: qsTr("Pominięto %1 pytań bez wybranego języka.").arg(panels.result.skipped || 0)
                color: Theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TextArea {
                Layout.fillWidth: true
                implicitHeight: Math.min(220, contentHeight + 24)
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                color: Theme.text
                text: (panels.result.path || "") + (panels.result.installedPath ? "\n\n" + qsTr("Instalacja:") + "\n" + panels.result.installedPath : "") + (panels.result.backupPath ? "\n\n" + qsTr("Kopia poprzedniej wersji:") + "\n" + panels.result.backupPath : "")
            }
            Label {
                visible: !!panels.result.installedPath
                text: qsTr("Uruchom grę ponownie i wybierz paczkę w grze z pakietami quizów.")
                color: Theme.muted
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                StudioButton {
                    text: qsTr("Pokaż folder")
                    onClicked: panels.controller.revealFile(panels.controller.lastBuild.fileUrl)
                }
                StudioButton {
                    visible: !panels.result.installedPath
                    text: qsTr("Zainstaluj…")
                    onClicked: {
                        resultDialog.close();
                        panels.openInstall(panels.controller.lastBuild.fileUrl);
                    }
                }
                StudioButton {
                    text: qsTr("Gotowe")
                    tone: "primary"
                    onClicked: resultDialog.close()
                }
            }
        }
    }
    NativeDialogs.FileDialog {
        id: templatePicker
        title: qsTr("Wybierz szablon Quiz World PACK0100")
        nameFilters: ["PAK / EDAT (*.PAK *.pak *.edat)"]
        onAccepted: templatePath.value = panels.controller.localFile(selectedFile)
    }
    Platform.FolderDialog {
        id: outputPicker
        title: qsTr("Wybierz katalog gotowych paczek")
        onAccepted: outputPath.value = panels.controller.localFile(folder)
    }
    Platform.FolderDialog {
        id: runtimePicker
        title: qsTr("Wybierz środowisko gry (runtime)")
        onAccepted: runtimePath.value = panels.controller.localFile(folder)
    }
    Platform.FolderDialog {
        id: installRuntimePicker
        title: qsTr("Wybierz środowisko gry (runtime)")
        onAccepted: installRuntime.value = panels.controller.localFile(folder)
    }
    NativeDialogs.FileDialog {
        id: ffmpegPicker
        title: qsTr("Wybierz FFmpeg")
        onAccepted: ffmpegPath.value = panels.controller.localFile(selectedFile)
    }
    NativeDialogs.FileDialog {
        id: installFile
        title: qsTr("Wybierz paczkę do instalacji")
        nameFilters: ["EDAT (*.edat)"]
        onAccepted: panels.openInstall(selectedFile)
    }
}
