import QtQuick 2.0
import CMPlayer 1.0 as Core
import QtQuick.Controls 1.0
import QtQuick.Controls.Styles 1.0

Item {
	id: dock
	readonly property real widthHint: 500
	property real dest: 0
	property bool show: false
	x: parent.width; y: 20; width: widthHint; height: parent.height-2*y; visible: false
	states: State {
		name: "show"; when: dock.show
		PropertyChanges { target: dock; visible: true }
		PropertyChanges { target: dock; x: dock.dest; explicit: true }
	}
	transitions: Transition {
		reversible: true; to: "show"
		SequentialAnimation {
			PropertyAction { property: "visible" }
			NumberAnimation { property: "x" }
		}
	}
	function updateDestination() { dock.dest = dock.parent.width-dock.width }
	Component.onCompleted: updateDestination()
	Connections { target: parent; onWidthChanged: { updateDestination() } }
	onWidthChanged: { updateDestination() }
	onDestChanged: { if (dock.show) dock.x = dock.dest }

	Rectangle {
		id: frame
		x: table.x-1; y: table.y-1
		width: table.width+2
		height: table.height+2
		border { color: "white"; width: 1 }
		color: Qt.rgba(0, 0, 0, 0.4)
	}

	Core.ModelView {
		id: table

		model: playlist
		headerVisible: false
		rowHeight: 40
		readonly property int nameFontSize: 15
		readonly property int locationFontSize: 10
		readonly property string nameFontFamily: Core.Util.monospace
		readonly property string locationFontFamily: Core.Util.monospace
		currentIndex: playlist.loaded
		function contentWidth() {
			var max = 0;
			for (var i=0; i<table.count; ++i) {
				var number = Core.Util.textWidth(playlist.number(i), table.nameFontSize, table.nameFontFamily);
				var name = Core.Util.textWidth(playlist.name(i), table.nameFontSize, table.nameFontFamily);
				var loc = Core.Util.textWidth(playlist.location(i), table.locationFontSize, table.locationFontFamily);
				max = Math.max(number + name, loc, max);
			}
			return max+30
		}

		onCountChanged: column.width = contentWidth()
		columns: Core.ItemColumn { title: "Name"; role: "name"; width: 200; id: column}

		onActivated: playlist.play(index)
		itemDelegate: Item {
			Column {
				width: parent.width
				Text {
					anchors { margins: 5; left: parent.left; right: parent.right }
					font {
						family: table.nameFontFamily; pixelSize: table.nameFontSize
						italic: current
						bold: current
					}
					verticalAlignment: Text.AlignVCenter; height: 25
					color: "white"; text: value; elide: Text.ElideRight
				}
				Text {
					anchors { margins: 5; left: parent.left; right: parent.right }
					font { family: table.locationFontFamily; pixelSize: table.locationFontSize }
					width: parent.width; height: table.locationFontSize; verticalAlignment: Text.AlignTop
					color: "white"; text: playlist.location(index); elide: Text.ElideRight
				}
			}
		}
	}

	MouseArea {
		anchors.fill: parent
		acceptedButtons: Qt.RightButton
		onClicked: Core.Util.execute("tool/playlist")
	}
}