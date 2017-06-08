#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <uv.h>
#include <assert.h>

uv_loop_t *server_loop;
uv_loop_t *client_loop;

uv_async_t async_next_step;
uv_timer_t timer;

uv_thread_t reader_thread;
uv_async_t stop_reader;
uv_barrier_t barrier;

#define CONNECTION_PORT 7357
#define DEFAULT_BACKLOG 16
#define DEFAULT_UV_SUGGESTED_SIZE 65536

int expected_suggested_size;
int expected_suggested_size2;
int next_alloc_size;
int next_alloc_size2;
int alloc_called;
int recvd_called;
int free_called;
int next_msg_letter;
int waiting_reader;

void print_bytes(char *oper, unsigned char *data, int size) {
   int i;
   printf("%s %d bytes:\n", oper, size);
   for(i=0; i < size; i++){
      printf("%02x ", data[i]);
   }
   puts("");
}

#define TESTING_UV_MSG_FRAMING
#include "../uv_msg_framing.c"

/* Common ********************************************************************/

void on_close(uv_handle_t *handle) {
   if (handle->type == UV_TCP) {
      free(handle);
   }
}

void on_walk(uv_handle_t *handle, void *arg) {
   uv_close(handle, on_close);
}

/* Reader Thread *************************************************************/

void check_msg(char *base, int size, char letter);

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
   printf("alloc_buffer called. suggested_size=%d  expected=%d  allocated=%d\n", suggested_size, expected_suggested_size, next_alloc_size);
   assert(suggested_size == expected_suggested_size);
   buf->base = (char*) malloc(next_alloc_size);
   buf->len = next_alloc_size;
   alloc_called++;
   // load the values for the next call
   expected_suggested_size = expected_suggested_size2;
   next_alloc_size = next_alloc_size2;
}

void free_buffer(uv_handle_t* handle, void* ptr) {
   printf("free_buffer called\n");
   free(ptr);
   free_called++;
}

void on_msg_received(uv_msg_t *client, void *msg, int size) {

   printf("msg_received called. size=%d\n", size);

   if (size < 0) {
      if (size != UV_EOF) {
         fprintf(stderr, "Read error: %s\n", uv_err_name(size));
      }
      uv_close((uv_handle_t*) client, on_close);
      return;
   }

   printf("new message here (%d bytes): %s\n", size, (char*)msg);

   check_msg(msg, size, next_msg_letter);
   next_msg_letter++;
   if( next_msg_letter == 'D' ) next_msg_letter = 'A';

   recvd_called++;

}

void on_new_connection(uv_stream_t *server, int status) {
   if (status < 0) {
      fprintf(stderr, "New connection error %s\n", uv_strerror(status));
      return;
   }

   uv_msg_t *client = malloc(sizeof(uv_msg_t));
   uv_msg_init(server_loop, client, UV_TCP);

   if (uv_accept(server, (uv_stream_t*) client) == 0) {
      uv_msg_read_start(client, alloc_buffer, on_msg_received, free_buffer);
   } else {
      uv_close((uv_handle_t*) client, on_close);
   }
}

void stop_reader_cb(uv_async_t *handle) {
  uv_stop(server_loop);
}

void reader_start(void *arg) {

   server_loop = malloc(sizeof(uv_loop_t));
   uv_loop_init(server_loop);

   uv_async_init(server_loop, &stop_reader, stop_reader_cb);

   uv_tcp_t *server = malloc(sizeof(uv_tcp_t));
   uv_tcp_init(server_loop, server);

   struct sockaddr_in addr;
   uv_ip4_addr("0.0.0.0", CONNECTION_PORT, &addr);

   uv_tcp_bind(server, (const struct sockaddr*)&addr, 0);

   int r = uv_listen((uv_stream_t*) server, DEFAULT_BACKLOG, on_new_connection);
   if (r) {
      fprintf(stderr, "Listen error %s\n", uv_strerror(r));
      return;
   }

   /* signal to the main thread the the listening socket is ready */
   uv_barrier_wait(&barrier);

   uv_run(server_loop, UV_RUN_DEFAULT);

   /* cleanup */
   puts("cleaning up reader thread");
   uv_walk(server_loop, on_walk, NULL);
   uv_run(server_loop, UV_RUN_DEFAULT);
   uv_loop_close(server_loop);
   free(server_loop);
}

/* Writer ********************************************************************/

uv_msg_t  *sendersocket;

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_req_t;

void on_write_complete(uv_write_t *req, int status) {
   if ( status < 0 ) {
      assert(0 && "message write failed");
   }
   free(req);
}

void send_data(uv_stream_t* stream, char *data, int size) {
   write_req_t *req = (write_req_t*) malloc(sizeof(write_req_t));
   req->buf = uv_buf_init(data, size);
   print_bytes("sending", data, size);
   uv_write((uv_write_t*) req, stream, &req->buf, 1, on_write_complete);
}

void on_connect(uv_connect_t *connect, int status) {
   uv_stream_t* socket;

   if (status < 0) {
      fprintf(stderr, "New connection error %s\n", uv_strerror(status));
      return;
   }

   socket = connect->handle;
   puts("client connected");
   free(connect);
   uv_stop(client_loop);
}

/*****************************************************************************/

void go_to_next_step(uv_async_t *handle) {

   if (waiting_reader != 1) {
      puts("next step ---------------------------------------------------------- AGAIN --");
      return;
   }
   waiting_reader = 0;

   puts("next step ----------------------------------------------------------------------");

   /* exit from the first loop run */
   uv_stop(client_loop);

}

void timer_cb() {

   /* exit from the second loop run */
   uv_stop(client_loop);

}

void wait_reader() {

   waiting_reader = 1;

   uv_run(client_loop, UV_RUN_DEFAULT);

   uv_timer_start(&timer, timer_cb, 50, 0);

   uv_run(client_loop, UV_RUN_DEFAULT);

}

/* Main **********************************************************************/

void cleanup() {

   /* cleanup */
   puts("cleaning up main thread");
   uv_async_send(&stop_reader);
   uv_walk(client_loop, on_walk, NULL);
   uv_run(client_loop, UV_RUN_DEFAULT);
   uv_loop_close(client_loop);

   uv_thread_join(&reader_thread);
   uv_barrier_destroy(&barrier);
   puts("done.");

}

int main() {
   int rc;

   uv_barrier_init(&barrier, 2);

   uv_thread_create(&reader_thread, reader_start, NULL);

   /* wait until the listening socket is ready on the reader thread */
   uv_barrier_wait(&barrier);

   client_loop = uv_default_loop();

   sendersocket = malloc(sizeof(uv_msg_t));
   uv_msg_init(client_loop, sendersocket, UV_TCP);

   uv_connect_t* connect = malloc(sizeof(uv_connect_t));

   struct sockaddr_in dest;
   uv_ip4_addr("127.0.0.1", CONNECTION_PORT, &dest);

   rc = uv_tcp_connect(connect, (uv_tcp_t*)sendersocket, (const struct sockaddr*)&dest, on_connect);
   if (rc) {
      fprintf(stderr, "Connect error %s\n", uv_strerror(rc));
      return 1;
   }

   uv_async_init(client_loop, &async_next_step, go_to_next_step);

   uv_timer_init(client_loop, &timer);

   uv_run(client_loop, UV_RUN_DEFAULT);

   run_tests();

   cleanup();

   return 0;

}

/*****************************************************************************/

int get_msg_size(void *msg) {
  int *pint = (int*) msg;
  assert(pint != 0);
  return ntohl(pint[0]);
}

void check_msg(char *base, int size, char letter) {
  int i;

  puts("checking message content");

  for(i=0; i < size-1; i++){
     assert(base[i] == letter);
     i++;
     if( i < size-1 ){
        assert(base[i] == i % 256);
     }
  }

  assert(base[size-1] == 0);

}

void create_test_msg(void *base, int size, char letter) {
  int *pint, i;
  char *ptr;

  pint = (int*) base;
  assert(pint != 0);
  pint[0] = htonl(size);

  ptr = base;
  ptr +=  4;

  for(i=0; i < size-1; i++){
     ptr[i] = letter;
     i++;
     if( i < size-1 ){
        ptr[i] = i % 256;
     }
  }

  ptr[size-1] = 0; /* null terminator */

  assert(get_msg_size(base) == size);

}

void test_coalesced_and_fragmented_messages() {
   int msg_size, entire_msg_size, next_chunk_size;
   int alloc_call_expected, recvd_call_expected;
   char *pmsg, *ptr, *stream_buffer;


   msg_size = 100;
   entire_msg_size = msg_size + 4;

   /* allocate space for 3 messages */
   stream_buffer = malloc(3 * entire_msg_size);
   assert(stream_buffer != 0);

   /* write the first message */
   ptr = stream_buffer;
   create_test_msg(ptr, msg_size, 'A');
   /* write the second message */
   ptr += entire_msg_size;
   create_test_msg(ptr, msg_size, 'B');
   /* write the third message */
   ptr += entire_msg_size;
   create_test_msg(ptr, msg_size, 'C');


   printf("stream_buffer: %p, get_msg_size: %d, msg_size: %d\n", stream_buffer, get_msg_size(stream_buffer), msg_size);
   assert(get_msg_size(stream_buffer) == msg_size);

   /* points to the first message */
   ptr = stream_buffer;
   next_msg_letter = 'A';

   /* start sending the chunks */


   /* send 2 bytes of the length */
   next_chunk_size = 2;
   alloc_call_expected = 1;
   expected_suggested_size = DEFAULT_UV_SUGGESTED_SIZE;
   next_alloc_size = 32;
   recvd_call_expected = 0;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;

   printf("stream_buffer: %p, get_msg_size: %d, msg_size: %d\n", stream_buffer, get_msg_size(stream_buffer), msg_size);
   print_bytes("buffer", stream_buffer, 8);
   assert(get_msg_size(stream_buffer) == msg_size);

   wait_reader();
   assert(alloc_called == alloc_call_expected);
   assert(recvd_called == recvd_call_expected);

   printf("stream_buffer: %p, get_msg_size: %d, msg_size: %d\n", stream_buffer, get_msg_size(stream_buffer), msg_size);
   print_bytes("buffer", stream_buffer, 8);
   assert(get_msg_size(stream_buffer) == msg_size);


   /* send 1 more byte of the length */
   next_chunk_size = 1;
   alloc_call_expected = 0;  // allocation callback call not expected
   recvd_call_expected = 0;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called == alloc_call_expected);
   assert(recvd_called == recvd_call_expected);

   assert(get_msg_size(stream_buffer) == msg_size);


   /* send more 2 bytes, the last one from the length and the first from the message */  // -- what if there is no sufficient memory here - another test case
   next_chunk_size = 2;
   alloc_call_expected = 0;  // allocation callback call not expected
   recvd_call_expected = 0;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called == alloc_call_expected);
   assert(recvd_called == recvd_call_expected);

   assert(get_msg_size(stream_buffer) == msg_size);


   /* send more 9 bytes from the message */
   next_chunk_size = 9;
   alloc_call_expected = 1;
   expected_suggested_size = entire_msg_size;
   next_alloc_size = entire_msg_size;
   recvd_call_expected = 0;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called == alloc_call_expected);
   assert(recvd_called == recvd_call_expected);


   /* send more 80 bytes from the message. total send: 90 bytes */
   next_chunk_size = 80;
   alloc_call_expected = 0;
   recvd_call_expected = 0;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called == alloc_call_expected);
   assert(recvd_called == recvd_call_expected);


   /* send more 64 bytes. the remaining 10 bytes from the first message + 4 bytes length + 50 bytes for the second message */
   next_chunk_size = 64;
   alloc_call_expected = 2;   //!? only for the second message  -- if the second message is bigger, then it should call the alloc callback
   expected_suggested_size = DEFAULT_UV_SUGGESTED_SIZE;
   next_alloc_size = 30;      //! if not enough buffer is supplied then another call may happen - new test case
   expected_suggested_size2 = entire_msg_size;
   next_alloc_size2 = entire_msg_size;
   recvd_call_expected = 1;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called == alloc_call_expected);
   assert(recvd_called == recvd_call_expected);


   /* send more 52 bytes. the remaining 50 bytes from the second message + 2 bytes length from the third */
   next_chunk_size = 52;
   alloc_call_expected = 0;
   expected_suggested_size = DEFAULT_UV_SUGGESTED_SIZE;
   next_alloc_size = 4;
   recvd_call_expected = 1;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called >= alloc_call_expected);
   assert(recvd_called == recvd_call_expected);


   /* send more 12 bytes. the remaining 2 bytes from the length and 10 bytes from the message */
   next_chunk_size = 12;
   alloc_call_expected = 1;
   expected_suggested_size = entire_msg_size;
   next_alloc_size = entire_msg_size;
   recvd_call_expected = 0;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called == alloc_call_expected);
   assert(recvd_called == recvd_call_expected);


   /* send more 10 bytes from the message. total sent: 20 bytes */
   next_chunk_size = 10;
   alloc_call_expected = 0;
   recvd_call_expected = 0;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called == alloc_call_expected);
   assert(recvd_called == recvd_call_expected);


   /* send more 10 bytes from the message. total sent: 30 bytes */
   next_chunk_size = 10;
   alloc_call_expected = 0;
   recvd_call_expected = 0;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called == alloc_call_expected);
   assert(recvd_called == recvd_call_expected);


   /* send the remaining 70 bytes from the message */
   next_chunk_size = 70;
   alloc_call_expected = 0;
   recvd_call_expected = 1;
   expected_suggested_size = DEFAULT_UV_SUGGESTED_SIZE;
   next_alloc_size = DEFAULT_UV_SUGGESTED_SIZE;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called >= alloc_call_expected);
   assert(recvd_called == recvd_call_expected);



   // test case: sending more than 1 message: 2 messages + 2 bytes (incomplete msg size)

   /* points to the first message */
   ptr = stream_buffer;

   /* send more 12 bytes. the remaining 2 bytes from the length and 10 bytes from the message */
   next_chunk_size = entire_msg_size + entire_msg_size + 2;
   alloc_call_expected = 0;
   expected_suggested_size = DEFAULT_UV_SUGGESTED_SIZE;
   next_alloc_size = 50;
   expected_suggested_size2 = entire_msg_size;
   next_alloc_size2 = entire_msg_size;
   recvd_call_expected = 2;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called >= alloc_call_expected);
   assert(recvd_called == recvd_call_expected);


   /* send the remaining bytes from the message */
   next_chunk_size = entire_msg_size - 2;
   alloc_call_expected = 0;
   recvd_call_expected = 1;

   alloc_called = 0; recvd_called = 0; free_called = 0;
   send_data((uv_stream_t*)sendersocket, ptr, next_chunk_size);
   ptr += next_chunk_size;
   wait_reader();
   assert(alloc_called >= alloc_call_expected);
   assert(recvd_called == recvd_call_expected);


   // test case: send more bytes than allocated


   free(stream_buffer);

   puts("All tests PASS!");

}

int run_tests() {

   test_coalesced_and_fragmented_messages();

}
