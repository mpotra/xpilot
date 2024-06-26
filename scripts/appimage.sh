#!/bin/sh

APPIMAGE_PATH="xPilot.AppDir"

rm -rf $APPIMAGE_PATH
mkdir -p $APPIMAGE_PATH

chmod +x ../scripts/appimagetool-x86_64.AppImage
mkdir -p ${APPIMAGE_PATH}/usr/lib
mkdir -p ${APPIMAGE_PATH}/usr/bin
mv ../scripts/AppRun-x86_64 ${APPIMAGE_PATH}/AppRun
chmod +x ${APPIMAGE_PATH}/AppRun

# Copy icons
cp xpilot.png ${APPIMAGE_PATH}/
{
	for size in 16 24 48 64 128 256 512; do
		ICON_PATH=${APPIMAGE_PATH}/usr/share/icons/hicolor/${size}x${size}/apps
		mkdir -p $ICON_PATH
		cp xpilot.png ${ICON_PATH}/xpilot.png
	done
}

cp xpilot.desktop ${APPIMAGE_PATH}/
cp xpilot.png ${APPIMAGE_PATH}/
cp build/xpilot ${APPIMAGE_PATH}/usr/bin/
chmod +x ${APPIMAGE_PATH}/usr/bin/xpilot

../scripts/appimagetool-x86_64.AppImage ${APPIMAGE_PATH}
mkdir ../dist && mv xPilot*.AppImage* ../dist/xPilot.AppImage