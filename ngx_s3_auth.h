/* S3 V4 Signature implementation
 *
 * This file contains the modularized source code for accepting a given HTTP
 * request as ngx_http_request_t and modifiying it to introduce the
 * Authorization header in compliance with the S3 V4 spec. The IAM access
 * key and the signing key (not to be confused with the secret key, see ./tools/s3-auth-gen) along
 * with it's scope are taken as inputs.
 *
 * The actual nginx module binding code is not present in this file. This file
 * is meant to serve as an "S3 Signing SDK for nginx".
 *
 * Maintainer/contributor rules
 *
 * (1) All functions here need to be static and inline.
 * (2) Every function must have it's own set of unit tests.
 * (3) The code must be written in a thread-safe manner. This is usually not
 *     a problem with standard nginx functions. However, care must be taken
 *     when using very old C functions such as strtok, gmtime, etc. etc.
 *     Always use the _r variants of such functions
 * (4) All heap allocation must be done using ngx_pool_t instead of malloc
 */

#ifndef __NGX_S3_AUTH__INTERNAL__H__
#define __NGX_S3_AUTH__INTERNAL__H__

#include <time.h>
#include <ngx_times.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "ngx_s3_auth_crypto.h"

typedef ngx_keyval_t header_pair_t;

struct S3CanonicalRequestDetails {
  ngx_str_t *canonical_request;
  ngx_str_t *signed_header_names;
  ngx_array_t *header_list;
};

struct S3CanonicalHeaderDetails {
  ngx_str_t *canonical_header_str;
  ngx_str_t *signed_header_names;
  ngx_array_t *header_list;
};

struct S3SignedRequestDetails {
  const ngx_str_t *signature;
  const ngx_str_t *signed_header_names;
  ngx_array_t *header_list;
};

static const ngx_str_t EMPTY_STRING_SHA256 = ngx_string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
static const ngx_str_t EMPTY_STRING = ngx_null_string;
static const ngx_str_t HASH_HEADER = ngx_string("x-amz-content-sha256");
static const ngx_str_t DATE_HEADER = ngx_string("x-amz-date");
static const ngx_str_t HOST_HEADER = ngx_string("host");
static const ngx_str_t AUTHZ_HEADER = ngx_string("Authorization");

static inline char* __CHAR_PTR_U(u_char* ptr) { return (char*) ptr; }
static inline const char* __CONST_CHAR_PTR_U(const u_char* ptr) { return (const char*) ptr; }

static inline const ngx_str_t* ngx_s3_auth__compute_request_time(ngx_pool_t *pool, const time_t *timep) {
  int size = 18; // len(date string + \0)
  const char fmt[] = "%Y%m%dT%H%M%SZ";

  ngx_str_t *const t = ngx_palloc(pool, sizeof(ngx_str_t));
  struct tm *tm_p = ngx_palloc(pool, sizeof(struct tm));
  gmtime_r(timep, tm_p);

  t->data = ngx_palloc(pool, size);
  t->len = strftime(__CHAR_PTR_U(t->data), size - 1, fmt, tm_p);

  // If the length of the resulting C string, including the terminating null-character,
  // doesn't exceed maxsize, the function returns the total number of characters copied
  // to ptr (not including the terminating null-character).
  // Otherwise, it returns zero, and the contents of the array pointed by ptr are indeterminate.
  // https://www.cplusplus.com/reference/ctime/strftime/
  while (t->len == 0) {
    ngx_pfree(pool, t->data);
    size *= 2;
    t->data = ngx_palloc(pool, size);
    t->len = strftime(__CHAR_PTR_U(t->data), size - 1, fmt, tm_p);
  }

  return t;
}

static inline int ngx_s3_auth__cmp_hnames(const void *one, const void *two) {
  header_pair_t *first, *second;
  int ret;

  first  = (header_pair_t *) one;
  second = (header_pair_t *) two;
  ret = ngx_strncmp(first->key.data, second->key.data, ngx_min(first->key.len, second->key.len));

  if (ret != 0){
    return ret;
  } else {
    return (first->key.len - second->key.len);
  }
}

static inline const ngx_str_t* ngx_s3_auth__canonize_query_string(ngx_pool_t *pool,
                                                                  const ngx_http_request_t *req) {
  u_char *p, *ampersand, *equal, *last;
  size_t i, len;
  ngx_str_t *qs = ngx_palloc(pool, sizeof(ngx_str_t));

  header_pair_t *qs_arg;
  ngx_array_t *query_string_args = ngx_array_create(pool, 0, sizeof(header_pair_t));

  if (req->args.len == 0) {
    return &EMPTY_STRING;
  }

  p = req->args.data;
  last = p + req->args.len;

  for (; p < last; p++) {
    qs_arg = ngx_array_push(query_string_args);

    ampersand = ngx_strlchr(p, last, '&');
    if (ampersand == NULL) {
      ampersand = last;
    }

    equal = ngx_strlchr(p, last, '=');
    if ((equal == NULL) || (equal > ampersand)) {
      equal = ampersand;
    }

    len = equal - p;
    qs_arg->key.data = ngx_palloc(pool, len*3);
    qs_arg->key.len = (u_char *)ngx_escape_uri(qs_arg->key.data, p, len, NGX_ESCAPE_ARGS) - qs_arg->key.data;


    len = ampersand - equal;
    if(len > 0 ) {
      qs_arg->value.data = ngx_palloc(pool, len*3);
      qs_arg->value.len = (u_char *)ngx_escape_uri(qs_arg->value.data, equal+1, len-1, NGX_ESCAPE_ARGS) - qs_arg->value.data;
    } else {
      qs_arg->value = EMPTY_STRING;
    }

    p = ampersand;
  }

  ngx_qsort(query_string_args->elts, (size_t) query_string_args->nelts,
            sizeof(header_pair_t), ngx_s3_auth__cmp_hnames);

  qs->data = ngx_palloc(pool, req->args.len*3 + query_string_args->nelts*2);
  qs->len = 0;

  for(i = 0; i < query_string_args->nelts; i++) {
    qs_arg = &((header_pair_t*)query_string_args->elts)[i];

    ngx_memcpy(qs->data + qs->len, qs_arg->key.data, qs_arg->key.len);
    qs->len += qs_arg->key.len;

    *(qs->data + qs->len) = '=';
    qs->len++;

    ngx_memcpy(qs->data + qs->len, qs_arg->value.data, qs_arg->value.len);
    qs->len += qs_arg->value.len;

    *(qs->data + qs->len) = '&';
    qs->len++;
  }
  qs->len--;

  return qs;
}

static inline struct S3CanonicalHeaderDetails ngx_s3_auth__canonize_headers(ngx_pool_t *pool,
                                                                            const ngx_http_request_t *req,
                                                                            const ngx_str_t *date,
                                                                            const ngx_str_t *content_hash,
                                                                            const ngx_str_t *s3_endpoint) {
  size_t header_names_size = 1, header_nameval_size = 1;
  size_t i, used;
  u_char *buf_progress;
  struct S3CanonicalHeaderDetails header_details;

  ngx_array_t *settable_header_array = ngx_array_create(pool, 3, sizeof(header_pair_t));
  header_pair_t *header_ptr;

  header_ptr = ngx_array_push(settable_header_array);
  header_ptr->key = HASH_HEADER;
  header_ptr->value = *content_hash;

  header_ptr = ngx_array_push(settable_header_array);
  header_ptr->key = DATE_HEADER;
  header_ptr->value = *date;

  header_ptr = ngx_array_push(settable_header_array);
  header_ptr->key = HOST_HEADER;
  header_ptr->value.len = s3_endpoint->len;
  header_ptr->value.data = ngx_palloc(pool, header_ptr->value.len);
  header_ptr->value.len = ngx_snprintf(
    header_ptr->value.data,
    header_ptr->value.len,
    "%V",
    s3_endpoint) - header_ptr->value.data;

  ngx_qsort(
    settable_header_array->elts,
    (size_t) settable_header_array->nelts,
    sizeof(header_pair_t),
    ngx_s3_auth__cmp_hnames);
  header_details.header_list = settable_header_array;

  for(i = 0; i < settable_header_array->nelts; i++) {
    header_names_size += ((header_pair_t*) settable_header_array->elts)[i].key.len + 1;     // :
    header_nameval_size += ((header_pair_t*) settable_header_array->elts)[i].key.len + 1;   // :
    header_nameval_size += ((header_pair_t*) settable_header_array->elts)[i].value.len + 2; // \r\n
  }

  //

  header_details.canonical_header_str = ngx_palloc(pool, sizeof(ngx_str_t));
  header_details.canonical_header_str->data = ngx_palloc(pool, header_nameval_size);

  for(i = 0, used = 0, buf_progress = header_details.canonical_header_str->data;
      i < settable_header_array->nelts;
      i++, used = buf_progress - header_details.canonical_header_str->data) {
    buf_progress = ngx_snprintf(buf_progress, header_nameval_size - used, "%V:%V\n",
                                & ((header_pair_t*) settable_header_array->elts)[i].key,
                                & ((header_pair_t*) settable_header_array->elts)[i].value);
  }
  header_details.canonical_header_str->len = used;

  //

  header_details.signed_header_names = ngx_palloc(pool, sizeof(ngx_str_t));
  header_details.signed_header_names->data = ngx_palloc(pool, header_names_size);

  for(i = 0, used = 0, buf_progress = header_details.signed_header_names->data;
      i < settable_header_array->nelts;
      i++, used = buf_progress - header_details.signed_header_names->data) {
    buf_progress = ngx_snprintf(buf_progress, header_names_size - used, "%V;",
                                & ((header_pair_t*) settable_header_array->elts)[i].key);
  }

  used--;
  header_details.signed_header_names->len = used;
  header_details.signed_header_names->data[used] = 0;

  return header_details;
}

static inline const ngx_str_t* ngx_s3_auth__request_body_hash(ngx_pool_t *pool,
                                                              const ngx_http_request_t *req) {
  /* TODO: support cases involving non-empty body */
  return &EMPTY_STRING_SHA256;
}

// S3 wants a peculiar kind of URI-encoding: they want RFC 3986, except that
// slashes shouldn't be encoded...
// this function is a light wrapper around ngx_escape_uri that does exactly that
// modifies the source in place if it needs to be escaped
// see http://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
static inline void ngx_s3_auth__escape_uri(ngx_pool_t *pool, ngx_str_t* src) {
  u_char *escaped_data;
  u_int escaped_data_len, escaped_data_with_slashes_len, i, j;
  uintptr_t escaped_count, slashes_count = 0;

  // first, we need to know how many characters need to be escaped
  escaped_count = ngx_escape_uri(NULL, src->data, src->len, NGX_ESCAPE_URI_COMPONENT);
  // except slashes should not be escaped...
  if (escaped_count > 0) {
    for (i = 0; i < src->len; i++) {
      if (src->data[i] == '/') {
        slashes_count++;
      }
    }
  }

  if (escaped_count == slashes_count) {
    // nothing to do! nothing but slashes escaped (if even that)
    return;
  }

  // each escaped character is replaced by 3 characters
  escaped_data_len = src->len + escaped_count * 2;
  escaped_data = ngx_palloc(pool, escaped_data_len);
  ngx_escape_uri(escaped_data, src->data, src->len, NGX_ESCAPE_URI_COMPONENT);

  // now we need to go back and re-replace each occurrence of %2F with a slash
  escaped_data_with_slashes_len = src->len + (escaped_count - slashes_count) * 2;
  if (slashes_count > 0) {
    for (i = 0, j = 0; i < escaped_data_with_slashes_len; i++) {
      if (j < escaped_data_len - 2 && strncmp((char*) (escaped_data + j), "%2F", 3) == 0) {
        escaped_data[i] = '/';
        j += 3;
      } else {
        escaped_data[i] = escaped_data[j];
        j++;
      }
    }

    src->len = escaped_data_with_slashes_len;
  } else {
    // no slashes
    src->len = escaped_data_len;
  }

  src->data = escaped_data;
}

static inline const ngx_str_t* ngx_s3_auth__canonical_url(ngx_pool_t *pool, const ngx_http_request_t *req) {
  ngx_str_t *url;
  const u_char *req_uri_data;
  u_int req_uri_len;

  if(req->args.len == 0) {
    req_uri_data = req->uri.data;
    req_uri_len = req->uri.len;
  } else {
    req_uri_data = req->uri_start;
    req_uri_len = req->args_start - req->uri_start - 1;
  }

  // we need to copy that data to not modify the request for other modules
  url = ngx_palloc(pool, sizeof(ngx_str_t));
  url->data = ngx_palloc(pool, req_uri_len);
  ngx_memcpy(url->data, req_uri_data, req_uri_len);
  url->len = req_uri_len;

  // then URI-encode it per RFC 3986
  ngx_s3_auth__escape_uri(pool, url);

  return url;
}

static inline struct S3CanonicalRequestDetails ngx_s3_auth__make_canonical_request(ngx_pool_t *pool,
                                                                                   const ngx_http_request_t *req,
                                                                                   const ngx_str_t *date,
                                                                                   const ngx_str_t *s3_endpoint) {
  struct S3CanonicalRequestDetails req_details;
  const ngx_str_t *canonical_qs = ngx_s3_auth__canonize_query_string(pool, req);
  const ngx_str_t *request_body_hash = ngx_s3_auth__request_body_hash(pool, req);
  const struct S3CanonicalHeaderDetails canonical_headers = ngx_s3_auth__canonize_headers(
    pool,
    req,
    date,
    request_body_hash,
    s3_endpoint);
  req_details.signed_header_names = canonical_headers.signed_header_names;

  const ngx_str_t *http_method = &(req->method_name);
  const ngx_str_t *url = ngx_s3_auth__canonical_url(pool, req);

  req_details.canonical_request = ngx_palloc(pool, sizeof(ngx_str_t));
  // length depends on format string you could find below
  req_details.canonical_request->len = http_method->len + 1
                                           + url->len + 1
                                           + canonical_qs->len + 1
                                           + canonical_headers.canonical_header_str->len + 1
                                           + canonical_headers.signed_header_names->len + 1
                                           + request_body_hash->len;
  req_details.canonical_request->data = ngx_palloc(pool, req_details.canonical_request->len);
  req_details.canonical_request->len = ngx_snprintf(
    req_details.canonical_request->data, req_details.canonical_request->len,
    "%V\n%V\n%V\n%V\n%V\n%V",
    http_method, url, canonical_qs, canonical_headers.canonical_header_str,
    canonical_headers.signed_header_names, request_body_hash) - req_details.canonical_request->data;
  req_details.header_list = canonical_headers.header_list;

  return req_details;
}

static inline const ngx_str_t* ngx_s3_auth__string_to_sign(ngx_pool_t *pool,
                                                           const ngx_str_t *key_scope,
                                                           const ngx_str_t *date,
                                                           const ngx_str_t *canonical_request_hash) {
  const char fmt[] = "AWS4-HMAC-SHA256\n%V\n%V\n%V";
  ngx_str_t *subject = ngx_palloc(pool, sizeof(ngx_str_t));

  subject->len = date->len
                     + key_scope->len
                     + canonical_request_hash->len
                     + sizeof(fmt);
  subject->data = ngx_palloc(pool, subject->len);
  subject->len = ngx_snprintf(
    subject->data, subject->len, fmt,
    date, key_scope, canonical_request_hash) - subject->data ;

  return subject;
}

static inline const ngx_str_t* ngx_s3_auth__make_auth_token(ngx_pool_t *pool,
                                                            const ngx_str_t *signature,
                                                            const ngx_str_t *signed_header_names,
                                                            const ngx_str_t *access_key_id,
                                                            const ngx_str_t *key_scope) {

  const char fmt[] = "AWS4-HMAC-SHA256 Credential=%V/%V,SignedHeaders=%V,Signature=%V";
  ngx_str_t *authz;

  authz = ngx_palloc(pool, sizeof(ngx_str_t));
  authz->len = access_key_id->len
                   + key_scope->len
                   + signed_header_names->len
                   + signature->len
                   + sizeof(fmt);
  authz->data = ngx_palloc(pool, authz->len);
  authz->len = ngx_snprintf(
    authz->data, authz->len, fmt,
    access_key_id, key_scope, signed_header_names, signature) - authz->data;
  return authz;
}

static inline struct S3SignedRequestDetails ngx_s3_auth__compute_signature(ngx_pool_t *pool,
                                                                           ngx_http_request_t *req,
                                                                           const ngx_str_t *signing_key,
                                                                           const ngx_str_t *key_scope,
                                                                           const ngx_str_t *s3_endpoint) {
  struct S3SignedRequestDetails req_details;

  const ngx_str_t *date = ngx_s3_auth__compute_request_time(pool, &req->start_sec);
  const struct S3CanonicalRequestDetails canonical_request = ngx_s3_auth__make_canonical_request(pool, req, date, s3_endpoint);
  const ngx_str_t *canonical_request_hash = ngx_s3_auth__hash_sha256(pool, canonical_request.canonical_request);
  const ngx_str_t *string_to_sign = ngx_s3_auth__string_to_sign(pool, key_scope, date, canonical_request_hash);
  const ngx_str_t *signature = ngx_s3_auth__sign_sha256_hex(pool, string_to_sign, signing_key);

  req_details.signature = signature;
  req_details.signed_header_names = canonical_request.signed_header_names;
  req_details.header_list = canonical_request.header_list;

  return req_details;
}

// list of header_pair_t
static inline const ngx_array_t* ngx_s3_auth__sign(ngx_pool_t *pool, ngx_http_request_t *req,
                                                   const ngx_str_t *access_key_id,
                                                   const ngx_str_t *signing_key,
                                                   const ngx_str_t *key_scope,
                                                   const ngx_str_t *s3_endpoint) {
  const struct S3SignedRequestDetails signature_details =
      ngx_s3_auth__compute_signature(pool, req, signing_key, key_scope, s3_endpoint);

  const ngx_str_t *auth_header_value = ngx_s3_auth__make_auth_token(
    pool, signature_details.signature,
    signature_details.signed_header_names, access_key_id, key_scope);

  header_pair_t *header_ptr;
  header_ptr = ngx_array_push(signature_details.header_list);
  header_ptr->key = AUTHZ_HEADER;
  header_ptr->value = *auth_header_value;

  return signature_details.header_list;
}

#endif
