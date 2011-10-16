#!/usr/bin/env bash
# Litecoin
# Creates a Litecoin.dmg OSX file from the contrib/LitecoinTemplate.dmg file
#
# Recipe from: http://digital-sushi.org/entry/how-to-create-a-disk-image-installer-for-apple-mac-os-x/
#
# To make a prettier LitecoinTemplate.dmg:
#  + open (mount) LitecoinTemplate.dmg
#  + change the file properties, icon positions, background image, etc
#  + eject, then commit the changed LitecoinTemplate.dmg
#

CWD=$(pwd)

if [ $# -lt 1 ]; then
    if [ $(basename $CWD) == "contrib" ]
    then
        TOP=$(dirname $CWD)
    else
        echo "Usage: $0 /path/to/litecoin/tree"
        exit 1
    fi
else
    TOP=$1
fi

# Create Litecoin-Qt.app
cd "$TOP"
if [ ! -e Makefile ]; then qmake litecoin-qt.pro; fi
make
macdeployqt Litecoin-Qt.app
# Workaround a bug in macdeployqt: https://bugreports.qt.nokia.com/browse/QTBUG-21913
# (when fixed, this won't be necessary)
cp /opt/local/lib/db48/libdb_cxx-4.8.dylib Litecoin-Qt.app/Contents/Frameworks/
install_name_tool -id @executable_path/../Frameworks/libdb_cxx-4.8.dylib \
    Litecoin-Qt.app/Contents/Frameworks/libdb_cxx-4.8.dylib
install_name_tool -change libqt.3.dylib \
        @executable_path/../Frameworks/libqt.3.dylib \
        Litecoin-Qt.app/Contents/MacOS/Litecoin-Qt

# Create a .dmg
macdeployqt Litecoin-Qt.app -dmg

# Compile litecoind
cd "$TOP/src"
rm litecoind
STATIC=1 make -f makefile.osx

