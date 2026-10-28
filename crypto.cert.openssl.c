#include "crypto.h"
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

static bool safe_name(const char *s) {
   if (!s || !*s || strlen(s)>253) return false;
   for (const char *p=s; *p; p++)
      if (!( (*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') ||
             (*p>='0'&&*p<='9') || *p=='.' || *p=='-')) return false;
   return true;
}
static bool add_ext(X509 *cert, int nid, const char *value) {
   X509V3_CTX ctx;
   X509V3_set_ctx(&ctx,cert,cert,NULL,NULL,0);
   X509_EXTENSION *ext=X509V3_EXT_conf_nid(NULL,&ctx,nid,(char*)value);
   if (!ext) return false;
   int ok=X509_add_ext(cert,ext,-1);
   X509_EXTENSION_free(ext);
   return ok==1;
}
static bool read_pair(const char *certpath,const char *keypath) {
   FILE *f=fopen(certpath,"rb"); if (!f) return false;
   X509 *cert=PEM_read_X509(f,NULL,NULL,NULL); fclose(f);
   f=fopen(keypath,"rb");
   EVP_PKEY *key=f?PEM_read_PrivateKey(f,NULL,NULL,NULL):NULL;
   if(f) fclose(f);
   bool ok=cert && key && X509_check_private_key(cert,key)==1;
   EVP_PKEY_free(key); X509_free(cert); return ok;
}
static bool write_exclusive(const char *path, bool cert, X509 *x, EVP_PKEY *key) {
   int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,cert?0644:0600);
   if(fd<0) return false;
   FILE *f=fdopen(fd,"wb");
   if(!f){ close(fd);unlink(path);return false; }
   bool ok=(cert?PEM_write_X509(f,x):PEM_write_PrivateKey(f,key,NULL,NULL,0,NULL,NULL))==1;
   if (ok && fflush(f)!=0) ok=false;
   if (ok && fsync(fileno(f))!=0) ok=false;
   if(fclose(f)!=0) ok=false;
   if(!ok) unlink(path);
   return ok;
}
bool rr_crypto_tls_identity_ensure(const char *certpath,const char *keypath,const char *dns) {
   if(!certpath || !keypath || !safe_name(dns) || !strcmp(certpath,keypath)) return false;
   struct stat a,b;
   bool c=lstat(certpath,&a)==0,k=lstat(keypath,&b)==0;
   if(c||k) return c&&k&&S_ISREG(a.st_mode)&&S_ISREG(b.st_mode)&&
       !(b.st_mode & 0077)&&read_pair(certpath,keypath);
   if(errno!=ENOENT) return false;
   EVP_PKEY_CTX *ctx=EVP_PKEY_CTX_new_id(EVP_PKEY_EC,NULL);
   EVP_PKEY *key=NULL; X509 *cert=NULL;
   bool ok=ctx && EVP_PKEY_keygen_init(ctx)==1 &&
      EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx,NID_X9_62_prime256v1)==1 &&
      EVP_PKEY_keygen(ctx,&key)==1;
   if(ok) cert=X509_new();
   ok=ok&&cert&&X509_set_version(cert,2)==1;
   unsigned char serial[16];
   if(ok) ok=RAND_bytes(serial,sizeof(serial))==1;
   if(ok){serial[0]&=0x7f; BIGNUM *n=BN_bin2bn(serial,sizeof(serial),NULL);
      ASN1_INTEGER *v=n?BN_to_ASN1_INTEGER(n,NULL):NULL;
      ok=v&&X509_set_serialNumber(cert,v)==1;
      ASN1_INTEGER_free(v);BN_free(n);}
   if(ok) ok=X509_gmtime_adj(X509_getm_notBefore(cert),-60)!=NULL &&
      X509_gmtime_adj(X509_getm_notAfter(cert),365L*24*3600)!=NULL &&
      X509_set_pubkey(cert,key)==1;
   X509_NAME *name=cert?X509_get_subject_name(cert):NULL;
   if(ok) ok=name&&X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,
      (const unsigned char*)dns,-1,-1,0)==1 &&
      X509_set_issuer_name(cert,name)==1;
   char san[300]; int len=snprintf(san,sizeof(san),"DNS:%s",dns);
   if(ok) ok=len>0&&(size_t)len<sizeof(san) &&
      add_ext(cert,NID_basic_constraints,"critical,CA:FALSE") &&
      add_ext(cert,NID_key_usage,"critical,digitalSignature") &&
      add_ext(cert,NID_ext_key_usage,"serverAuth") &&
      add_ext(cert,NID_subject_alt_name,san) &&
      X509_sign(cert,key,EVP_sha256())>0;
   /* Persist key before cert. Any orphaned key is an actionable failure;
    * we deliberately never silently replace it on restart. */
   if(ok) ok=write_exclusive(keypath,false,cert,key);
   if(ok) ok=write_exclusive(certpath,true,cert,key);
   if(ok) ok=read_pair(certpath,keypath);
   X509_free(cert);EVP_PKEY_free(key);EVP_PKEY_CTX_free(ctx);
   return ok;
}
char *rr_crypto_cert_spki_sha256(const char *path) {
   if(!path) return NULL;
   FILE *f=fopen(path,"rb"); if(!f) return NULL;
   X509 *cert=PEM_read_X509(f,NULL,NULL,NULL); fclose(f);
   EVP_PKEY *key=cert?X509_get_pubkey(cert):NULL;
   unsigned char *der=NULL;
   int n=key?i2d_PUBKEY(key,&der):0;
   unsigned char hash[32];unsigned int hlen=0;
   char *out=NULL;
   if(n>0 && EVP_Digest(der,n,hash,&hlen,EVP_sha256(),NULL)==1 && hlen==32) {
      unsigned char base64[48];
      int len=EVP_EncodeBlock(base64,hash,32);
      if(len==44) out=strdup((char*)base64);
   }
   OPENSSL_free(der);EVP_PKEY_free(key);X509_free(cert);
   return out;
}
