#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <librrprotocol/codecneg.h>

time_t now;

int main(void) {
   assert(codec_is_test_variant("opuT"));
   assert(codec_is_test_variant("opuP"));
   assert(!codec_is_test_variant("opus"));

   char *normal = codec_filter_test_mode("pc16 pc1T g722 g72T opus opuT pc1P", false);
   assert(normal);
   assert(strcmp(normal, "pc16 g722 opus") == 0);
   free(normal);

   char *testing = codec_filter_test_mode("pc16 pc1T g722 g72T pc1P", true);
   assert(testing);
   assert(strcmp(testing, "pc16 pc1T g722 g72T pc1P g72P") == 0);
   free(testing);

   char *pink = codec_filter_test_mode("opus opuT", true);
   assert(pink);
   assert(strcmp(pink, "opus opuT opuP") == 0);
   free(pink);

   puts("PASS: test-mode codec filtering");
   return 0;
}
