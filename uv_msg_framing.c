#include "uv_msg_framing.h"

#ifdef DEBUGTRACE
#define UVTRACE(X)   printf X;
#else
#define UVTRACE(X)
#endif


/* Message Writting **********************************************************/


void uv_msg_send_completed(uv_write_t *req, int status) {
   msg_write_req_t *msg_req = (msg_write_req_t*) req;

   UVTRACE(("send completed: %s\n", msg_req->buf[1].base));

   if ( msg_req->msg_write_cb ) {
      msg_req->msg_write_cb(msg_req->preq, status);
   }

   free(msg_req);
}

int uv_msg_send(uv_write_t *req, uv_stream_t* stream, void *msg, int size, uv_msg_write_cb msg_write_cb) {

   if ( !req || !stream || !msg || size <= 0 ) return UV_EINVAL;

   UVTRACE(("sending message: %s\n", msg));

   msg_write_req_t *msg_req = malloc(sizeof(msg_write_req_t));
   if ( !msg_req ) return UV_ENOBUFS;

   memcpy(&msg_req->req, req, sizeof(uv_write_t));
   msg_req->preq = req;  //  -- this is not required if we don't require the user to allocate the request.

   msg_req->msg_size = htonl(size);
   msg_req->buf[0].base = (char*) &msg_req->msg_size;
   msg_req->buf[0].len = 4;

   msg_req->buf[1] = uv_buf_init(msg, size);

   msg_req->msg_write_cb = msg_write_cb;

   return uv_write((uv_write_t*) msg_req, stream, &msg_req->buf[0], 2, uv_msg_send_completed);

}


/* Message Reading ***********************************************************/


void uv_stream_msg_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *stream_buf) {
   msg_buf_t *msg_buf = handle->data;

   UVTRACE(("stream_msg_alloc  msg_buf=%p\n", msg_buf));
   if( msg_buf==0 ) return;

   if( msg_buf->buf==0 ){
      uv_buf_t buf = {0};
      msg_buf->alloc_cb(handle, suggested_size, &buf);
      msg_buf->buf = buf.base;
      if( msg_buf->buf==0 ) return;
      msg_buf->alloc_size = buf.len;
   }

   UVTRACE(("stream_msg_alloc msg_buf->buf=%p  filled=%d\n", msg_buf->buf, msg_buf->filled));

   if( msg_buf->filled >= 4 ){
      int msg_size = ntohl(*(int*)msg_buf->buf);
      UVTRACE(("stream_msg_alloc  msg_size=%d\n", msg_size));
      if( msg_size + 4 > msg_buf->alloc_size ){
         uv_buf_t buf = {0};
         suggested_size = msg_size + 4;
         msg_buf->alloc_cb(handle, suggested_size, &buf);  // here the suggested size is exactly what it needs to read the message
         if( buf.base==0 || buf.len < suggested_size ) return;  // if buf.len < suggested_size and buf.base is valid it will be lost here (the allocated memory)
         memcpy(buf.base, msg_buf->buf, msg_buf->filled);
         if( msg_buf->free_cb ) msg_buf->free_cb(handle, msg_buf->buf);
         msg_buf->buf = buf.base;
         msg_buf->alloc_size = buf.len;
      }
      stream_buf->len = msg_size - msg_buf->filled;
   } else {
      stream_buf->len = msg_buf->alloc_size - msg_buf->filled;
   }

   stream_buf->base = msg_buf->buf + msg_buf->filled;
   UVTRACE(("stream_msg_alloc  base=%p  len=%d\n", stream_buf->base, stream_buf->len));
}

void uv_stream_msg_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
   msg_buf_t *msg_buf = stream->data;

   UVTRACE(("process_messages: received %d bytes\n", nread));
   UVTRACE(("msg_buf: %p\n", msg_buf));

   if( msg_buf==0 ) return;

   if (nread < 0) {
      msg_buf->msg_read_cb(stream, 0, nread);
      return;
   }

   msg_buf->filled += nread;

   UVTRACE(("alloc_size: %d, filled: %d\n", msg_buf->alloc_size, msg_buf->filled));

   char *ptr = msg_buf->buf;

   while( msg_buf->filled >= 4 ){
      int msg_size = ntohl(*(int*)ptr);
      int entire_msg = msg_size + 4;
      if( msg_buf->filled >= entire_msg ){
         msg_buf->msg_read_cb(stream, ptr + 4, msg_size);
         if( msg_buf->filled > entire_msg ){
            ptr += entire_msg;
         }
         msg_buf->filled -= entire_msg;
      }
   }

   if( ptr > msg_buf->buf && msg_buf->filled > 0 ){
      UVTRACE(("moving the buffer\n"));
      memmove(msg_buf->buf, ptr, msg_buf->filled);
   } else if( msg_buf->filled == 0 ){
      UVTRACE(("releasing the buffer\n"));
      if( msg_buf->free_cb ) msg_buf->free_cb((uv_handle_t*)stream, msg_buf->buf);
      msg_buf->buf = 0;
      msg_buf->alloc_size = 0;
      msg_buf->filled = 0;
   }

}

int uv_msg_read_start(uv_stream_t* stream, uv_alloc_cb alloc_cb, uv_msg_read_cb msg_read_cb, uv_free_cb free_cb) {

   msg_buf_t *msg_buf;
   msg_buf = malloc(sizeof(msg_buf_t));
   if( msg_buf==0 ) return UV_ENOBUFS;
   memset(msg_buf, 0, sizeof(msg_buf_t));

   msg_buf->msg_read_cb = msg_read_cb;
   msg_buf->alloc_cb = alloc_cb;
   msg_buf->free_cb = free_cb;

   UVTRACE(("previous stream->data=%p\n", stream->data));

   stream->data = msg_buf;

   return uv_read_start(stream, uv_stream_msg_alloc, uv_stream_msg_read);

}
