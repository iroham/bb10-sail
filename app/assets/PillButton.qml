// PillButton: the one filled button (accent pill, dark label). 96 px tall, as wide as its label needs.
import bb.cascades 1.4

Container {
    id: btn
    property alias text: label.text
    property bool down: false
    signal clicked()

    layout: DockLayout {}
    preferredHeight: 96; minHeight: 96
    accessibility.name: label.text
    onTouch: {
        if (event.isDown()) btn.down = true;
        else if (event.isUp() || event.isCancel()) btn.down = false;
    }
    gestureHandlers: [ TapHandler { onTapped: btn.clicked() } ]

    ImageView {
        imageSource: btn.down ? "asset:///shapes/button_down.amd" : "asset:///shapes/button.amd"
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
    }
    Container {
        leftPadding: 56; rightPadding: 56
        verticalAlignment: VerticalAlignment.Center
        Label {
            id: label
            textStyle { base: SystemDefaults.TextStyles.BodyText; fontWeight: FontWeight.W500; color: Color.create("#0C1524") }
        }
    }
}
