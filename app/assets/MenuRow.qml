// MenuRow: one action in the menu. Icon, label, pressed highlight; 104 px tall.
import bb.cascades 1.4

Container {
    id: row
    property alias icon: img.imageSource
    property alias text: label.text
    property bool down: false
    property bool active: true             // false: drawn faint, ignores taps
    signal clicked()

    layout: DockLayout {}
    horizontalAlignment: HorizontalAlignment.Fill
    preferredHeight: 104; minHeight: 104
    background: row.down ? Color.create("#2E3238") : Color.Transparent
    accessibility.name: label.text
    onTouch: {
        if (!row.active) return;
        if (event.isDown()) row.down = true;
        else if (event.isUp() || event.isCancel()) row.down = false;
    }
    gestureHandlers: [ TapHandler { onTapped: if (row.active) row.clicked() } ]

    Container {
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        verticalAlignment: VerticalAlignment.Center
        leftPadding: 40; rightPadding: 40
        opacity: row.active ? 1.0 : 0.35
        ImageView {
            id: img
            preferredWidth: 52; preferredHeight: 52
            verticalAlignment: VerticalAlignment.Center
            scalingMethod: ScalingMethod.AspectFit
            opacity: 0.85
        }
        Label {
            id: label
            leftMargin: 32
            verticalAlignment: VerticalAlignment.Center
            textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#ECEEF1") }
        }
    }
}
