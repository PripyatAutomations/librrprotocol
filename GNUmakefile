# Standalone POSIX build; parent RustyRig build uses rules.mk.
# SPDX-License-Identifier: GPL-2.0-only
.DEFAULT_GOAL := all
CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr
MULTIARCH ?= $(shell $(CC) -dumpmachine)
LIBDIR ?= $(PREFIX)/lib/$(MULTIARCH)
INCLUDEDIR ?= $(PREFIX)/include
BUILD_DIR ?= build
DESTDIR ?=
VERSION ?= 0.1.0
SOVERSION ?= 0
WITH_RTP ?= false

include sources.mk
OBJECTS := $(addprefix $(BUILD_DIR)/objects/,$(librrprotocol_objs))
PUBLIC_HEADERS := $(filter-out %.internal.h internal.h,$(notdir $(wildcard *.h)))
PACKAGES := glib-2.0 gio-2.0 openssl libcurl sqlite3
ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)
ifeq ($(shell $(PKG_CONFIG) --exists $(PACKAGES) && echo yes),)
$(error Missing required development libraries: $(PACKAGES))
endif
endif
RR_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PACKAGES)) -I$(BUILD_DIR)/include -I$(BUILD_DIR)/generated -DHOST_POSIX=1
RR_LIBS := $(shell $(PKG_CONFIG) --libs $(PACKAGES)) $(shell $(PKG_CONFIG) --libs-only-L librustyaxe 2>/dev/null) -lrustyaxe -lm -pthread
ifeq ($(WITH_RTP),true)
RR_CFLAGS += -DRR_HAVE_SRTP=1 $(shell $(PKG_CONFIG) --cflags libsrtp2)
RR_LIBS += $(shell $(PKG_CONFIG) --libs libsrtp2)
endif
REAL_LIB := $(BUILD_DIR)/librrprotocol.so.$(SOVERSION).$(VERSION)
SONAME := librrprotocol.so.$(SOVERSION)
PC_FILE := $(BUILD_DIR)/librrprotocol.pc

.PHONY: all check install clean distclean deb src-deb source-deb
all: $(REAL_LIB) $(PC_FILE)

$(BUILD_DIR)/include/librrprotocol $(BUILD_DIR)/generated/build_config.h &:
	@mkdir -p $(BUILD_DIR)/include $(BUILD_DIR)/generated
	@ln -sfn ../.. $(BUILD_DIR)/include/librrprotocol
	@printf '%s\n' '/* Standalone librrprotocol build configuration. */' \
	    '#ifndef RR_PROTOCOL_BUILD_CONFIG_H' '#define RR_PROTOCOL_BUILD_CONFIG_H' \
	    '#define HOST_POSIX 1' '#endif' > $(BUILD_DIR)/generated/build_config.h

$(BUILD_DIR)/objects/%.o: %.c $(wildcard *.h) GNUmakefile | $(BUILD_DIR)/include/librrprotocol $(BUILD_DIR)/generated/build_config.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(RR_CFLAGS) $(CFLAGS) -fPIC -c -o $@ $<

$(REAL_LIB): $(OBJECTS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$(SONAME) -o $@ $^ $(RR_LIBS)
	ln -sfn $(notdir $@) $(BUILD_DIR)/$(SONAME)
	ln -sfn $(SONAME) $(BUILD_DIR)/librrprotocol.so

$(PC_FILE): GNUmakefile
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' 'prefix=$(PREFIX)' 'libdir=$(LIBDIR)' 'includedir=$(INCLUDEDIR)' '' \
	    'Name: librrprotocol' 'Description: RustyRig wire protocol library' \
	    'Version: $(VERSION)' 'Requires.private: glib-2.0 gio-2.0 openssl' \
	    'Libs: -L$${libdir} -lrrprotocol' 'Cflags: -I$${includedir}' > $@

check: all
	@echo "Standalone protocol library built. Integration tests run from rustyrig-fw."

install: all
	install -Dm755 $(REAL_LIB) "$(DESTDIR)$(LIBDIR)/$(notdir $(REAL_LIB))"
	ln -sfn $(notdir $(REAL_LIB)) "$(DESTDIR)$(LIBDIR)/$(SONAME)"
	ln -sfn $(SONAME) "$(DESTDIR)$(LIBDIR)/librrprotocol.so"
	install -d "$(DESTDIR)$(INCLUDEDIR)/librrprotocol"
	@for h in $(PUBLIC_HEADERS); do install -m644 "$h" "$(DESTDIR)$(INCLUDEDIR)/librrprotocol/$h"; done
	install -m644 $(BUILD_DIR)/generated/build_config.h "$(DESTDIR)$(INCLUDEDIR)/librrprotocol/build_config.h"
	install -Dm644 $(PC_FILE) "$(DESTDIR)$(LIBDIR)/pkgconfig/librrprotocol.pc"

clean:
	rm -rf "$(BUILD_DIR)"

distclean: clean
	@if command -v dh_clean >/dev/null 2>&1; then dh_clean; fi
	rm -rf debian/tmp debian/.debhelper debian/files debian/debhelper-build-stamp
	rm -rf debian/librrprotocol0 debian/librrprotocol-dev

deb:
	dpkg-buildpackage -b -us -uc

src-deb source-deb:
	@git rev-parse --is-inside-work-tree >/dev/null 2>&1 && git diff --quiet && git diff --cached --quiet || { echo "Commit tracked changes before source packaging" >&2; exit 1; }
	@v=$$(dpkg-parsechangelog -S Version | sed 's/-[^-]*$$//'); \
	 git archive --format=tar --prefix="librrprotocol-$$v/" HEAD -- . ':!debian' | gzip -n > "../librrprotocol_$$v.orig.tar.gz"
	dpkg-buildpackage -S -us -uc
