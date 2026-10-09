// librustyaxe/codecneg.c: support for using gstreamer for audio streams
//    https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
//
// Here we handle moving audio between the server and gstreamer
//
// Here we manage negotiating codecs that are supported between both sides
// and framing audio.
//
#include <stdint.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <fwdsp/fwdsp-shared.h>

// if passed NULL for codecs, use all available codecs
const char *media_capab_prepare(const char *codecs) {
   if (!codecs) {
      return NULL;
   }
   // emit codec message
   char msgbuf[1024];
   snprintf(msgbuf, sizeof(msgbuf),
      "{ \"msg\": { \"type\": \"media\" }, \"media\": { \"cmd\": \"capab\", \"codecs\": \"%s\" } }", codecs);

   return strdup(msgbuf);
}

char *codec_filter_common(const char *preferred, const char *available) {
   char *result = NULL;
   size_t res_sz = 0;

   if (!preferred || !available) {
      Log(LOG_WARN, "codecneg", "codec_filter_common: empty list -- preferred:<%p> available:<%p>", preferred,
         available);

      return NULL;
   }
   const char *p = preferred;
   while (*p) {
      while (*p == ' ') {
         p++;
      }

      if (!*p) {
         break;
      }
      const char *start = p;
      while (*p && *p != ' ') {
         p++;
      }
      size_t len = p - start;

      const char *a = available;
      while (*a) {
         while (*a == ' ') {
            a++;
         }

         if (!*a) {
            break;
         }
         const char *astart = a;
         while (*a && *a != ' ') {
            a++;
         }
         size_t alen = a - astart;

         if (len == alen && memcmp(start, astart, len) == 0) {
            char *new_result = realloc(result, res_sz + len + 2);

            if (!new_result) {
               free(result);

               return NULL;
            }
            result = new_result;
            memcpy(result + res_sz, start, len);
            res_sz += len;
            result[res_sz++] = ' ';
            result[res_sz] = '\0';
            break;  // break out of 'available' loop, continue with next
                    // preferred
         }
      }
   }

   if (res_sz > 0 && result[res_sz - 1] == ' ') {
      result[--res_sz] = 0;
   }
   Log(LOG_CRAZY, "codecneg", "codec_filter_common(|%s|, |%s|) returned |%s|", preferred, available, result);

   return result;
}

char *codec_filter_test_mode(const char *codecs, bool test_mode) {
   if (!codecs) {
      return NULL;
   }

   char *result = strdup("");

   if (!result) {
      return NULL;
   }
   size_t result_len = 0;
   const char *p = codecs;

   while (*p) {
      while (*p == ' ') {
         p++;
      }

      if (!*p) {
         break;
      }

      const char *start = p;
      while (*p && *p != ' ') {
         p++;
      }
      size_t len = (size_t)(p - start);

      if ( !test_mode && len == 4 && codec_is_test_variant(start) ) {
         continue;
      }

      size_t extra = len + (result_len ? 1 : 0);
      char *grown = realloc(result, result_len + extra + 1);

      if (!grown) {
         free(result);

         return NULL;
      }
      result = grown;

      if (result_len) {
         result[result_len++] = ' ';
      }
      memcpy(result + result_len, start, len);
      result_len += len;
      result[result_len] = '\0';
   }

   /* Test mode is deliberately additive for the built-in audio formats. This lets an
    * older user config that lists the original tone variants pick up the newer pink
    * variants without silently changing production codec lists when test mode is
    * disabled. */
   if (test_mode) {
      static const char *const variants[][3] = {
         {
            "pc16", "pc1T", "pc1P"
         },
         {
            "g722", "g72T", "g72P"
         },
         {
            "mu16", "mu1T", "mu1P"
         },
         {
            "mu08", "mu0T", "mu0P"
         },
         {
            "opus", "opuT", "opuP"
         },
         {
            "oggv", "oggT", "oggP"
         },
         {
            "aacv", "aacT", "aacP"
         },
         {
            "flac", "flaT", "flaP"
         }
      };

      for (size_t i = 0 ; i < sizeof(variants) / sizeof(variants[0]) ; i++) {
         bool enabled = false;

         for (size_t j = 0 ; j < 3 ; j++) {
            const char *q = codecs;
            while (*q) {
               while (*q == ' ') {
                  q++;
               }

               if (!*q) {
                  break;
               }
               const char *start = q;
               while (*q && *q != ' ') {
                  q++;
               }
               size_t len = (size_t)(q - start);

               if (len == 4 && memcmp(start, variants[i][j], 4) == 0) {
                  enabled = true;
                  break;
               }
            }

            if (enabled) {
               break;
            }
         }

         if (!enabled) {
            continue;
         }

         for (size_t j = 1 ; j < 3 ; j++) {
            bool present = false;
            const char *q = result;
            while (*q) {
               while (*q == ' ') {
                  q++;
               }

               if (!*q) {
                  break;
               }
               const char *start = q;
               while (*q && *q != ' ') {
                  q++;
               }
               size_t len = (size_t)(q - start);

               if (len == 4 && memcmp(start, variants[i][j], 4) == 0) {
                  present = true;
                  break;
               }
            }

            if (present) {
               continue;
            }
            size_t len = strlen(variants[i][j]);
            char *grown = realloc(result, result_len + len + (result_len ? 1 : 0) + 1);

            if (!grown) {
               free(result);

               return NULL;
            }
            result = grown;

            if (result_len) {
               result[result_len++] = ' ';
            }
            memcpy(result + result_len, variants[i][j], len);
            result_len += len;
            result[result_len] = '\0';
         }
      }
   }

   return result;
}

bool codec_is_test_variant(const char codec[4]) {
   return codec && codec[0] && codec[1] && codec[2] &&
          (codec[3] == 'T' || codec[3] == 'P');
}
