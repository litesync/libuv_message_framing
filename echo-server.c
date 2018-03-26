/*
** xxxxxxxxx
*/
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <uv.h>
#include "uv_msg_framing.c"
#include "uv_send_message.c"

#define DEFAULT_PORT 7000

#ifdef _WIN32
# define PIPENAME "\\\\?\\pipe\\some.name"
#elif defined (__android__)
# define PIPENAME "/data/local/tmp/some.name"
#else
# define PIPENAME "/tmp/some.name"
#endif

/****************************************************************************/

void on_close(uv_handle_t *handle) {
   if (handle->type == UV_TCP || handle->type == UV_NAMED_PIPE) {
      free(handle);
   }
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
   buf->base = (char*) malloc(suggested_size);
   buf->len = suggested_size;
}

void free_buffer(uv_handle_t* handle, void* ptr) {
   free(ptr);
}

void on_msg_sent(send_message_t *req, int status) {

   if ( status < 0 ) {
      printf("message send failed: %s   user_data: %d\n", (char*)req->msg, (int)req->data);
   } else {
      printf("message sent: %s   user_data: %d\n", (char*)req->msg, (int)req->data);
   }

}

void on_msg_received(uv_msg_t *client, void *msg, int size) {

   if (size < 0) {
      if (size != UV_EOF) {
         fprintf(stderr, "Read error: %s\n", uv_err_name(size));
      }
      uv_close((uv_handle_t*) client, NULL);
      return;
   }

   printf("new message received (%d bytes): %s\n", size, (char*)msg);

   send_message(client, msg, size, UV_MSG_TRANSIENT, on_msg_sent, (void*)124);

}

void on_new_connection(uv_stream_t *server, int status) {

   if (status < 0) {
      fprintf(stderr, "New connection error %s\n", uv_strerror(status));
      return;
   }

   uv_msg_t *client = malloc(sizeof(uv_msg_t));

#ifdef USE_PIPE_EXAMPLE
   uv_msg_init(server->loop, client, UV_NAMED_PIPE);
#else
   uv_msg_init(server->loop, client, UV_TCP);
#endif

   if (uv_accept(server, (uv_stream_t*) client) == 0) {
      /* new client connected! start reading messages on this stream (asynchronously) */
      uv_msg_read_start(client, alloc_buffer, on_msg_received, free_buffer);
   } else {
      uv_close((uv_handle_t*) client, on_close);
   }

}

#ifdef USE_PIPE_EXAMPLE

int main() {
   int rc;
   uv_loop_t *loop = uv_default_loop();

   uv_msg_t* socket = malloc(sizeof(uv_msg_t));
   rc = uv_msg_init(loop, socket, UV_NAMED_PIPE);

   rc = uv_pipe_bind((uv_pipe_t*)socket, PIPENAME);
   if (rc) {
      fprintf(stderr, "Bind error %s\n", uv_strerror(rc));
      return 1;
   }

   rc = uv_listen((uv_stream_t*) socket, 16, on_new_connection);
   if (rc) {
      fprintf(stderr, "Listen error %s\n", uv_strerror(rc));
      return 1;
   }

   return uv_run(loop, UV_RUN_DEFAULT);
}

#else

int main() {
   int rc;
   uv_loop_t *loop = uv_default_loop();

   uv_msg_t* socket = malloc(sizeof(uv_msg_t));
   rc = uv_msg_init(loop, socket, UV_TCP);

   struct sockaddr_in addr;
   uv_ip4_addr("0.0.0.0", DEFAULT_PORT, &addr);

   rc = uv_tcp_bind((uv_tcp_t*)socket, (const struct sockaddr*)&addr, 0);
   if (rc) {
      fprintf(stderr, "Bind error %s\n", uv_strerror(rc));
      return 1;
   }

   rc = uv_listen((uv_stream_t*) socket, 16, on_new_connection);
   if (rc) {
      fprintf(stderr, "Listen error %s\n", uv_strerror(rc));
      return 1;
   }

   return uv_run(loop, UV_RUN_DEFAULT);
}

#endif
