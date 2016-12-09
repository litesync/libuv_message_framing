#include "uv_msg_framing.h"

#ifdef DEBUGTRACE
#define UVTRACE(X)   printf X;
#else
#define UVTRACE(X)
#endif


/* Stream Initialization *****************************************************/

int uv_msg_init(uv_loop_t* loop, uv_msg_t* handle, int stream_type) {
   int rc;

   switch( stream_type ){
   case UV_TCP:
      rc = uv_tcp_init(loop, (uv_tcp_t*) handle);
      break;
   case UV_NAMED_PIPE:
      rc = uv_pipe_init(loop, (uv_pipe_t*) handle, 0);
      break;
   default:
      return UV_EINVAL;
   }

   if( rc ) return rc;

   handle->buf = NULL;
   handle->alloc_size = 0;
   handle->filled = 0;
   handle->alloc_cb = NULL;
   handle->free_cb = NULL;
   handle->msg_read_cb = NULL;
   /* initialize the public member */
   handle->data = NULL;

   return 0;
}


/* Message Writting **********************************************************/

int uv_msg_send(uv_msg_write_t *req, uv_stream_t* stream, void *msg, int size, uv_write_cb write_cb) {

   if ( !req || !stream || !msg || size <= 0 ) return UV_EINVAL;

   UVTRACE(("sending message: %s\n", msg));

   req->msg_size = htonl(size);
   req->buf[0].base = (char*) &req->msg_size;
   req->buf[0].len = 4;
   req->buf[1] = uv_buf_init(msg, size);

   return uv_write((uv_write_t*) req, stream, &req->buf[0], 2, write_cb);

}


/* Message Reading ***********************************************************/

int uv_stream_msg_realloc(uv_handle_t *handle, size_t suggested_size) {
   uv_msg_t *msg_buf = (uv_msg_t*) handle;
   uv_buf_t buf = {0};
   msg_buf->alloc_cb(handle, suggested_size, &buf);  // here the suggested size is exactly what it needs to read the message
   if( buf.base==0 || buf.len < suggested_size ) return 0;  // if buf.len < suggested_size and buf.base is valid it will be lost here (the allocated memory)
   memcpy(buf.base, msg_buf->buf, msg_buf->filled);
   if( msg_buf->free_cb ) msg_buf->free_cb(handle, msg_buf->buf);
   msg_buf->buf = buf.base;
   msg_buf->alloc_size = buf.len;
   return 1;
}

void uv_stream_msg_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *stream_buf) {
   uv_msg_t *msg_buf = (uv_msg_t*) handle;

   UVTRACE(("stream_msg_alloc  msg_buf=%p\n", msg_buf));
   if( msg_buf==0 ) return;

   if( msg_buf->buf==0 ){
      uv_buf_t buf = {0};
      msg_buf->alloc_cb(handle, suggested_size, &buf);
      msg_buf->buf = buf.base;
      if( msg_buf->buf==0 ) return;
      msg_buf->alloc_size = buf.len;
   }

   UVTRACE(("stream_msg_alloc  msg_buf->buf=%p  filled=%d\n", msg_buf->buf, msg_buf->filled));

   if( msg_buf->filled >= 4 ){
#ifdef NO_NETWORK_ORDER
      int msg_size = *(int*)msg_buf->buf;
#else
      int msg_size = ntohl(*(int*)msg_buf->buf);
#endif
      UVTRACE(("stream_msg_alloc  msg_size=%d\n", msg_size));
      int entire_msg_size = msg_size + 4;
      if( msg_buf->alloc_size < entire_msg_size ){
         if( !uv_stream_msg_realloc(handle, entire_msg_size) ){
            stream_buf->base = 0;
            return;
         }
      }
      stream_buf->len = entire_msg_size - msg_buf->filled;
   } else {
      if( msg_buf->alloc_size < 4 ){
         /* There is no enough space for the message size */
         UVTRACE(("calling realloc - alloc_size: %d, filled: %d\n", msg_buf->alloc_size, msg_buf->filled));
         if( !uv_stream_msg_realloc(handle, 64 * 1024) ){
            stream_buf->base = 0;
            return;
         }
      }
      stream_buf->len = msg_buf->alloc_size - msg_buf->filled;
   }

   stream_buf->base = msg_buf->buf + msg_buf->filled;
   UVTRACE(("stream_msg_alloc  base=%p  len=%d\n", stream_buf->base, stream_buf->len));
}

void uv_stream_msg_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
   uv_msg_t *msg_buf = (uv_msg_t*) stream;

   UVTRACE(("process_messages: received %d bytes\n", nread));
   UVTRACE(("msg_buf: %p  msg_buf->buf: %p  buf.base: %p\n", msg_buf, msg_buf->buf, buf->base));

   if( msg_buf==0 ) return;

   if (nread == 0) {
      /* Nothing read */
      // does it should release the ->buf here?
      return;
   }

   if (nread < 0) {
      // does it should release the ->buf here?
      msg_buf->msg_read_cb(stream, 0, nread);
      return;
   }

#ifdef TESTING_UV_MSG_FRAMING
   assert(buf->base == msg_buf->buf + msg_buf->filled);
   print_bytes("received", buf->base, nread);
#endif

   msg_buf->filled += nread;

   UVTRACE(("alloc_size: %d, received: %d, filled: %d\n", msg_buf->alloc_size, nread, msg_buf->filled));

   char *ptr = msg_buf->buf;

   while( msg_buf->filled >= 4 ){
#ifdef NO_NETWORK_ORDER
      int msg_size = *(int*)ptr;
#else
      int msg_size = ntohl(*(int*)ptr);
#endif
      int entire_msg = msg_size + 4;
      UVTRACE(("msg_size: %d, entire_msg: %d\n", msg_size, entire_msg));
      if( msg_buf->filled >= entire_msg ){
         msg_buf->msg_read_cb(stream, ptr + 4, msg_size);
         if( msg_buf->filled > entire_msg ){
            ptr += entire_msg;
         }
         msg_buf->filled -= entire_msg;
      } else {
         break;
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
   }

#ifdef TESTING_UV_MSG_FRAMING
   uv_async_send(&async_next_step);
#endif
}

int uv_msg_read_start(uv_msg_t* stream, uv_alloc_cb alloc_cb, uv_msg_read_cb msg_read_cb, uv_free_cb free_cb) {

   stream->msg_read_cb = msg_read_cb;
   stream->alloc_cb = alloc_cb;
   stream->free_cb = free_cb;

   return uv_read_start((uv_stream_t*)stream, uv_stream_msg_alloc, uv_stream_msg_read);

}
