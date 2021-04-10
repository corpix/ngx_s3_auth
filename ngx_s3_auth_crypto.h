#ifndef __NGX_S3_AUTH_CRYPTO__
#define __NGX_S3_AUTH_CRYPTO__

#include <ngx_core.h>
#include <ngx_palloc.h>

ngx_str_t* ngx_s3_auth__hash_sha256(ngx_pool_t *pool, const ngx_str_t *blob);
ngx_str_t* ngx_s3_auth__sign_sha256_hex(ngx_pool_t *pool, const ngx_str_t *blob, const ngx_str_t *signing_key);

#endif
