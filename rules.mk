librrprotocol ?= librrprotocol.so
libs += ${librrprotocol}

# !ls *.c|sed 's/.c$/.o/g'|sed 's/^/librrprotocol_objs += /g'
#librrprotocol_objs += au_gst.o
librrprotocol_objs += auth.hash.o
librrprotocol_objs += codecneg.o
librrprotocol_objs += connman.o
librrprotocol_objs += cfg.servers.o
librrprotocol_objs += cli.alert.o
librrprotocol_objs += cli.chat.o
librrprotocol_objs += cli.error.o
#librrprotocol_objs += cli.irc.o
librrprotocol_objs += cli.main.o
librrprotocol_objs += cli.notice.o
librrprotocol_objs += cli.ping.o
librrprotocol_objs += cli.rigctl.o
librrprotocol_objs += cli.syslog.o
librrprotocol_objs += http.api.o
# XXX: This needs updated to use *either* ev or mongoose as configured.
#librrprotocol_objs += irc.o
#librrprotocol_objs += irc.capab.o
#librrprotocol_objs += irc.channel.o
#librrprotocol_objs += irc.client.o
#librrprotocol_objs += irc.commands.o
#librrprotocol_objs += irc.event.o
#librrprotocol_objs += irc.modes.o
#librrprotocol_objs += irc.numerics.o
#librrprotocol_objs += irc.parser.o
#librrprotocol_objs += irc.server.o
#librrprotocol_objs += irc.user.o
librrprotocol_objs += is.o
librrprotocol_objs += rrclient.o
librrprotocol_objs += srv.auth.o
librrprotocol_objs += srv.auth.passdb.o
librrprotocol_objs += srv.chat.o
librrprotocol_objs += srv.chat.error.o
librrprotocol_objs += srv.http.o
librrprotocol_objs += srv.http.listeners.o
librrprotocol_objs += srv.client.o
librrprotocol_objs += srv.session.o
librrprotocol_objs += srv.irc.core.o
librrprotocol_objs += srv.ping.o
librrprotocol_objs += srv.rigctl.o
librrprotocol_objs += srv.send.o
librrprotocol_objs += vfo.o
#librrprotocol_objs += ws.audio.o
# This needs merged into various other files...
#librrprotocol_objs += wsnew.o
librrprotocol_objs += ws.auth.o
librrprotocol_objs += ws.file-xfer.o
#librrprotocol_objs += ws.media.o
#librrprotocol_objs += ws.mediachan.o

librrprotocol_cflags := ${CFLAGS} -I./modsrc/ -I./ -I./inc

extra_clean += ${librustyaxe_objs} ${librustyaxe}
librrprotocol_headers := $(wildcard librrprotocol/*.h)
librrprotocol_srcs = $(wildcard librrprotocol/*.c)

real_librrprotocol_objs := $(foreach x, ${librrprotocol_objs}, ${BUILD_DIR}/librrprotocol/${x})
${librrprotocol_srcs}: GNUmakefile ${librrprotocol_headers} librrprotocol/rules.mk ${BUILD_DIR}/build_config.h

${BUILD_DIR}/librrprotocol/.stamp:
	@mkdir -p "${BUILD_DIR}/librrprotocol/"
	touch "${BUILD_DIR}/librrprotocol/.stamp"

${librrprotocol}: ${BUILD_DIR}/librrprotocol/.stamp ${real_librrprotocol_objs} ${librrprotocol_headers} GNUmakefile librrprotocol/rules.mk
	@echo "[link] $@ from $(words ${real_librrprotocol_objs}) objects"
	@${CC} ${LDFLAGS} ${LIB_LDFLAGS} -lm -o $@ ${real_librrprotocol_objs} || exit 2

${BUILD_DIR}/librrprotocol/%.o:librrprotocol/%.c GNUmakefile ${librrprotocol_headers} ${librustyaxe} ${librustyaxe_headers}
	@echo "[compile] $< => $@"
	@${RM} $@
	@${CC} ${librrprotocol_cflags} -o $@ -c $< || exit 2
