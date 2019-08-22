#!/bin/sh

rm -rf alTrace.app
mkdir alTrace.app
cd alTrace.app
mkdir Contents
mkdir Contents/MacOS
mkdir Contents/Resources
cp ../../alTrace.icns Contents/Resources/
cp ../../alTrace-Mac-Info.plist Contents/Info.plist
cp -a ../altrace_wx Contents/MacOS/

