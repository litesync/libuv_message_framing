/*
** This example code must be run with the tcp-echo-server.c running
*/
#include <uv.h>
#include "uv_msg_framing.c"

#define DEFAULT_PORT 7000

int received=0;


void on_write_complete(uv_write_t *req, int status) {

   if ( status < 0 ) {
      puts("message write failed");
   } else {
      puts("message sent");
   }

   free(req->data);
   free(req);

}

void send_message(uv_stream_t* stream, char *msg, int size, uv_msg_write_cb msg_write_cb) {
   uv_write_t *req = malloc(sizeof(uv_write_t));

   msg = strdup(msg);

   req->data = msg;  // save the data pointer to release on completion

   uv_msg_send(req, stream, msg, size, msg_write_cb);

}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
   buf->base = (char*) malloc(suggested_size);
   buf->len = suggested_size;
}

void free_buffer(uv_handle_t* handle, void* ptr) {
   free(ptr);
}

void on_msg_received(uv_stream_t *client, void *msg, int size) {

   if (size < 0) {
      if (size != UV_EOF)
         fprintf(stderr, "Read error %s\n", uv_err_name(size));
      uv_close((uv_handle_t*) client, NULL);
      return;
   }

   printf("new message here (%d bytes): %s\n", size, msg);

   received++;
   if( received==2 ){
      char *msg = "Anooooooother one... :P";
      send_message(client, msg, strlen(msg)+1, on_write_complete);
   }

}

void on_connect(uv_connect_t *connect, int status) {
   uv_stream_t* socket;

   if (status < 0) {
      fprintf(stderr, "New connection error %s\n", uv_strerror(status));
      return;
   }

   socket = connect->handle;

   uv_msg_read_start((uv_stream_t*) socket, alloc_buffer, on_msg_received, free_buffer);

   char *msg = "Hello World!";
   send_message(socket, msg, strlen(msg)+1, on_write_complete);

   msg = "This is the second message";
   send_message(socket, msg, strlen(msg)+1, on_write_complete);

}

int main() {
   int rc;
   uv_loop_t *loop = uv_default_loop();
   uv_tcp_t* socket = malloc(sizeof(uv_tcp_t));
   uv_tcp_init(loop, socket);

   uv_connect_t* connect = malloc(sizeof(uv_connect_t));

   struct sockaddr_in dest;
   uv_ip4_addr("127.0.0.1", DEFAULT_PORT, &dest);

   rc = uv_tcp_connect(connect, socket, (const struct sockaddr*)&dest, on_connect);
   if (rc) {
      fprintf(stderr, "Connect error %s\n", uv_strerror(rc));
      return 1;
   }

   return uv_run(loop, UV_RUN_DEFAULT);
}
