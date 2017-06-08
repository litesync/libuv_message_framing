/*
** This example code must be run with the tcp-echo-server running
** https://github.com/nikhilm/uvbook/blob/master/code/tcp-echo-server/main.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <uv.h>
#include "uv_msg_framing.c"
#include "uv_send_message.c"

#define DEFAULT_PORT 7000

void on_msg_sent(send_message_t *req, int status);

/****************************************************************************/

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
   buf->base = (char*) malloc(suggested_size);
   buf->len = suggested_size;
}

void free_buffer(uv_handle_t* handle, void* ptr) {
   free(ptr);
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

   if (strcmp(msg, "Is it working?") == 0) {
      char *response = "Yeaaah!";
      send_message(client, response, strlen(response)+1, UV_MSG_STATIC, on_msg_sent, 0);
   }

}

void on_msg_sent(send_message_t *req, int status) {

   if ( status < 0 ) {
      printf("message send failed: %s   user_data: %d\n", (char*)req->msg, (int)req->data);
   } else {
      printf("message sent: %s   user_data: %d\n", (char*)req->msg, (int)req->data);
   }

}

void on_connect(uv_connect_t *connect, int status) {
   uv_msg_t* socket = (uv_msg_t*) connect->handle;
   char *msg;

   free(connect);

   if (status < 0) {
      fprintf(stderr, "Connection error: %s\n", uv_strerror(status));
      return;
   }

   /* we are connected! start the reading messages on this stream (asynchronously) */

   uv_msg_read_start(socket, alloc_buffer, on_msg_received, free_buffer);

   /* now send some messages */

   msg = "Hello Mom!";
   send_message(socket, msg, strlen(msg)+1, UV_MSG_STATIC, 0, 0);

   msg = "Hello Dad!";
   send_message(socket, msg, strlen(msg)+1, UV_MSG_TRANSIENT, 0, 0);

   msg = strdup("Hello World!");
   send_message(socket, msg, strlen(msg)+1, free, 0, 0);

   msg = "Hello Mom!";
   send_message(socket, msg, strlen(msg)+1, UV_MSG_STATIC, on_msg_sent, (void*)123);

   msg = "Hello Dad!";
   send_message(socket, msg, strlen(msg)+1, UV_MSG_TRANSIENT, on_msg_sent, (void*)124);

   msg = strdup("Hello World!");
   send_message(socket, msg, strlen(msg)+1, free, on_msg_sent, (void*)125);

   msg = "Is it working?";
   send_message(socket, msg, strlen(msg)+1, UV_MSG_STATIC, on_msg_sent, (void*)126);

}

int main() {
   int rc;
   uv_loop_t *loop = uv_default_loop();

   uv_msg_t* socket = malloc(sizeof(uv_msg_t));
   rc = uv_msg_init(loop, socket, UV_TCP);

   struct sockaddr_in dest;
   uv_ip4_addr("127.0.0.1", DEFAULT_PORT, &dest);

   uv_connect_t* connect = malloc(sizeof(uv_connect_t));
   rc = uv_tcp_connect(connect, (uv_tcp_t*)socket, (const struct sockaddr*)&dest, on_connect);
   if (rc) {
      fprintf(stderr, "Connect error: %s\n", uv_strerror(rc));
      return 1;
   }

   return uv_run(loop, UV_RUN_DEFAULT);
}
