#include "remote_crypto.h"
#include <mbedtls/ecdh.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/ccm.h>
#include <mbedtls/platform_util.h>
#include <cstring>
#if defined(WARP_REMOTE_CRYPTO_HOST) || defined(TESLA_BLE_MODULE_HOST_TEST)
#include <sys/random.h>
#else
#include <esp_random.h>
#endif
namespace {
int random_bytes(void *,unsigned char *out,size_t n){
#if defined(WARP_REMOTE_CRYPTO_HOST) || defined(TESLA_BLE_MODULE_HOST_TEST)
 while(n){ssize_t rc=getrandom(out,n,0);if(rc<=0)return -1;out+=rc;n-=static_cast<size_t>(rc);}
#else
 esp_fill_random(out,n);
#endif
 return 0;
}
struct P256 {
 mbedtls_ecp_group group;mbedtls_mpi scalar,result;mbedtls_ecp_point point;
 P256(){mbedtls_ecp_group_init(&group);mbedtls_mpi_init(&scalar);mbedtls_mpi_init(&result);mbedtls_ecp_point_init(&point);}
 ~P256(){mbedtls_ecp_point_free(&point);mbedtls_mpi_free(&result);mbedtls_mpi_free(&scalar);mbedtls_ecp_group_free(&group);}
 bool secret(const uint8_t *sk){return sk&&mbedtls_ecp_group_load(&group,MBEDTLS_ECP_DP_SECP256R1)==0&&mbedtls_mpi_read_binary(&scalar,sk,32)==0&&mbedtls_ecp_check_privkey(&group,&scalar)==0;}
};
}
bool wr_crypto_public(uint8_t out[64],const uint8_t secret[32]){
 if(!out){return false;}
 mbedtls_platform_zeroize(out,64);
 P256 ctx;uint8_t wire[65];size_t n=0;
 bool ok=ctx.secret(secret)&&mbedtls_ecp_mul(&ctx.group,&ctx.point,&ctx.scalar,&ctx.group.G,random_bytes,nullptr)==0&&
  mbedtls_ecp_point_write_binary(&ctx.group,&ctx.point,MBEDTLS_ECP_PF_UNCOMPRESSED,&n,wire,sizeof(wire))==0&&n==65;
 if(ok)memcpy(out,wire+1,64);
 mbedtls_platform_zeroize(wire,sizeof(wire));return ok;
}
bool wr_crypto_shared(uint8_t out[32],const uint8_t secret[32],const uint8_t peer[64]){
 if(!out){return false;}
 mbedtls_platform_zeroize(out,32);if(!peer)return false;
 P256 ctx;uint8_t wire[65];wire[0]=4;memcpy(wire+1,peer,64);
 bool ok=ctx.secret(secret)&&mbedtls_ecp_point_read_binary(&ctx.group,&ctx.point,wire,sizeof(wire))==0&&
  mbedtls_ecp_check_pubkey(&ctx.group,&ctx.point)==0&&mbedtls_ecdh_compute_shared(&ctx.group,&ctx.result,&ctx.point,&ctx.scalar,random_bytes,nullptr)==0&&
  mbedtls_mpi_write_binary(&ctx.result,out,32)==0;
 if(!ok)mbedtls_platform_zeroize(out,32);
 mbedtls_platform_zeroize(wire,sizeof(wire));return ok;
}
bool wr_crypto_hkdf(uint8_t *out,size_t size,const uint8_t *key,size_t key_size,const uint8_t *salt,size_t salt_size,const uint8_t *info,size_t info_size){
 if(!out||!size||size>255*32||!key||!key_size||(!salt&&salt_size)||(!info&&info_size))return false;
 bool ok=mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),salt,salt_size,key,key_size,info,info_size,out,size)==0;
 if(!ok){mbedtls_platform_zeroize(out,size);}
 return ok;
}
bool wr_crypto_seal(uint8_t *cipher,uint8_t tag[16],const uint8_t key[32],const uint8_t nonce[13],const uint8_t *aad,size_t aad_size,const uint8_t *plain,size_t size){
 if(!cipher||!tag||!key||!nonce||!plain||!size||size>512||(!aad&&aad_size))return false;
 mbedtls_ccm_context ctx;mbedtls_ccm_init(&ctx);
 bool ok=mbedtls_ccm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,key,256)==0&&mbedtls_ccm_encrypt_and_tag(&ctx,size,nonce,13,aad,aad_size,plain,cipher,tag,16)==0;
 mbedtls_ccm_free(&ctx);if(!ok){mbedtls_platform_zeroize(cipher,size);mbedtls_platform_zeroize(tag,16);}return ok;
}
bool wr_crypto_open(uint8_t *plain,const uint8_t key[32],const uint8_t nonce[13],const uint8_t *aad,size_t aad_size,const uint8_t *cipher,size_t size,const uint8_t tag[16]){
 if(!plain||!key||!nonce||!cipher||!tag||!size||size>512||(!aad&&aad_size))return false;
 mbedtls_ccm_context ctx;mbedtls_ccm_init(&ctx);
 bool ok=mbedtls_ccm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,key,256)==0&&mbedtls_ccm_auth_decrypt(&ctx,size,nonce,13,aad,aad_size,cipher,plain,tag,16)==0;
 mbedtls_ccm_free(&ctx);if(!ok)mbedtls_platform_zeroize(plain,size);return ok;
}
