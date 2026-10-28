#include "crypto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Domain separated, length framed. This is NOT a TLS authentication check:
 * verify the peer's presented TLS SPKI through the transport as well. */
static bool binding_message(const char *domain, const char *owner,
                             const char *spki, char **out, size_t *size) {
   if (!domain || !*domain || !owner || !*owner || !spki || !*spki ||
       strlen(domain)>253 || strlen(owner)>128 || strlen(spki)>128)
      return false;
   static const char prefix[]="rustyrig.tls-catalog-binding.v1";
   size_t n=strlen(prefix)+1+strlen(domain)+strlen(owner)+strlen(spki)+64;
   char *buf=malloc(n);
   if (!buf) return false;
   int m=snprintf(buf,n,"%s\n%zu:%s%zu:%s%zu:%s",
      prefix,strlen(domain),domain,strlen(owner),owner,strlen(spki),spki);
   if(m<=0 || (size_t)m>=n){free(buf);return false;}
   *out=buf;*size=(size_t)m;return true;
}
bool rr_crypto_binding_sign(const rr_crypto_key_t *identity,const char *domain,
                            const char *owner,const char *spki,uint8_t sig[64]) {
   char *msg=NULL;size_t n=0;
   if(!identity || !sig || !binding_message(domain,owner,spki,&msg,&n))return false;
   bool ok=rr_crypto_ed25519_sign(identity,msg,n,sig);
   free(msg);return ok;
}
bool rr_crypto_binding_verify(const uint8_t pub[32],const char *domain,
                              const char *owner,const char *spki,
                              const uint8_t sig[64]) {
   if(!pub || !sig)return false;
   rr_crypto_key_t *k=rr_crypto_ed25519_public(pub,32);
   char *msg=NULL;size_t n=0;
   bool ok=k && binding_message(domain,owner,spki,&msg,&n) &&
      rr_crypto_ed25519_verify(k,msg,n,sig);
   free(msg);rr_crypto_key_free(k);return ok;
}
