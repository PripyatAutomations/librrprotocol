#include "crypto.h"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
struct rr_crypto_key { EVP_PKEY *pkey; };
static rr_crypto_key_t *wrap(EVP_PKEY *p) {
   if (!p || EVP_PKEY_base_id(p) != EVP_PKEY_ED25519) { EVP_PKEY_free(p); return NULL; }
   rr_crypto_key_t *k = calloc(1,sizeof(*k));
   if (!k) { EVP_PKEY_free(p); return NULL; }
   k->pkey = p; return k;
}
bool rr_crypto_init(void) { return true; }
const char *rr_crypto_provider(void) { return "openssl"; }
rr_crypto_key_t *rr_crypto_ed25519_generate(void) {
   EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519,NULL);
   EVP_PKEY *p = NULL;
   if (!ctx || EVP_PKEY_keygen_init(ctx) != 1 || EVP_PKEY_keygen(ctx,&p) != 1) { EVP_PKEY_free(p); p=NULL; }
   EVP_PKEY_CTX_free(ctx); return wrap(p);
}
rr_crypto_key_t *rr_crypto_key_load_pem(const char *path) {
   FILE *f = fopen(path,"rb"); if (!f) return NULL;
   EVP_PKEY *p = PEM_read_PrivateKey(f,NULL,NULL,NULL); fclose(f); return wrap(p);
}
bool rr_crypto_key_write_pem(const rr_crypto_key_t *key, const char *path) {
   if (!key || !path) return false;
   FILE *f = fopen(path,"wb"); if (!f) return false;
   bool ok = PEM_write_PrivateKey(f,key->pkey,NULL,NULL,0,NULL,NULL) == 1;
   if (fclose(f)) ok=false;
   return ok;
}
rr_crypto_key_t *rr_crypto_ed25519_public(const uint8_t *raw, size_t len) {
   if (!raw || len!=32) return NULL;
   return wrap(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519,NULL,raw,len));
}
bool rr_crypto_ed25519_public_bytes(const rr_crypto_key_t *k, uint8_t out[32]) {
   size_t len=32;
   return k && out && EVP_PKEY_get_raw_public_key(k->pkey,out,&len)==1 && len==32;
}
bool rr_crypto_ed25519_sign(const rr_crypto_key_t *k,const void *data,size_t len,uint8_t sig[64]) {
   if (!k || !data || !sig) return false;
   EVP_MD_CTX *ctx=EVP_MD_CTX_new(); size_t n=64;
   bool ok=ctx && EVP_DigestSignInit(ctx,NULL,NULL,NULL,k->pkey)==1 &&
      EVP_DigestSign(ctx,sig,&n,data,len)==1 && n==64;
   EVP_MD_CTX_free(ctx); return ok;
}
bool rr_crypto_ed25519_verify(const rr_crypto_key_t *k,const void *data,size_t len,const uint8_t sig[64]) {
   if (!k || !data || !sig) return false;
   EVP_MD_CTX *ctx=EVP_MD_CTX_new();
   bool ok=ctx && EVP_DigestVerifyInit(ctx,NULL,NULL,NULL,k->pkey)==1 &&
      EVP_DigestVerify(ctx,sig,64,data,len)==1;
   EVP_MD_CTX_free(ctx); return ok;
}
bool rr_crypto_sha256(const void *data,size_t len,uint8_t out[32]) {
   return data && out && SHA256(data,len,out)!=NULL;
}
bool rr_crypto_random(void *data,size_t len) {
   return data && len<=INT_MAX && RAND_bytes(data,(int)len)==1;
}
void rr_crypto_key_free(rr_crypto_key_t *key) {
   if (key) { EVP_PKEY_free(key->pkey); free(key); }
}
