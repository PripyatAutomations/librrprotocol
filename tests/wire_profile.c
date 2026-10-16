#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#include <librrprotocol/wire.h>
static uint64_t test_clock = 1000000;
static uint64_t profile_clock(void) { return test_clock++; }
#ifdef TEST_NO_PROFILE
#undef USE_PROFILING
#else
#ifndef USE_PROFILING
#define USE_PROFILING 1
#endif
#endif
#define mono_us profile_clock
#include <librrprotocol/wire.c>
#undef mono_us
static unsigned dumps, rows;
static char first_row[512];
void Log(logpriority_t priority, const char *subsys, const char *format, ...) {
   if (strcmp(subsys, "profile")) return;
   char text[512];
   va_list args;
   va_start(args, format);
   vsnprintf(text, sizeof(text), format, args);
   va_end(args);
   if (strstr(text, "WebSocket JSON parser counts")) { dumps++; rows = 0; }
   else if (!strncmp(text, "  ", 2) && rows++ == 0) snprintf(first_row, sizeof(first_row), "%s", text);
   rr_wire_profile_dump(); /* Logging can forward messages or re-enter profiling. */
}
int main(void) {
   alarm(5);
   const char *list = "{\"op\":\"media.list\"}";
   for (unsigned i = 0; i < 2; i++) {
      dict *message = rr_wire_decode(list);
      assert(message); dict_free(message);
   }
   assert(!rr_wire_decode("{\"op\":\"media.list\",\"unexpected\":1}"));
   assert(!rr_wire_decode("{\"op\":\"untrusted-operation-name\"}"));
   assert(!rr_wire_decode("{\"op\":"));
#ifdef USE_PROFILING
   assert(dumps == 1);
   const rr_wire_rule_t *rule;
   dict *wire = json2dict(list);
   rule = rule_for(wire, false);
   dict_free(wire);
   wire_profile_count_t *counts = &wire_profile_counts[rule - wire_rules];
   assert(counts->count == 3 && counts->rejected == 1 && counts->parse_us == 3);
   assert(counts->bytes == 2 * strlen(list) + strlen("{\"op\":\"media.list\",\"unexpected\":1}"));
   assert(wire_profile_counts[WIRE_PROFILE_INVALID].count == 2);
   test_clock += 299000000;
   rr_wire_profile_dump(); assert(dumps == 1);
   test_clock += 1000000;
   rr_wire_profile_dump(); assert(dumps == 2);
   assert(strstr(first_row, "media.list: 3 messages") && strstr(first_row, "1 rejected"));
   puts("PASS: bounded per-operation JSON parser counts, bytes, rejects, timing, ranking and reentrant five-minute dumps");
#else
   rr_wire_profile_dump(); assert(!dumps);
   puts("PASS: parser profiling compiles out without USE_PROFILING");
#endif
}
