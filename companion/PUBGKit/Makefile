export THEOS_PACKAGE_SCHEME = rootless
ARCHS = arm64 arm64e
TARGET = iphone:clang:latest:15.0

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = PUBGKit
PUBGKit_FILES = Tweak.xm src/Mem.cpp src/Core.cpp src/Probe.mm src/Menu.mm src/ESP.mm src/Aimbot.mm
PUBGKit_CFLAGS = -I$(THEOS_PROJECT_DIR)/src
PUBGKit_CCFLAGS = -std=c++17
PUBGKit_OBJCFLAGS = -fobjc-arc
PUBGKit_OBJCCFLAGS = -fobjc-arc -std=c++17
PUBGKit_FRAMEWORKS = UIKit Foundation QuartzCore CoreGraphics

include $(THEOS_MAKE_PATH)/tweak.mk

after-stage::
	mkdir -p "$(THEOS_STAGING_DIR)$(THEOS_PACKAGE_INSTALL_PREFIX)/Library/Application Support/PUBGKit"
	cp resources/offsets.plist "$(THEOS_STAGING_DIR)$(THEOS_PACKAGE_INSTALL_PREFIX)/Library/Application Support/PUBGKit/offsets.plist"
