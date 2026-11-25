// BarAction: an icon with a word, for the bar while the tab list is open. Pressed: fades.
import bb.cascades 1.4

Container {
    id: act
    property alias icon: img.imageSource
    property alias text: label.text
    property bool accent: false            // the one that ends the mode
    property bool down: false
    signal clicked()

    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
    verticalAlignment: VerticalAlignment.Fill
    leftPadding: 28; rightPadding: 28
    opacity: act.down ? 0.6 : 1.0
    accessibility.name: label.text
    onTouch: {
        if (event.isDown()) act.down = true;
        else if (event.isUp() || event.isCancel()) act.down = false;
    }
    gestureHandlers: [ TapHandler { onTapped: act.clicked() } ]

    ImageView {
        id: img
        visible: !act.accent
        preferredWidth: 48; preferredHeight: 48
        verticalAlignment: VerticalAlignment.Center
        scalingMethod: ScalingMethod.AspectFit
        opacity: 0.85
    }
    Label {
        id: label
        leftMargin: act.accent ? 0 : 16
        verticalAlignment: VerticalAlignment.Center
        textStyle { base: SystemDefaults.TextStyles.BodyText; color: act.accent ? Color.create("#62A8FF") : Color.create("#ECEEF1") }
    }
}
