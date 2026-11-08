# Shared protocol source inventory for standalone and parent builds.
# !ls *.c|sed 's/.c$/.o/g'|sed 's/^/librrprotocol_objs += /g'
librrprotocol_objs += auth.hash.o
librrprotocol_objs += binframe.o
librrprotocol_objs += bridge.ipc.o
librrprotocol_objs += bridge.media.o
librrprotocol_objs += bridge.sdr.o
librrprotocol_objs += cfg.bridge.o
librrprotocol_objs += cfg.fwdsp.o # shared by rrserver, rrclient, fwdsp
librrprotocol_objs += codecneg.o
librrprotocol_objs += connman.o
librrprotocol_objs += crypto.openssl.o
librrprotocol_objs += crypto.cert.openssl.o
librrprotocol_objs += crypto.binding.o
#librrprotocol_objs += irc.client.o
#librrprotocol_objs += irc.server.o
# is_admin_online/is_elmer_online depend on rrserver sessions and ACLs;
# do not link application-only is.c into the standalone protocol SDK.
librrprotocol_objs += latency.o
librrprotocol_objs += link.wire.o
librrprotocol_objs += media.health.o
librrprotocol_objs += objects.o
librrprotocol_objs += rtp.o
librrprotocol_objs += rtp.socket.o
librrprotocol_objs += server.url.o
librrprotocol_objs += traffic.o
librrprotocol_objs += vfo.o
librrprotocol_objs += wire.o
#librrprotocol_objs += ws.audio.o
#librrprotocol_objs += ws.file-xfer.o
#librrprotocol_objs += ws.media.o
#librrprotocol_objs += wsnew.o
# This needs merged into various other files...

