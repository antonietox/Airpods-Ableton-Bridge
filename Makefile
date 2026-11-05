ARCHS  = -arch x86_64 -arch arm64
CFLAGS = -O2 -Wall -Wno-unused-parameter $(ARCHS)
LIBS   = -framework CoreAudio -framework CoreFoundation
DRV    = build/AirPodsBridge.driver
APP    = build/AirPods Bridge.app

all: driver app

driver:
	mkdir -p "$(DRV)/Contents/MacOS"
	cp src/driver/Info.plist "$(DRV)/Contents/"
	clang $(CFLAGS) -bundle -o "$(DRV)/Contents/MacOS/AirPodsBridge" src/driver/AirPodsBridge.c $(LIBS)
	codesign --force -s - "$(DRV)"

app:
	mkdir -p "$(APP)/Contents/MacOS"
	cp src/forwarder/Info.plist "$(APP)/Contents/"
	clang $(CFLAGS) -o "$(APP)/Contents/MacOS/apbfwd" src/forwarder/apbfwd.c $(LIBS)
	codesign --force -s - "$(APP)"

clean:
	rm -rf build

.PHONY: all driver app clean
