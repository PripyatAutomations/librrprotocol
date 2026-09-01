//
// srv.auth.passdb.c: Password file support for server authentication
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <arpa/inet.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

// This defines a hard-coded fallback path for httpd root, if not set in config
extern bool dying;
extern time_t now;
char session_token[HTTP_TOKEN_LEN + 1] = { 0 };

http_user_t http_users[HTTP_MAX_USERS];

// This is used in ws.* too, so not static
int http_getuid(const char *user) {
   if (!user) {
      return -1;
   }

   for (int i = 0 ; i < HTTP_MAX_USERS ; i++) {
      http_user_t *up = &http_users[i];

      if (up->name[0] == '\0' || up->pass[0] == '\0') {
         continue;
      }

      if (strcasecmp(up->name, user) == 0) {
         Log(LOG_CRAZY, "auth", "Found uid [%d] for username |%s|", i, up->name);
         return i;
      }
   }

   Log(LOG_CRAZY, "auth", "http_getuid(%s) returns not-found!", user);

   return -1;
}

static bool http_backup_authdb(void) {
   char new_path[256];
   struct tm *tm_info = localtime(&now);
   char date_str[9];  // YYYYMMDD + null terminator
   int index = 0;

   strftime(date_str, sizeof(date_str), "%Y%m%d", tm_info);

   // Find the next seqnum for the backup
   do{
      if (index > MAX_AUTHDB_BK_INDEX) {
         return true;
      }
      prepare_msg(new_path, sizeof(new_path), "%s.bak-%s.%d", HTTP_AUTHDB_PATH, date_str, index);
      index++;
   } while (file_exists(new_path) );

   // Rename the file
   if (rename(HTTP_AUTHDB_PATH, new_path) == 0) {
      Log(LOG_INFO, "http.core", "* Renamed old config (%s) to %s", HTTP_AUTHDB_PATH, new_path);
   } else {
      Log( LOG_CRIT, "http.core", "* Error renaming old config (%s) to %s: %d:%s", HTTP_AUTHDB_PATH, new_path, errno,
         strerror(errno) );
      return true;
   }

   return false;
}

bool http_save_users(const char *filename) {
   if (!filename) {
      return true;
   }

   if (http_backup_authdb() ) {
      return true;
   }
   int users_saved = 0;

   FILE *file = fopen(filename, "w");

   if (!file) {
      Log( LOG_CRIT, "auth", "Error saving user database to %s: %d:%s", filename, errno, strerror(errno) );
      return true;
   }
   Log(LOG_INFO, "auth", "Saving HTTP user database");

   for (int i = 0 ; i < HTTP_MAX_USERS ; i++) {
      http_user_t *up = &http_users[i];

      if (!up) {
         return true;
      }

      if (up->name[0] != '\0' && up->pass[0] != '\0') {
         Log(LOG_DEBUG, "auth", " => %s %sabled with privileges: %s", up->name, (up->enabled ? "en" : "dis"),
            up->privs);
         fprintf( file, "%d:%s:%d:%s:%s\n", up->uid, up->name, up->enabled, up->pass,
            (up->privs[0] != '\0' ? up->privs : "none") );
         users_saved++;
      }
   }

   fclose(file);
   Log(LOG_INFO, "auth", "Saved %d users to %s", users_saved, filename);
   return true;
}

// Load users from the file into the global array
int http_load_users(const char *filename) {
   if (!filename) {
      return -1;
   }
   Log(LOG_INFO, "auth", "Loading static users from %s", filename);
   FILE *file = fopen(filename, "r");

   if (!file) {
      return -1;
   }
   memset( http_users, 0, sizeof(http_users) );
   char line[HTTP_WS_MAX_MSG + 1];
   int user_count = 0;

   while (fgets(line, sizeof(line), file) && user_count < HTTP_MAX_USERS) {
      // Trim leading spaces
      char *start = line + strspn(line, " \t\r\n");

      if (start != line) {
         memmove(line, start, strlen(start) + 1);
      }

      // Skip comments and empty lines
      if (line[0] == '#' || line[0] == ';' ||
          (strlen(line) > 1 && (line[0] == '/' && line[1] == '/') ) || line[0] == '\n') {
         continue;
      }
      // Remove trailing \r or \n characters
      char *end = line + strlen(line) - 1;
      while (end >= line && (*end == '\r' || *end == '\n') ) {
         *end = '\0';
         end--;
      }
      // Trim leading spaces (again)
      start = line + strspn(line, " \t\r\n");

      if (start != line) {
         memmove(line, start, strlen(start) + 1);
      }

      if (line[0] == '\n' || line[0] == '\0') {
         continue;
      }
      http_user_t *up = NULL;
      char *token = strtok(line, ":");
      int i = 0, uid = -1;

      while (token && i < 7) {
         switch (i) {
            case 0: {
               // uid
               uid = atoi(token);
               up = &http_users[uid];
               up->uid = uid;
               break;
            }
            case 1: {
               // Username
               strlcpy( up->name, token, sizeof(up->name) );
               break;
            }
            case 2: {
               // Enabled flag
               up->enabled = atoi(token);
               break;
            }
            case 3: {
               // Password hash
               strlcpy( up->pass, token, sizeof(up->pass) );
               break;
            }
            case 4: {
               // Email
               strlcpy( up->email, token, sizeof(up->email) );
               break;
            }
            case 5: {
               // max_clones limit
               int val = atoi(token);

               if (val < 0 || val > HTTP_MAX_SESSIONS) {
                  Log(LOG_CRIT, "auth.core", "Loading user %s has invalid maxclones: %d (min: 1, max: %d)", up->name,
                     val, HTTP_MAX_SESSIONS);
               }
               up->max_clones = val;
               break;
            }
            case 6: {
               // Privileges
               strlcpy( up->privs, token, sizeof(up->privs) );
               Log(LOG_DEBUG, "auth", "load_users: uid=%d, user=%s, email=%s, enabled=%s, privs=%s, max_clones=%d", uid,
                  (up->name[0] != '\0' ? up->name : "none"), (up->email[0] != '\0' ? up->email : "none"),
                  (up->enabled ? "true" : "false"), (up->privs[0] != '\0' ? up->privs : "none"), up->max_clones);
               break;
            }
         }
         token = strtok(NULL, ":");
         i++;
      }
      user_count++;
   }
   Log(LOG_INFO, "auth", "Loaded %d static users from %s", user_count, filename);
   fclose(file);
   return 0;
}
