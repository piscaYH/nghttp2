/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2015 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "nghttp2_http.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>

static int memeq(const void *a, const void *b, size_t n) {
  return memcmp(a, b, n) == 0;
}

#define streq(A, B, N) ((sizeof((A)) - 1) == (N) && memeq((A), (B), (N)))

static char downcase(char c) {
  return 'A' <= c && c <= 'Z' ? (c - 'A' + 'a') : c;
}

static int memieq(const void *a, const void *b, size_t n) {
  size_t i;
  const uint8_t *aa = a, *bb = b;

  for (i = 0; i < n; ++i) {
    if (downcase(aa[i]) != downcase(bb[i])) {
      return 0;
    }
  }
  return 1;
}

#define strieq(A, B, N) ((sizeof((A)) - 1) == (N) && memieq((A), (B), (N)))

typedef enum {
  NGHTTP2_TOKEN__AUTHORITY,
  NGHTTP2_TOKEN__METHOD,
  NGHTTP2_TOKEN__PATH,
  NGHTTP2_TOKEN__SCHEME,
  NGHTTP2_TOKEN__STATUS,
  NGHTTP2_TOKEN_CONNECTION,
  NGHTTP2_TOKEN_CONTENT_LENGTH,
  NGHTTP2_TOKEN_HOST,
  NGHTTP2_TOKEN_KEEP_ALIVE,
  NGHTTP2_TOKEN_PROXY_CONNECTION,
  NGHTTP2_TOKEN_TE,
  NGHTTP2_TOKEN_TRANSFER_ENCODING,
  NGHTTP2_TOKEN_UPGRADE,
  NGHTTP2_TOKEN_MAXIDX,
} nghttp2_token;

/*
 * This function was generated by genlibtokenlookup.py.  Inspired by
 * h2o header lookup.  https://github.com/h2o/h2o
 */
static int lookup_token(const uint8_t *name, size_t namelen) {
  switch (namelen) {
  case 2:
    switch (name[namelen - 1]) {
    case 'e':
      if (streq("t", name, 1)) {
        return NGHTTP2_TOKEN_TE;
      }
      break;
    }
    break;
  case 4:
    switch (name[namelen - 1]) {
    case 't':
      if (streq("hos", name, 3)) {
        return NGHTTP2_TOKEN_HOST;
      }
      break;
    }
    break;
  case 5:
    switch (name[namelen - 1]) {
    case 'h':
      if (streq(":pat", name, 4)) {
        return NGHTTP2_TOKEN__PATH;
      }
      break;
    }
    break;
  case 7:
    switch (name[namelen - 1]) {
    case 'd':
      if (streq(":metho", name, 6)) {
        return NGHTTP2_TOKEN__METHOD;
      }
      break;
    case 'e':
      if (streq(":schem", name, 6)) {
        return NGHTTP2_TOKEN__SCHEME;
      }
      if (streq("upgrad", name, 6)) {
        return NGHTTP2_TOKEN_UPGRADE;
      }
      break;
    case 's':
      if (streq(":statu", name, 6)) {
        return NGHTTP2_TOKEN__STATUS;
      }
      break;
    }
    break;
  case 10:
    switch (name[namelen - 1]) {
    case 'e':
      if (streq("keep-aliv", name, 9)) {
        return NGHTTP2_TOKEN_KEEP_ALIVE;
      }
      break;
    case 'n':
      if (streq("connectio", name, 9)) {
        return NGHTTP2_TOKEN_CONNECTION;
      }
      break;
    case 'y':
      if (streq(":authorit", name, 9)) {
        return NGHTTP2_TOKEN__AUTHORITY;
      }
      break;
    }
    break;
  case 14:
    switch (name[namelen - 1]) {
    case 'h':
      if (streq("content-lengt", name, 13)) {
        return NGHTTP2_TOKEN_CONTENT_LENGTH;
      }
      break;
    }
    break;
  case 16:
    switch (name[namelen - 1]) {
    case 'n':
      if (streq("proxy-connectio", name, 15)) {
        return NGHTTP2_TOKEN_PROXY_CONNECTION;
      }
      break;
    }
    break;
  case 17:
    switch (name[namelen - 1]) {
    case 'g':
      if (streq("transfer-encodin", name, 16)) {
        return NGHTTP2_TOKEN_TRANSFER_ENCODING;
      }
      break;
    }
    break;
  }
  return -1;
}

static int64_t parse_uint(const uint8_t *s, size_t len) {
  int64_t n = 0;
  size_t i;
  if (len == 0) {
    return -1;
  }
  for (i = 0; i < len; ++i) {
    if ('0' <= s[i] && s[i] <= '9') {
      if (n > INT64_MAX / 10) {
        return -1;
      }
      n *= 10;
      if (n > INT64_MAX - (s[i] - '0')) {
        return -1;
      }
      n += s[i] - '0';
      continue;
    }
    return -1;
  }
  return n;
}

static int lws(const uint8_t *s, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) {
    if (s[i] != ' ' && s[i] != '\t') {
      return 0;
    }
  }
  return 1;
}

static int check_pseudo_header(nghttp2_stream *stream, const nghttp2_nv *nv,
                               int flag) {
  if (stream->http_flags & flag) {
    return 0;
  }
  if (lws(nv->value, nv->valuelen)) {
    return 0;
  }
  stream->http_flags |= flag;
  return 1;
}

static int expect_response_body(nghttp2_stream *stream) {
  return (stream->http_flags & NGHTTP2_HTTP_FLAG_METH_HEAD) == 0 &&
         stream->status_code / 100 != 1 && stream->status_code != 304 &&
         stream->status_code != 204;
}

static int http_request_on_header(nghttp2_stream *stream, nghttp2_nv *nv,
                                  int trailer) {
  int token;

  if (nv->name[0] == ':') {
    if (trailer ||
        (stream->http_flags & NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
  }

  token = lookup_token(nv->name, nv->namelen);

  switch (token) {
  case NGHTTP2_TOKEN__AUTHORITY:
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG__AUTHORITY)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  case NGHTTP2_TOKEN__METHOD:
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG__METHOD)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    if (streq("HEAD", nv->value, nv->valuelen)) {
      stream->http_flags |= NGHTTP2_HTTP_FLAG_METH_HEAD;
    } else if (streq("CONNECT", nv->value, nv->valuelen)) {
      if (stream->stream_id % 2 == 0) {
        /* we won't allow CONNECT for push */
        return NGHTTP2_ERR_HTTP_HEADER;
      }
      stream->http_flags |= NGHTTP2_HTTP_FLAG_METH_CONNECT;
      if (stream->http_flags &
          (NGHTTP2_HTTP_FLAG__PATH | NGHTTP2_HTTP_FLAG__SCHEME)) {
        return NGHTTP2_ERR_HTTP_HEADER;
      }
    }
    break;
  case NGHTTP2_TOKEN__PATH:
    if (stream->http_flags & NGHTTP2_HTTP_FLAG_METH_CONNECT) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG__PATH)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  case NGHTTP2_TOKEN__SCHEME:
    if (stream->http_flags & NGHTTP2_HTTP_FLAG_METH_CONNECT) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG__SCHEME)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  case NGHTTP2_TOKEN_HOST:
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG_HOST)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  case NGHTTP2_TOKEN_CONTENT_LENGTH: {
    if (stream->content_length != -1) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    stream->content_length = parse_uint(nv->value, nv->valuelen);
    if (stream->content_length == -1) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  }
  /* disallowed header fields */
  case NGHTTP2_TOKEN_CONNECTION:
  case NGHTTP2_TOKEN_KEEP_ALIVE:
  case NGHTTP2_TOKEN_PROXY_CONNECTION:
  case NGHTTP2_TOKEN_TRANSFER_ENCODING:
  case NGHTTP2_TOKEN_UPGRADE:
    return NGHTTP2_ERR_HTTP_HEADER;
  case NGHTTP2_TOKEN_TE:
    if (!strieq("trailers", nv->value, nv->valuelen)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  default:
    if (nv->name[0] == ':') {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
  }

  if (nv->name[0] != ':') {
    stream->http_flags |= NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED;
  }

  return 0;
}

static int http_response_on_header(nghttp2_stream *stream, nghttp2_nv *nv,
                                   int trailer) {
  int token;

  if (nv->name[0] == ':') {
    if (trailer ||
        (stream->http_flags & NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
  }

  token = lookup_token(nv->name, nv->namelen);

  switch (token) {
  case NGHTTP2_TOKEN__STATUS: {
    if (!check_pseudo_header(stream, nv, NGHTTP2_HTTP_FLAG__STATUS)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    if (nv->valuelen != 3) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    stream->status_code = parse_uint(nv->value, nv->valuelen);
    if (stream->status_code == -1) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  }
  case NGHTTP2_TOKEN_CONTENT_LENGTH: {
    if (stream->content_length != -1) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    stream->content_length = parse_uint(nv->value, nv->valuelen);
    if (stream->content_length == -1) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  }
  /* disallowed header fields */
  case NGHTTP2_TOKEN_CONNECTION:
  case NGHTTP2_TOKEN_KEEP_ALIVE:
  case NGHTTP2_TOKEN_PROXY_CONNECTION:
  case NGHTTP2_TOKEN_TRANSFER_ENCODING:
  case NGHTTP2_TOKEN_UPGRADE:
    return NGHTTP2_ERR_HTTP_HEADER;
  case NGHTTP2_TOKEN_TE:
    if (!strieq("trailers", nv->value, nv->valuelen)) {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    break;
  default:
    if (nv->name[0] == ':') {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
  }

  if (nv->name[0] != ':') {
    stream->http_flags |= NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED;
  }

  return 0;
}

int nghttp2_http_on_header(nghttp2_session *session, nghttp2_stream *stream,
                           nghttp2_frame *frame, nghttp2_nv *nv, int trailer) {
  /* We are strict for pseudo header field.  One bad character should
     lead to fail.  OTOH, we should be a bit forgiving for regular
     headers, since existing public internet has so much illegal
     headers floating around and if we kill the stream because of
     this, we may disrupt many web sites and/or libraries.  So we
     become conservative here, and just ignore those illegal regular
     headers. */
  if (!nghttp2_check_header_name(nv->name, nv->namelen)) {
    size_t i;
    if (nv->namelen > 0 && nv->name[0] == ':') {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    /* header field name must be lower-cased without exception */
    for (i = 0; i < nv->namelen; ++i) {
      char c = nv->name[i];
      if ('A' <= c && c <= 'Z') {
        return NGHTTP2_ERR_HTTP_HEADER;
      }
    }
    /* When ignoring regular headers, we set this flag so that we
       still enforce header field ordering rule for pseudo header
       fields. */
    stream->http_flags |= NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED;
    return NGHTTP2_ERR_IGN_HTTP_HEADER;
  }

  if (!nghttp2_check_header_value(nv->value, nv->valuelen)) {
    assert(nv->namelen > 0);
    if (nv->name[0] == ':') {
      return NGHTTP2_ERR_HTTP_HEADER;
    }
    /* When ignoring regular headers, we set this flag so that we
       still enforce header field ordering rule for pseudo header
       fields. */
    stream->http_flags |= NGHTTP2_HTTP_FLAG_PSEUDO_HEADER_DISALLOWED;
    return NGHTTP2_ERR_IGN_HTTP_HEADER;
  }

  if (session->server || frame->hd.type == NGHTTP2_PUSH_PROMISE) {
    return http_request_on_header(stream, nv, trailer);
  }

  return http_response_on_header(stream, nv, trailer);
}

int nghttp2_http_on_request_headers(nghttp2_stream *stream,
                                    nghttp2_frame *frame) {
  if (stream->http_flags & NGHTTP2_HTTP_FLAG_METH_CONNECT) {
    if ((stream->http_flags & NGHTTP2_HTTP_FLAG__AUTHORITY) == 0) {
      return -1;
    }
    stream->content_length = -1;
  } else if ((stream->http_flags & NGHTTP2_HTTP_FLAG_REQ_HEADERS) !=
                 NGHTTP2_HTTP_FLAG_REQ_HEADERS ||
             (stream->http_flags &
              (NGHTTP2_HTTP_FLAG__AUTHORITY | NGHTTP2_HTTP_FLAG_HOST)) == 0) {
    return -1;
  }

  if (frame->hd.type == NGHTTP2_PUSH_PROMISE) {
    /* we are going to reuse data fields for upcoming response.  Clear
       them now, except for method flags. */
    stream->http_flags &= NGHTTP2_HTTP_FLAG_METH_ALL;
    stream->content_length = -1;
  }

  return 0;
}

int nghttp2_http_on_response_headers(nghttp2_stream *stream) {
  if ((stream->http_flags & NGHTTP2_HTTP_FLAG__STATUS) == 0) {
    return -1;
  }

  if (stream->status_code / 100 == 1) {
    /* non-final response */
    stream->http_flags = (stream->http_flags & NGHTTP2_HTTP_FLAG_METH_ALL) |
                         NGHTTP2_HTTP_FLAG_EXPECT_FINAL_RESPONSE;
    stream->content_length = -1;
    stream->status_code = -1;
    return 0;
  }

  stream->http_flags &= ~NGHTTP2_HTTP_FLAG_EXPECT_FINAL_RESPONSE;

  if (!expect_response_body(stream)) {
    stream->content_length = 0;
  } else if (stream->http_flags & NGHTTP2_HTTP_FLAG_METH_CONNECT) {
    stream->content_length = -1;
  }

  return 0;
}

int nghttp2_http_on_trailer_headers(nghttp2_stream *stream _U_,
                                    nghttp2_frame *frame) {
  if ((frame->hd.flags & NGHTTP2_FLAG_END_STREAM) == 0) {
    return -1;
  }

  return 0;
}

int nghttp2_http_on_remote_end_stream(nghttp2_stream *stream) {
  if (stream->http_flags & NGHTTP2_HTTP_FLAG_EXPECT_FINAL_RESPONSE) {
    return -1;
  }

  if (stream->content_length != -1 &&
      stream->content_length != stream->recv_content_length) {
    return -1;
  }

  return 0;
}

int nghttp2_http_on_data_chunk(nghttp2_stream *stream, size_t n) {
  stream->recv_content_length += n;

  if ((stream->http_flags & NGHTTP2_HTTP_FLAG_EXPECT_FINAL_RESPONSE) ||
      (stream->content_length != -1 &&
       stream->recv_content_length > stream->content_length)) {
    return -1;
  }

  return 0;
}

void nghttp2_http_record_request_method(nghttp2_stream *stream,
                                        nghttp2_frame *frame) {
  const nghttp2_nv *nva;
  size_t nvlen;
  size_t i;

  switch (frame->hd.type) {
  case NGHTTP2_HEADERS:
    nva = frame->headers.nva;
    nvlen = frame->headers.nvlen;
    break;
  case NGHTTP2_PUSH_PROMISE:
    nva = frame->push_promise.nva;
    nvlen = frame->push_promise.nvlen;
    break;
  default:
    return;
  }

  /* TODO we should do this strictly. */
  for (i = 0; i < nvlen; ++i) {
    const nghttp2_nv *nv = &nva[i];
    if (lookup_token(nv->name, nv->namelen) != NGHTTP2_TOKEN__METHOD) {
      continue;
    }
    if (streq("CONNECT", nv->value, nv->valuelen)) {
      stream->http_flags |= NGHTTP2_HTTP_FLAG_METH_CONNECT;
      return;
    }
    if (streq("HEAD", nv->value, nv->valuelen)) {
      stream->http_flags |= NGHTTP2_HTTP_FLAG_METH_HEAD;
      return;
    }
    return;
  }
}
