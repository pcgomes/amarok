/****************************************************************************************
 * Copyright (c) 2024 Tuomas Nurmi <tuomas@norsumanageri.org>                           *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or                        *
 * modify it under the terms of the GNU General Public License as                       *
 * published by the Free Software Foundation; either version 2 of                       *
 * the License or (at your option) version 3 or any later version                       *
 * accepted by the membership of KDE e.V. (or its successor approved                    *
 * by the membership of KDE e.V.), which shall act as a proxy                           *
 * defined in Section 14 of version 3 of the license.                                   *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.kde.kirigami 2.16 as Kirigami
import org.kde.amarok.qml 1.0 as AmarokQml
import org.kde.amarok.similarartists 1.0

AmarokQml.Applet {
    id: applet

    RowLayout {
        anchors.fill: parent
        Layout.margins: applet.spacing

        ColumnLayout {
            Layout.margins: applet.spacing
            Layout.maximumWidth: Math.min(parent.width / 2, 300)

            Label {
                id: artistLabel
                text: SimilarArtistsEngine.currentTarget ? i18nc("%1 is the artist for which similars are currently shown", "Similar to <b>%1</b>", SimilarArtistsEngine.currentTarget) : ""
                width: 100
            }
            ListView {
                id: similarArtistsList
                model: SimilarArtistsEngine.model
                visible: SimilarArtistsEngine.currentTarget
                Layout.fillHeight: true
                interactive: false
                contentHeight: height
                boundsBehavior: Flickable.StopAtBounds
                width: parent.width
                delegate: Rectangle {
                    height: ( applet.height - artistLabel.height - Kirigami.Units.smallSpacing ) / ( SimilarArtistsEngine.maximumArtists + 1 )
                    width: applet.width - Kirigami.Units.smallSpacing * 4 - ( currentlyHighlighted && currentlyHighlighted.visible ? currentlyHighlighted.width : 0 )
                    color: palette.window
                    MouseArea {
                        height: parent.height
                        hoverEnabled: true
                        anchors.fill: parent
                        z: 1

                        Rectangle {
                            anchors.fill: parent
                            color: Kirigami.Theme.highlightColor
                            opacity: currentlyHighlighted.header === name ? 0.2 : parent.containsMouse ? 0.1 : 0.0
                            z: 1
                        }

                        onClicked:
                        {
                            if( currentlyHighlighted.header == name )
                            {
                                currentlyHighlighted.header = ""
                                return
                            }
                            currentlyHighlighted.header = name;
                            currentlyHighlighted.lastfmLink = link;
                            currentlyHighlighted.cover = Qt.binding(function() { return albumcover });
                            currentlyHighlighted.bio = Qt.binding(function() { return bio });
                            currentlyHighlighted.counts = Qt.binding(function() {
                                if( listeners.length == 0 )
                                    return ""
                                let str = i18nc( "Artist's listener and playcount from Last.fm", "%1 listeners<br>%2 plays", listeners, plays )
                                if( parseInt( ownplays ) > 0 )
                                    str += i18nc( "Number of user's own scrobbles of an artist on Last.fm, appended to previous string if not 0", "<br>%1 plays by you", ownplays )
                                return "<i>" + str + "</i>"
                            });
                            // unfortunately disabled in last.fm api since 2019 or so
                            // currentlyHighlighted.image = image;
                        }

                        ProgressBar {
                            id: matchBar
                            from: 0
                            to: 100
                            width: parent.width / 3
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            value: match
                        }

                        Label {
                            id: nameLabel
                            text: name
                            elide: Text.ElideRight
                            anchors.left: matchBar.right
                            anchors.right: currentlyHighlighted.visible ? parent.right : undefined
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }
        Rectangle {
            id: currentlyHighlighted
            Layout.fillHeight: true
            Layout.fillWidth: true
            property alias header: headerLabel.text
            property alias bio: bioText.text
            property alias cover: albumCover.source
            property alias counts: countLabel.text
            property alias lastfmLink: lastfmLink.url
            color: palette.base
            radius: Kirigami.Units.smallSpacing / 2
            visible: header.length > 0

            Label {
                id: headerLabel
                anchors.topMargin: applet.spacing
                anchors.top: parent.top
                anchors.left: parent.left
                font.weight: Font.Bold
            }
            RoundButton {
                id: lastfmLink
                visible: headerLabel.text.length > 0
                property string url
                anchors.left: parent.left
                anchors.top: headerLabel.bottom
                width: height * 2
                icon.width: width * 0.8
                icon.source: applet.imageUrl("lastfm.png")
                radius: Kirigami.Units.smallSpacing
                onClicked: if(url) { Qt.openUrlExternally(url) }
            }
            Label {
                id: countLabel
                anchors.top: lastfmLink.bottom
                anchors.left: parent.left
            }

            Item {
                id: coverRect

                visible: albumCover.status == Image.Ready
                anchors.top: countLabel.bottom
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                width: albumCover.width + Kirigami.Units.smallSpacing

                Rectangle {
                    id: albumCoverBg
                    width: parent.width
                    height: albumCover.paintedHeight + Kirigami.Units.smallSpacing
                    anchors.centerIn: parent
                    color: "white"
                    radius: Kirigami.Units.smallSpacing / 2
                    border.width: albumCoverMouse.containsMouse ? 3 : 1
                    border.color: albumCoverMouse.containsMouse ? Kirigami.Theme.highlightColor : applet.palette.light
                    MouseArea {
                        id: albumCoverMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: SimilarArtistsEngine.navigateToArtist( headerLabel.text )
                    }
                }
                Image {
                    id: albumCover
                    anchors.margins: albumCoverBg.radius
                    anchors.centerIn: parent
                    height: parent.height - Kirigami.Units.smallSpacing
                    fillMode: Image.PreserveAspectFit
                }
            }

            Flickable {
                clip: true
                width: parent.width - Math.max( coverRect.width, headerLabel.width, countLabel.width ) - Kirigami.Units.smallSpacing
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: applet.spacing
                contentHeight: bioText.height
                Label {
                    id: bioText
                    width: parent.width
                    wrapMode: Text.Wrap
                    onLinkActivated: Qt.openUrlExternally(link)
                }
            }
        }
    }
    SystemPalette {
        id: palette
    }
}
