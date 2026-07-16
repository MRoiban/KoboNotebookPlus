NICKELHOOK ?= ../../third_party/NickelHook/

include $(NICKELHOOK)/NickelHook.mk

override LIBRARY  := libcustomnotebooktemplates.so
override SOURCES  += src/customnotebooktemplates.cc src/fs_util.cc src/settings.cc \
	src/templates.cc
override MOCS     += src/covermenureceiver.h
override PKGCONF  += Qt5Widgets
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers \
	-fvisibility=hidden -fvisibility-inlines-hidden

# Always restore the firmware's exact stock library in the same update that
# installs the plugin. The archive deliberately leaves every onboard template
# and manifest file untouched.
override KOBOROOT += package/libiinknote.so:/usr/local/Kobo/libiinknote.so

include $(NICKELHOOK)/NickelHook.mk
