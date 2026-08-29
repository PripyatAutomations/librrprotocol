//
// rrclient/ws.cat.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//

#if     defined(USE_HTTP)
bool rr_cat_parse_ws(rr_cat_req_type reqtype, struct mg_ws_message *msg) {
   if (reqtype != REQ_WS || msg == NULL) {
      return false;
   }
   Log(LOG_CRAZY, "cat.ws", "parsing %d bytes from ws: |%.*s|", msg->data.len, msg->data.len, msg->data.buf);

   // Extract "cmd" and "val" from JSON
   const char *cmd_str = mg_json_get_str(msg->data, "$.cat.cmd");
   const char *val_str = mg_json_get_str(msg->data, "$.cat.val");
   Log(LOG_DEBUG, "cat.ws", "cmd: %s, val: %s", cmd_str, val_str);

   if (cmd_str && val_str) {
      // Copy cmd to a fixed-size buffer and null-terminate
      char cmd[16] = {
         0
      };
      strlcpy( cmd, cmd_str, sizeof(cmd) );

      // Convert val to an integer
      int val = atoi(val_str);
      Log(LOG_DEBUG, "cat.ws", "got cmd: %s", cmd);
   }
cleanup:
   free( (void *)cmd_str );
   free( (void *)val_str );

   return false;
}
#endif
