librrprotocol ?= librrprotocol.so
libs += ${librrprotocol}

include librrprotocol/sources.mk

# SRTP belongs to the protocol transport library, never to fwdsp or global flags.
ifeq (${WITH_RTP},true)
librrprotocol_rtp_cflags := -DRR_HAVE_SRTP=1 ${SRTP_CFLAGS}
librrprotocol_ldlibs += ${SRTP_LIBS}
ifeq (${WITH_RTP_ENCRYPTION},true)
librrprotocol_rtp_cflags += -DRR_RTP_ENCRYPTION=1
endif
endif
# Exactly one crypto provider per build. No fallback to a second provider.
CRYPTO_BACKEND ?= openssl
ifneq ($(CRYPTO_BACKEND),openssl)
$(error Unsupported CRYPTO_BACKEND=$(CRYPTO_BACKEND); only openssl implemented)
endif
librrprotocol_ldlibs += $(shell pkg-config --libs openssl)
ifeq ($(LIBRUSTYAXE_PROVIDER),system)
# Installed RustyAxe headers must outrank checkout-root headers.
librrprotocol_cflags := ${CFLAGS} ${librrprotocol_rtp_cflags} -I./modsrc/ -idirafter ./ -idirafter ./inc
else
librrprotocol_cflags := ${CFLAGS} ${librrprotocol_rtp_cflags} -I./modsrc/ -I./ -I./inc
endif

# Switching optional protection features must not reuse objects from another build.
.PHONY: rrprotocol-rtp-config-force
${BUILD_DIR}/librrprotocol/.rtp-config: rrprotocol-rtp-config-force
	@mkdir -p $(dir $@)
	@printf '%s\n' '${WITH_RTP} ${WITH_RTP_ENCRYPTION} ${SRTP_CFLAGS} ${SRTP_LIBS}' > $@.new
	@cmp -s $@.new $@ || cp $@.new $@
	@rm -f $@.new
${BUILD_DIR}/librrprotocol/rtp.o ${BUILD_DIR}/librrprotocol/rtp.socket.o: ${BUILD_DIR}/librrprotocol/.rtp-config

extra_clean += librrprotocol.so.0
# librrprotocol headers include librustyaxe's public structs.  Make those
# headers prerequisites too, otherwise a shared-struct change can leave the
# protocol library with a stale definition of global arrays such as http_users.
librrprotocol_headers := $(wildcard librrprotocol/*.h)
ifeq ($(LIBRUSTYAXE_PROVIDER),local)
librrprotocol_headers += $(wildcard librustyaxe/*.h) $(wildcard inc/librustyaxe/*.h)
endif
librrprotocol_srcs = $(wildcard librrprotocol/*.c)

real_librrprotocol_objs := $(foreach x, ${librrprotocol_objs}, ${BUILD_DIR}/librrprotocol/${x})
${librrprotocol_srcs}: GNUmakefile ${librrprotocol_headers} librrprotocol/rules.mk ${BUILD_DIR}/build_config.h

${BUILD_DIR}/librrprotocol/.stamp:
	@mkdir -p "${BUILD_DIR}/librrprotocol/"
	touch "${BUILD_DIR}/librrprotocol/.stamp"

${librrprotocol}: ${BUILD_DIR}/librrprotocol/.stamp ${real_librrprotocol_objs} ${librrprotocol_headers} GNUmakefile librrprotocol/rules.mk
	@echo "[link] $@ from $(words ${real_librrprotocol_objs}) objects"
	@${CC} ${LDFLAGS} ${LIB_LDFLAGS} $(call SHARED_ID_FLAG,librrprotocol.so) -lm -o $@ ${real_librrprotocol_objs} -lrustyaxe ${librrprotocol_ldlibs} || exit 2
	@ln -sf librrprotocol.so librrprotocol.so.0

# Objects include build_config.h via the protocol/rustyaxe headers; wait for
# pack-eeprom before compiling them.
${BUILD_DIR}/librrprotocol/%.o:librrprotocol/%.c GNUmakefile ${librrprotocol_headers} ${librustyaxe} ${librustyaxe_headers} ${OBJECT_ORDER_ONLY}
	@echo "[compile] $< => $@"
	@${RM} $@
	@${CC} ${librrprotocol_cflags} -o $@ -c $< || exit 2
