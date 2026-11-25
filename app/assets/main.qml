// Sail: the whole interface. The page fills the screen above one bottom bar (next to the keyboard, where the
// thumbs are): back, the address, the menu. Colours: graphite surfaces, one blue accent (#62A8FF).
import bb.cascades 1.4
import bb.system 1.2

Page {
    id: root
    attachedObjects: [
        SystemDialog {
            id: cookieAsk
            title: "Clear cookies and site data?"
            body: "Websites sign you out and forget what they saved on this phone. History and bookmarks stay."
            confirmButton.label: "Clear"
            cancelButton.label: "Cancel"
            onFinished: if (value == SystemUiResult.ConfirmButtonSelection) engine.clearSiteData()
        },
        SystemDialog {
            id: clearAsk
            title: "Clear history?"
            body: "Sail forgets every page you have visited. Bookmarks stay."
            confirmButton.label: "Clear"
            cancelButton.label: "Cancel"
            onFinished: if (value == SystemUiResult.ConfirmButtonSelection) store.clearHistory()
        }
    ]
    property bool editing: true            // the address is being typed (the app starts ready to type)
    property bool menuOpen: false
    property string listMode: ""           // a list covers the page: "tabs", "history" or "bookmarks"
    property bool tabsOpen: root.listMode != ""
    property variant sug: []               // what is offered for the address being typed (store.suggest)
    property int sugCount: 0
    property variant top: []               // start view: bookmarks and much-visited pages (store.top)
    property int topCount: 0
    property bool canPaste: false
    property int listCount: 0              // rows in the open list
    property bool marked: store.rev >= 0 && engine.url != "" && store.isBookmarked(engine.url)
    property bool overPage: engine.view == "page" && (root.editing || root.menuOpen || root.tabsOpen)

    function startEdit() {
        root.menuOpen = false;
        field.text = engine.url;
        root.setSug([]);                                         // nothing is offered until something is typed
        root.editing = true;
        root.focusField();
        field.editor.setSelection(0, field.text.length);
    }
    function endEdit() {
        if (engine.view == "start") { field.text = ""; root.focusField(); return; }   // nothing to go back to
        root.editing = false;
        field.loseFocus();
        engine.hideKeyboardBar();
        root.focusPage();
    }
    function go() {
        var t = field.text.replace(/^\s+|\s+$/g, "");
        if (t == "") return;
        root.editing = false;
        root.setSug([]);
        field.loseFocus();                                       // typing is over: the system's symbol row goes away
        engine.hideKeyboardBar();
        engine.open(t);
        root.focusPage();
    }
    function dropFocus() {                                       // the app is exiting
        field.loseFocus();
        pageWin.loseFocus();
        keySink.loseFocus();
        engine.hideKeyboardBar();
    }
    function focusField() { if (!engine.quiet) field.requestFocus(); }
    function setSug(s) { root.sugCount = 0; root.sug = s; root.sugCount = s.length; }
    function sg(i, k) { var r = i < root.sugCount ? root.sug[i] : null; return r ? r[k] : ""; }
    function tp(i, k) { var r = i < root.topCount ? root.top[i] : null; return r ? r[k] : ""; }
    function kindIcon(k) {
        return k == "search" ? "asset:///icons/search.png" : k == "go" ? "asset:///icons/world.png"
             : k == "bookmark" ? "asset:///icons/star.png" : "asset:///icons/history.png";
    }
    function refreshTop() { var s = store.top; root.topCount = 0; root.top = s; root.topCount = s.length; }
    function goTo(url) { field.text = url; root.go(); }

    // ---- lists that cover the page: tabs, history, bookmarks
    function fillTabs() {
        if (root.listMode == "") return;
        tabModel.clear();
        var rows = root.listMode == "tabs" ? engine.tabs : root.listMode == "history" ? store.history
                 : root.listMode == "downloads" ? engine.downloads : root.listMode == "settings" ? engine.settings : store.bookmarks;
        tabModel.append(rows);
        root.listCount = rows.length;
    }
    function showList(mode) {
        root.menuOpen = false;
        if (root.editing) { root.editing = false; field.loseFocus(); engine.hideKeyboardBar(); }
        root.listMode = mode;
        root.fillTabs();
    }
    function leaveTabs() {                                       // back to whatever the current tab shows
        root.listMode = "";
        if (engine.view == "start") root.startEdit();
        else { root.editing = false; root.focusPage(); }
    }
    function pickRow(i) {
        if (root.listMode == "tabs") { engine.selectTab(i); root.leaveTabs(); return; }
        if (root.listMode == "downloads") { engine.openDownload(i); return; }
        if (root.listMode == "settings") { engine.pickSetting(i); return; }
        var url = tabModel.value(i).url;
        root.listMode = "";
        root.goTo(url);
    }
    function shutRow(i) {
        if (root.listMode == "tabs") engine.closeTab(i);
        else if (root.listMode == "history") store.removeHistory(i);
        else if (root.listMode == "downloads") engine.removeDownload(i);
        else store.removeBookmark(i);
    }
    function openMenu() {
        root.canPaste = engine.canPaste();
        engine.querySelection();                                 // the answer decides whether Copy and Paste are offered
        root.menuOpen = true;
    }
    function newTab() {
        root.menuOpen = false;
        root.listMode = "";
        engine.newTab();
        root.startEdit();
    }
    function focusPage() {
        if (!engine.keysViaApp) pageWin.requestFocus();
        else { keySink.requestFocus(); engine.focusEngine(); }
    }
    // a page appeared without the address being typed here (opened from outside, or the engine restarted)
    function pageShown() {
        if (engine.view != "page") return;
        if (root.editing && field.text == "") root.editing = false;
        if (!root.editing) root.focusPage();                     // the page window can take the keys only once it shows
    }
    onOverPageChanged: {
        engine.setOverlay(root.overPage);
        if (!root.overPage && engine.view == "page") root.focusPage();   // however the menu or a list was left, keys go to the page again
    }
    onCreationCompleted: {
        engine.viewChanged.connect(root.pageShown);
        engine.leaving.connect(root.dropFocus);
        engine.tabsChanged.connect(root.fillTabs);
        store.historyChanged.connect(root.fillTabs);
        store.bookmarksChanged.connect(root.fillTabs);
        engine.downloadsChanged.connect(root.fillTabs);
        engine.settingsChanged.connect(root.fillTabs);
        store.topChanged.connect(root.refreshTop);
        root.refreshTop();
        if (engine.uiTest != "") {
            if (engine.uiTest == "tabs" || engine.uiTest == "history" || engine.uiTest == "bookmarks" || engine.uiTest == "downloads" || engine.uiTest == "settings") { root.editing = false; root.showList(engine.uiTest); return; }
            if (engine.uiTest == "suggest") { root.focusField(); field.text = "bbc"; return; }
            if (engine.uiTest == "start") { root.focusField(); return; }
                               // design check, see engine.cpp
            root.editing = engine.uiTest == "edit";
            if (engine.uiTest == "menu") root.openMenu();
            if (root.editing) root.startEdit();
            return;
        }
        root.focusField();
    }

    Container {
        layout: DockLayout {}
        background: Color.create("#0F1113")
        // sliding a finger over the keyboard scrolls the page
        eventHandlers: [
            TouchKeyboardHandler {
                onTouch: {
                    if (engine.view != "page" || root.editing || root.menuOpen || root.tabsOpen) {
                        if (event.touchType == TouchType.Down) engine.note("keyboard slide ignored: view " + engine.view + " editing " + root.editing + " menu " + root.menuOpen + " list " + root.listMode);
                        return;
                    }
                    var ph = event.touchType == TouchType.Down ? 0 : event.touchType == TouchType.Move ? 1 : 2;
                    engine.keyboardTouch(ph, event.screenX, event.screenY, event.fingerId);
                }
            }
        ]

        // ---- the page area --------------------------------------------------------------------------------
        // It hangs from the bar's top edge. If the system makes the app shorter (its symbol row, while a page's
        // text field is typed in), the bar stays in view and the page slides up instead of covering it.
        Container {
        layout: DockLayout {}
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        bottomPadding: engine.barHeight
        Container {
            id: area
            layout: DockLayout {}
            verticalAlignment: VerticalAlignment.Bottom
            horizontalAlignment: HorizontalAlignment.Fill
            preferredHeight: engine.pageHeight; minHeight: engine.pageHeight; maxHeight: engine.pageHeight

            // the engine's window
            ForeignWindowControl {
                id: pageWin
                objectName: "page"
                windowId: "sailPage"
                updatedProperties: WindowProperty.Position | WindowProperty.Size | WindowProperty.Visible
                visible: boundToWindow && engine.view == "page"
                preferredWidth: engine.pageWidth; preferredHeight: engine.pageHeight
                focusPolicy: engine.keysViaApp ? FocusPolicy.None : FocusPolicy.Key
                keyInputForwardingEnabled: !engine.keysViaApp
            }
            // takes the keys instead of the page window when they are forwarded by the app (test switch)
            Container {
                id: keySink
                focusPolicy: FocusPolicy.Key
                onFocusedChanged: engine.note("key sink focused " + focused)
                preferredWidth: 1; preferredHeight: 1
                keyListeners: [
                    KeyListener {
                        onKeyPressed: engine.key(event.key, event.shiftPressed, event.ctrlPressed, event.altPressed, true)
                        onKeyReleased: engine.key(event.key, event.shiftPressed, event.ctrlPressed, event.altPressed, false)
                    }
                ]
            }

            // start: nothing loaded yet. Bookmarks and much-visited pages are one tap away.
            Container {
                visible: engine.view == "start"
                verticalAlignment: VerticalAlignment.Top
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: 112
                Container {
                    leftPadding: 64; rightPadding: 240
                    Label {
                        text: "Sail"
                        textStyle { base: SystemDefaults.TextStyles.BigText; fontWeight: FontWeight.W300; fontSize: FontSize.PointValue; fontSizeValue: 30; color: Color.create("#ECEEF1") }
                    }
                    Label {
                        topMargin: 16
                        multiline: true
                        text: "Type an address or a search, then press Enter."
                        textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#9BA2AC") }
                    }
                }
                Container {
                    topMargin: 56
                    visible: root.sugCount == 0
                    horizontalAlignment: HorizontalAlignment.Fill
                    ItemRow { inset: 64; visible: root.topCount > 0; icon: root.kindIcon(root.tp(0, "kind")); title: root.tp(0, "title"); sub: root.tp(0, "host"); onClicked: root.goTo(root.tp(0, "url")) }
                    ItemRow { inset: 64; visible: root.topCount > 1; icon: root.kindIcon(root.tp(1, "kind")); title: root.tp(1, "title"); sub: root.tp(1, "host"); onClicked: root.goTo(root.tp(1, "url")) }
                    ItemRow { inset: 64; visible: root.topCount > 2; icon: root.kindIcon(root.tp(2, "kind")); title: root.tp(2, "title"); sub: root.tp(2, "host"); onClicked: root.goTo(root.tp(2, "url")) }
                    ItemRow { inset: 64; visible: root.topCount > 3; icon: root.kindIcon(root.tp(3, "kind")); title: root.tp(3, "title"); sub: root.tp(3, "host"); onClicked: root.goTo(root.tp(3, "url")) }
                    ItemRow { inset: 64; visible: root.topCount > 4; icon: root.kindIcon(root.tp(4, "kind")); title: root.tp(4, "title"); sub: root.tp(4, "host"); onClicked: root.goTo(root.tp(4, "url")) }
                }
            }

            // error: the page did not load
            Container {
                visible: engine.view == "error"
                verticalAlignment: VerticalAlignment.Center
                leftPadding: 64; rightPadding: 240; bottomPadding: 120
                ImageView {
                    imageSource: engine.errorKind == "offline" ? "asset:///icons/offline.png" : engine.errorKind == "cert" ? "asset:///icons/shield.png" : "asset:///icons/alert.png"
                    preferredWidth: 88; preferredHeight: 88
                    opacity: 0.8
                }
                Label {
                    topMargin: 40
                    multiline: true
                    text: engine.errorTitle
                    textStyle { base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.W500; color: Color.create("#ECEEF1") }
                }
                Label {
                    topMargin: 16
                    multiline: true
                    text: engine.errorText
                    textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#9BA2AC") }
                }
                PillButton {
                    topMargin: 56
                    text: engine.errorRetry ? "Try again" : "Go back"
                    onClicked: { if (engine.errorRetry) engine.reload(); else if (engine.canBack) engine.back(); else { engine.home(); root.startEdit(); } }
                }
                Label {
                    topMargin: 40
                    text: engine.errorCode
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.create("#6F7680") }
                }
            }

            // stopped: the engine died
            Container {
                visible: engine.view == "stopped"
                verticalAlignment: VerticalAlignment.Center
                leftPadding: 64; rightPadding: 240; bottomPadding: 120
                ImageView {
                    imageSource: "asset:///icons/alert.png"
                    preferredWidth: 88; preferredHeight: 88
                    opacity: 0.8
                }
                Label {
                    topMargin: 40
                    multiline: true
                    text: "The page stopped working"
                    textStyle { base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.W500; color: Color.create("#ECEEF1") }
                }
                Label {
                    topMargin: 16
                    multiline: true
                    text: "Sail's engine closed unexpectedly. Reopening the page starts it again."
                    textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#9BA2AC") }
                }
                PillButton {
                    topMargin: 56
                    text: "Reopen page"
                    onClicked: engine.restart()
                }
            }

            // dims the page while the address is edited or the menu is open; a tap on it puts things back
            Container {
                visible: root.overPage
                opacity: root.overPage ? 1.0 : 0.0
                background: Color.create("#99000000")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                gestureHandlers: [ TapHandler { onTapped: { if (root.menuOpen) { root.menuOpen = false; root.focusPage(); } else root.endEdit(); } } ]
            }

            // while an address is typed: what Enter will do, then matches from bookmarks and history
            Container {
                visible: root.editing && root.sugCount > 0
                verticalAlignment: VerticalAlignment.Bottom
                horizontalAlignment: HorizontalAlignment.Fill
                background: Color.create("#15171A")
                Container { horizontalAlignment: HorizontalAlignment.Fill; preferredHeight: 2; minHeight: 2; background: Color.create("#2E3238") }
                ItemRow { visible: root.sugCount > 4; icon: root.kindIcon(root.sg(4, "kind")); title: root.sg(4, "title"); sub: root.sg(4, "host"); onClicked: root.goTo(root.sg(4, "url")) }
                ItemRow { visible: root.sugCount > 3; icon: root.kindIcon(root.sg(3, "kind")); title: root.sg(3, "title"); sub: root.sg(3, "host"); onClicked: root.goTo(root.sg(3, "url")) }
                ItemRow { visible: root.sugCount > 2; icon: root.kindIcon(root.sg(2, "kind")); title: root.sg(2, "title"); sub: root.sg(2, "host"); onClicked: root.goTo(root.sg(2, "url")) }
                ItemRow { visible: root.sugCount > 1; icon: root.kindIcon(root.sg(1, "kind")); title: root.sg(1, "title"); sub: root.sg(1, "host"); onClicked: root.goTo(root.sg(1, "url")) }
                ItemRow { visible: root.sugCount > 0; icon: root.kindIcon(root.sg(0, "kind")); title: root.sg(0, "title"); sub: root.sg(0, "host"); onClicked: root.goTo(root.sg(0, "url")) }
            }

            // a list that covers the page: the tabs (the current one marked), history, or bookmarks
            Container {
                visible: root.tabsOpen
                background: Color.create("#0F1113")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                topPadding: 56
                Container {
                    leftPadding: 64
                    Label {
                        text: root.listMode == "history" ? "History" : root.listMode == "bookmarks" ? "Bookmarks" : root.listMode == "downloads" ? "Downloads" : root.listMode == "settings" ? "Settings" : engine.tabCount == 1 ? "1 tab" : engine.tabCount + " tabs"
                        textStyle { base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.W500; color: Color.create("#ECEEF1") }
                    }
                    // nothing to list yet: say how it fills
                    Label {
                        topMargin: 24
                        multiline: true
                        visible: root.listMode != "" && root.listMode != "tabs" && root.listCount == 0
                        text: root.listMode == "history" ? "Pages you visit are listed here."
                            : root.listMode == "downloads" ? "Files you download are saved in the phone's Downloads folder and listed here."
                            : "To keep a page, open the menu on it and choose Add bookmark."
                        textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#9BA2AC") }
                    }
                }
                ListView {
                    id: tabList
                    topMargin: 32
                    horizontalAlignment: HorizontalAlignment.Fill
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    dataModel: ArrayDataModel { id: tabModel }
                    function pick(i) { root.pickRow(i); }
                    function shut(i) { root.shutRow(i); }
                    listItemComponents: [
                        ListItemComponent {
                            type: ""
                            Container {
                                id: item
                                property bool down: false
                                layout: DockLayout {}
                                horizontalAlignment: HorizontalAlignment.Fill
                                preferredHeight: 156; minHeight: 156
                                background: item.down ? Color.create("#2E3238") : Color.Transparent
                                Container {                       // the current tab's marker
                                    visible: ListItemData.current
                                    horizontalAlignment: HorizontalAlignment.Left
                                    verticalAlignment: VerticalAlignment.Center
                                    preferredWidth: 8; preferredHeight: 84
                                    background: Color.create("#62A8FF")
                                }
                                Container {
                                    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    verticalAlignment: VerticalAlignment.Fill
                                    leftPadding: 64; rightPadding: 24
                                    Container {
                                        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                        verticalAlignment: VerticalAlignment.Center
                                        accessibility.name: ListItemData.title
                                        onTouch: {
                                            if (event.isDown()) item.down = true;
                                            else if (event.isUp() || event.isCancel()) item.down = false;
                                        }
                                        gestureHandlers: [ TapHandler { onTapped: item.ListItem.view.pick(item.ListItem.indexPath[0]) } ]
                                        Label {
                                            text: ListItemData.title
                                            textStyle { base: SystemDefaults.TextStyles.BodyText; fontWeight: ListItemData.current ? FontWeight.W500 : FontWeight.Normal; color: Color.create("#ECEEF1") }
                                        }
                                        Label {
                                            topMargin: 4
                                            visible: ListItemData.host != ""
                                            text: ListItemData.host
                                            textStyle { base: SystemDefaults.TextStyles.SubtitleText; color: Color.create("#9BA2AC") }
                                        }
                                    }
                                    BarButton {
                                        visible: !ListItemData.plain
                                        icon: "asset:///icons/stop.png"; name: "Remove"
                                        iconSize: 48
                                        onClicked: item.ListItem.view.shut(item.ListItem.indexPath[0])
                                    }
                                }
                            }
                        }
                    ]
                }
            }

            // the menu: rises from the bar, under the button that opened it
            Container {
                id: menu
                visible: root.menuOpen
                opacity: root.menuOpen ? 1.0 : 0.0
                translationY: root.menuOpen ? 0 : 48
                layout: DockLayout {}
                horizontalAlignment: HorizontalAlignment.Right
                verticalAlignment: VerticalAlignment.Bottom
                rightMargin: 0
                preferredWidth: 620
                bottomPadding: 16; rightPadding: 16
                Container {
                    layout: DockLayout {}
                    horizontalAlignment: HorizontalAlignment.Fill
                    ImageView {
                        imageSource: "asset:///shapes/panel.amd"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                    }
                    Container {
                        horizontalAlignment: HorizontalAlignment.Fill
                        topPadding: 16; bottomPadding: 16; leftPadding: 2; rightPadding: 2
                        MenuRow { visible: engine.selLength > 0; icon: "asset:///icons/copy.png"; text: "Copy selected text"; onClicked: { root.menuOpen = false; engine.copySelection(); root.focusPage(); } }
                        MenuRow { visible: engine.selEditable && root.canPaste; icon: "asset:///icons/paste.png"; text: "Paste"; onClicked: { root.menuOpen = false; engine.paste(); root.focusPage(); } }
                        MenuRow { icon: "asset:///icons/plus.png"; text: "New tab"; onClicked: root.newTab() }
                        MenuRow {
                            icon: root.marked ? "asset:///icons/unstar.png" : "asset:///icons/star.png"
                            text: root.marked ? "Remove bookmark" : "Add bookmark"
                            active: engine.url != "" && engine.view == "page"
                            onClicked: { root.menuOpen = false; store.toggleBookmark(engine.url, engine.title); root.focusPage(); }
                        }
                        MenuRow { icon: "asset:///icons/bookmarks.png"; text: "Bookmarks"; onClicked: root.showList("bookmarks") }
                        MenuRow { icon: "asset:///icons/history.png"; text: "History"; onClicked: root.showList("history") }
                        MenuRow { icon: "asset:///icons/download.png"; text: "Downloads"; onClicked: root.showList("downloads") }
                        MenuRow { icon: "asset:///icons/settings.png"; text: "Settings"; onClicked: root.showList("settings") }
                        MenuRow { icon: "asset:///icons/share.png"; text: "Share link"; active: engine.url != ""; onClicked: { root.menuOpen = false; engine.share(); } }
                        MenuRow { icon: "asset:///icons/link.png"; text: "Copy link"; active: engine.url != ""; onClicked: { root.menuOpen = false; engine.copyLink(); } }
                    }
                }
            }
        }

        }

        // ---- the bar ------------------------------------------------------------------------------------------
        Container {
            id: bar
            layout: DockLayout {}
            verticalAlignment: VerticalAlignment.Bottom
            horizontalAlignment: HorizontalAlignment.Fill
            preferredHeight: engine.barHeight; minHeight: engine.barHeight; maxHeight: engine.barHeight
            background: Color.create("#15171A")

            Container {
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                leftPadding: root.editing || root.tabsOpen ? 24 : 8; rightPadding: root.editing || root.tabsOpen ? 24 : 8

                // while the tab list is open the bar holds its actions
                BarAction { visible: root.listMode == "tabs"; icon: "asset:///icons/plus.png"; text: "New tab"; onClicked: root.newTab() }
                BarAction { visible: root.listMode == "history" && root.listCount > 0; icon: "asset:///icons/trash.png"; text: "Clear history"; onClicked: clearAsk.show() }
                BarAction { visible: root.listMode == "settings"; icon: "asset:///icons/trash.png"; text: "Clear cookies"; onClicked: cookieAsk.show() }
                Container { visible: root.tabsOpen; layoutProperties: StackLayoutProperties { spaceQuota: 1 } }
                BarAction { visible: root.tabsOpen; accent: true; text: "Done"; onClicked: root.leaveTabs() }

                BarButton {
                    visible: !root.editing && !root.tabsOpen
                    icon: "asset:///icons/back.png"; name: "Back"
                    active: engine.canBack || engine.view == "error"
                    onClicked: { if (engine.canBack) engine.back(); else { engine.home(); root.startEdit(); } }
                }
                BarButton {
                    visible: !root.editing && !root.tabsOpen && engine.canForward
                    icon: "asset:///icons/forward.png"; name: "Forward"
                    onClicked: engine.forward()
                }

                // the address
                Container {
                    visible: !root.tabsOpen
                    layout: DockLayout {}
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    verticalAlignment: VerticalAlignment.Center
                    preferredHeight: 100; minHeight: 100; maxHeight: 100
                    leftMargin: 8; rightMargin: 8
                    ImageView {
                        imageSource: root.editing ? "asset:///shapes/field_edit.amd" : "asset:///shapes/field.amd"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                    }
                    // shown: where you are
                    Container {
                        visible: !root.editing
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        leftPadding: 36
                        Container {
                            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            verticalAlignment: VerticalAlignment.Fill
                            accessibility.name: "Address, " + engine.host
                            gestureHandlers: [ TapHandler { onTapped: root.startEdit() } ]
                            ImageView {
                                imageSource: engine.url == "" ? "asset:///icons/search.png" : engine.view == "error" ? (engine.errorKind == "cert" ? "asset:///icons/warn.png" : "asset:///icons/alert.png") : engine.secure ? "asset:///icons/lock.png" : "asset:///icons/warn.png"
                                preferredWidth: 40; preferredHeight: 40
                                verticalAlignment: VerticalAlignment.Center
                                scalingMethod: ScalingMethod.AspectFit
                                opacity: engine.url != "" && (!engine.secure || engine.errorKind == "cert" && engine.view == "error") ? 1.0 : 0.6
                            }
                            Label {
                                leftMargin: 20
                                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                                verticalAlignment: VerticalAlignment.Center
                                text: engine.url == "" ? "Search or type an address" : engine.host
                                textStyle { base: SystemDefaults.TextStyles.BodyText; color: engine.url == "" ? Color.create("#9BA2AC") : Color.create("#ECEEF1") }
                            }
                        }
                        BarButton {
                            preferredWidth: 100; minWidth: 100; iconSize: 44
                            visible: engine.url != ""
                            icon: engine.loading ? "asset:///icons/stop.png" : "asset:///icons/reload.png"
                            name: engine.loading ? "Stop loading" : "Reload"
                            onClicked: { if (engine.loading) engine.stop(); else engine.reload(); }
                        }
                    }
                    // edited: type where to go
                    Container {
                        visible: root.editing
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Center
                        leftPadding: 20; rightPadding: 12
                        TextField {
                            id: field
                            hintText: "Search or type an address"
                            backgroundVisible: false
                            focusHighlightEnabled: false
                            inputMode: TextFieldInputMode.Url
                            textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#ECEEF1") }
                            onTextChanging: root.setSug(root.editing ? store.suggest(text) : [])
                            input {
                                submitKey: SubmitKey.Go
                                flags: TextInputFlag.SpellCheckOff | TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff | TextInputFlag.PredictionOff
                                onSubmitted: root.go()
                            }
                        }
                    }
                }

                BarButton {
                    visible: !root.editing && !root.tabsOpen
                    icon: "asset:///icons/tabs.png"; name: engine.tabCount + " tabs"
                    badge: engine.tabCount > 99 ? "99" : "" + engine.tabCount
                    onClicked: root.showList("tabs")
                }
                BarButton {
                    visible: !root.editing && !root.tabsOpen
                    icon: "asset:///icons/menu.png"; name: "Menu"
                    onClicked: { if (root.menuOpen) root.menuOpen = false; else root.openMenu(); }
                }
                // leave address editing without going anywhere
                Container {
                    id: cancel
                    property bool down: false
                    visible: root.editing && engine.view != "start"
                    verticalAlignment: VerticalAlignment.Fill
                    layout: DockLayout {}
                    leftPadding: 28; rightPadding: 20
                    accessibility.name: "Cancel"
                    onTouch: { if (event.isDown()) cancel.down = true; else if (event.isUp() || event.isCancel()) cancel.down = false; }
                    gestureHandlers: [ TapHandler { onTapped: root.endEdit() } ]
                    Label {
                        text: "Cancel"
                        verticalAlignment: VerticalAlignment.Center
                        opacity: cancel.down ? 0.6 : 1.0
                        textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#62A8FF") }
                    }
                }
            }

            // load progress: a thin line along the top edge of the bar
            Container {
                verticalAlignment: VerticalAlignment.Top
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: 4; minHeight: 4; maxHeight: 4
                background: Color.create("#62A8FF")
                opacity: engine.loading ? 1.0 : 0.0
                translationX: - engine.pageWidth * (1.0 - Math.max(engine.progress, 8) / 100.0)
            }
        }
    }
}
