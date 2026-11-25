// BarButton: an icon button for the bottom bar. 120 px wide touch area, pressed highlight, disabled state.
import bb.cascades 1.4

Container {
    id: btn
    property alias icon: img.imageSource
    property string name: ""               // what a screen reader says
    property bool active: true             // false: drawn faint, ignores taps
    property int iconSize: 56
    property bool down: false
    property alias badge: count.text       // a short text drawn inside the icon (the number of tabs)
    signal clicked()

    layout: DockLayout {}
    preferredWidth: 120; minWidth: 120
    verticalAlignment: VerticalAlignment.Fill
    accessibility.name: btn.name
    onTouch: {
        if (!btn.active) return;
        if (event.isDown()) btn.down = true;
        else if (event.isUp() || event.isCancel()) btn.down = false;
    }
    gestureHandlers: [ TapHandler { onTapped: if (btn.active) btn.clicked() } ]

    ImageView {
        imageSource: "asset:///shapes/press.png"
        preferredWidth: 100; preferredHeight: 100
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        opacity: btn.down ? 1.0 : 0.0
    }
    ImageView {
        id: img
        preferredWidth: btn.iconSize; preferredHeight: btn.iconSize
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        scalingMethod: ScalingMethod.AspectFit
        opacity: btn.active ? 1.0 : 0.3
    }
    Label {
        id: count
        visible: text != ""
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        textStyle { base: SystemDefaults.TextStyles.SmallText; fontWeight: FontWeight.W500; color: Color.create("#ECEEF1") }
    }
}
