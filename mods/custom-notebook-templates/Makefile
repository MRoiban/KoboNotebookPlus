NICKELHOOK ?= ../../third_party/NickelHook/

ifneq ($(filter check,$(MAKECMDGOALS)),check)
include $(NICKELHOOK)/NickelHook.mk
endif

override LIBRARY  := libcustomnotebooktemplates.so
override SOURCES  += src/firmware_resolver.cc src/fs_util.cc src/settings.cc \
	src/templates.cc src/covers.cc src/cover_cache.cc src/cover_editor.cc src/page_io.cc \
	src/notebook_widget.cc src/notebook_hook_services.cc src/notebook_menu.cc src/pages.cc \
	src/page_actions.cc src/export_naming.cc src/export_range.cc \
	src/passive_input.cc src/passive_input_router.cc \
	src/three_finger_swipe.cc src/notebook_gestures.cc \
	src/notebook_sleep.cc src/visibility.cc src/eraser_menu.cc src/layers_state.cc \
	src/layers_service.cc src/layers_preview.cc src/layers_eraser.cc src/layers_menu.cc \
	src/plugin_runtime.cc src/plugin_entry.cc src/hooks.cc src/visibility_hooks.cc
override MOCS     += src/covermenureceiver.h src/exportrangereceiver.h \
	src/notebooksleepsettingsreceiver.h
override PKGCONF  += Qt5Widgets
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers \
	-fvisibility=hidden -fvisibility-inlines-hidden -MMD -MP
override CFLAGS   += -MMD -MP

# Always restore the firmware's exact stock library in the same update that
# installs the plugin. The archive deliberately leaves every onboard template
# and manifest file untouched.
override KOBOROOT += package/libiinknote.so:/usr/local/Kobo/libiinknote.so
override KOBOROOT += package/iink-export-best:/usr/local/Kobo/iink-export-best
override KOBOROOT += package/iink-export-fast:/usr/local/Kobo/iink-export-fast
override KOBOROOT += iink-launcher.sh:/usr/local/Kobo/iink-launcher.sh

.PHONY: exporters
exporters:
	python3 ../../scripts/make-iink-exporters.py \
		../../extracted/rootfs/usr/local/Kobo/iink \
		package/iink-export-best package/iink-export-fast

install koboroot: exporters

.PHONY: check
check:
	python3 ../../scripts/test-three-finger-swipe.py
	python3 ../../scripts/test-export-naming.py
	python3 ../../scripts/test-export-contracts.py
	python3 ../../scripts/test-hot-path-policy.py
	python3 ../../scripts/test-summarize-notebook-performance.py
	python3 ../../scripts/test-seed-generated-asset-cache.py
	python3 ../../scripts/test-collect-device-stability.py
	python3 ../../scripts/test-make-iink-exporters.py
	python3 ../../scripts/test-verify-layer-artifacts.py
	python3 ../../scripts/test-verify-binary-parity.py
	python3 ../../scripts/test-verify-globals-budget.py
	python3 ../../scripts/test-verify-release-package.py
	python3 ../../scripts/verify-globals-budget.py

ifneq ($(filter check,$(MAKECMDGOALS)),check)
include $(NICKELHOOK)/NickelHook.mk
# NickelHook's compile rules pass $^, so the header prerequisites pulled in from
# the generated *.d files would be handed to the compiler as extra inputs. Keep
# only the translation unit itself.
override nh_cmd_c    = $(CC) $(CPPFLAGS) $(CFLAGS) -c $(filter %.c,$(2)) -o $(1)
override nh_cmd_cc   = $(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(filter %.cc %.cpp,$(2)) -o $(1)
override nh_cmd_moco = $(CXX) -xc++ $(CPPFLAGS) $(CXXFLAGS) -c $(filter %.moc,$(2)) -o $(1)
override nh_cmd_rcco = $(CXX) -xc++ $(CPPFLAGS) $(CXXFLAGS) -c $(filter %.rcc,$(2)) -o $(1)

override DEPFILES := $(OBJECTS_C:.o=.d) $(OBJECTS_CXX:.o=.d) \
	$(OBJECTS_CXX1:.o=.d) $(OBJECTS_MOC:.o=.d) $(OBJECTS_RCC:.o=.d)
override GENERATED += $(DEPFILES)
-include $(DEPFILES)
endif
