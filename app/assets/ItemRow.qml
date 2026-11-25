// ItemRow: a place to go (a suggestion, a bookmark, a much-visited page). Icon, title, a quieter line under it.
import bb.cascades 1.4

Container {
    id: row
    property alias icon: img.imageSource
    property alias title: main.text
    property alias sub: under.text
    property int inset: 40                 // left padding, to line up with what is around the row
    property bool down: false
    signal clicked()

    layout: DockLayout {}
    horizontalAlignment: HorizontalAlignment.Fill
    preferredHeight: 148; minHeight: 148
    background: row.down ? Color.create("#2E3238") : Color.Transparent
    accessibility.name: main.text
    onTouch: {
        if (event.isDown()) row.down = true;
        else if (event.isUp() || event.isCancel()) row.down = false;
    }
    gestureHandlers: [ TapHandler { onTapped: row.clicked() } ]

    Container {
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Center
        leftPadding: row.inset; rightPadding: 40
        ImageView {
            id: img
            preferredWidth: 48; preferredHeight: 48
            verticalAlignment: VerticalAlignment.Center
            scalingMethod: ScalingMethod.AspectFit
            opacity: 0.7
        }
        Container {
            leftMargin: 32
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            verticalAlignment: VerticalAlignment.Center
            Label {
                id: main
                textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#ECEEF1") }
            }
            Label {
                id: under
                topMargin: 0
                visible: text != ""
                textStyle { base: SystemDefaults.TextStyles.SubtitleText; color: Color.create("#9BA2AC") }
            }
        }
    }
}
