NICKELHOOK ?= ../../third_party/NickelHook/

ifneq ($(filter check,$(MAKECMDGOALS)),check)
include $(NICKELHOOK)/NickelHook.mk
endif

override LIBRARY  := libcustomnotebooktemplates.so
override SOURCES  += src/firmware_resolver.cc src/fs_util.cc src/settings.cc \
	src/templates.cc src/covers.cc src/cover_cache.cc src/cover_editor.cc src/page_io.cc \
	src/notebook_widget.cc src/notebook_hook_services.cc src/notebook_menu.cc src/pages.cc \
	src/page_actions.cc src/visibility.cc src/eraser_menu.cc src/layers_state.cc \
	src/layers_service.cc src/layers_preview.cc src/layers_eraser.cc src/layers_menu.cc \
	src/plugin_runtime.cc src/plugin_entry.cc src/hooks.cc src/visibility_hooks.cc
override MOCS     += src/covermenureceiver.h
override PKGCONF  += Qt5Widgets
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers \
	-fvisibility=hidden -fvisibility-inlines-hidden

# Always restore the firmware's exact stock library in the same update that
# installs the plugin. The archive deliberately leaves every onboard template
# and manifest file untouched.
override KOBOROOT += package/libiinknote.so:/usr/local/Kobo/libiinknote.so

.PHONY: check
check:
	python3 ../../scripts/test-verify-layer-artifacts.py
	python3 ../../scripts/test-verify-binary-parity.py
	python3 ../../scripts/test-verify-globals-budget.py
	python3 ../../scripts/verify-globals-budget.py

ifneq ($(filter check,$(MAKECMDGOALS)),check)
include $(NICKELHOOK)/NickelHook.mk
endif
