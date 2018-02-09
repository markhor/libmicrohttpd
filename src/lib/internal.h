/*
  This file is part of libmicrohttpd
  Copyright (C) 2007-2017 Daniel Pittman and Christian Grothoff

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

/**
 * @file microhttpd/internal.h
 * @brief  internal shared structures
 * @author Daniel Pittman
 * @author Christian Grothoff
 */

#ifndef INTERNAL_H
#define INTERNAL_H

#include "mhd_options.h"
#include "platform.h"
#include "microhttpd2.h"
#include "microhttpd_tls.h"
#include "mhd_assert.h"

#ifdef HTTPS_SUPPORT
#include <gnutls/gnutls.h>
#if GNUTLS_VERSION_MAJOR >= 3
#include <gnutls/abstract.h>
#endif
#endif /* HTTPS_SUPPORT */

#ifdef HAVE_STDBOOL_H
#include <stdbool.h>
#endif
#ifdef MHD_PANIC
/* Override any defined MHD_PANIC macro with proper one */
#undef MHD_PANIC
#endif /* MHD_PANIC */

#ifdef HAVE_MESSAGES
/**
 * Trigger 'panic' action based on fatal errors.
 *
 * @param msg error message (const char *)
 */
#define MHD_PANIC(msg) do { mhd_panic (mhd_panic_cls, __FILE__, __LINE__, msg); BUILTIN_NOT_REACHED; } while (0)
#else
/**
 * Trigger 'panic' action based on fatal errors.
 *
 * @param msg error message (const char *)
 */
#define MHD_PANIC(msg) do { mhd_panic (mhd_panic_cls, __FILE__, __LINE__, NULL); BUILTIN_NOT_REACHED; } while (0)
#endif

#include "mhd_threads.h"
#include "mhd_locks.h"
#include "mhd_sockets.h"
#include "mhd_itc_types.h"


/**
 * Close FD and abort execution if error is detected.
 * @param fd the FD to close
 */
#define MHD_fd_close_chk_(fd) do {             \
    if (0 == close ((fd)) && (EBADF == errno)) \
      MHD_PANIC(_("Failed to close FD.\n"));   \
  } while(0)

/**
 * Should we perform additional sanity checks at runtime (on our internal
 * invariants)?  This may lead to aborts, but can be useful for debugging.
 */
#define EXTRA_CHECKS MHD_NO

#define MHD_MAX(a,b) (((a)<(b)) ? (b) : (a))
#define MHD_MIN(a,b) (((a)<(b)) ? (a) : (b))


/**
 * Minimum size by which MHD tries to increment read/write buffers.
 * We usually begin with half the available pool space for the
 * IO-buffer, but if absolutely needed we additively grow by the
 * number of bytes given here (up to -- theoretically -- the full pool
 * space).
 */
#define MHD_BUF_INC_SIZE 1024


/**
 * Handler for fatal errors.
 */
extern MHD_PanicCallback mhd_panic;

/**
 * Closure argument for "mhd_panic".
 */
extern void *mhd_panic_cls;

/* If we have Clang or gcc >= 4.5, use __buildin_unreachable() */
#if defined(__clang__) || (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)
#define BUILTIN_NOT_REACHED __builtin_unreachable()
#elif defined(_MSC_FULL_VER)
#define BUILTIN_NOT_REACHED __assume(0)
#else
#define BUILTIN_NOT_REACHED
#endif

#ifndef MHD_STATICSTR_LEN_
/**
 * Determine length of static string / macro strings at compile time.
 */
#define MHD_STATICSTR_LEN_(macro) (sizeof(macro)/sizeof(char) - 1)
#endif /* ! MHD_STATICSTR_LEN_ */



/**
 * Ability to use same connection for next request
 */
enum MHD_ConnKeepAlive
{
  /**
   * Connection must be closed after sending response.
   */
  MHD_CONN_MUST_CLOSE = -1,

  /**
   * KeelAlive state is not yet determined
   */
  MHD_CONN_KEEPALIVE_UNKOWN = 0,

  /**
   * Connection can be used for serving next request
   */
  MHD_CONN_USE_KEEPALIVE = 1
};


/**
 * Function to receive plaintext data.
 *
 * @param conn the connection struct
 * @param write_to where to write received data
 * @param max_bytes maximum number of bytes to receive
 * @return number of bytes written to @a write_to
 */
typedef ssize_t
(*ReceiveCallback) (struct MHD_Connection *conn,
                    void *write_to,
                    size_t max_bytes);


/**
 * Function to transmit plaintext data.
 *
 * @param conn the connection struct
 * @param read_from where to read data to transmit
 * @param max_bytes maximum number of bytes to transmit
 * @return number of bytes transmitted
 */
typedef ssize_t
(*TransmitCallback) (struct MHD_Connection *conn,
                     const void *read_from,
                     size_t max_bytes);


/**
 * States in a state machine for a request.
 *
 * The main transitions are any-state to #MHD_REQUEST_CLOSED, any
 * state to state+1, #MHD_REQUEST_FOOTERS_SENT to
 * #MHD_REQUEST_INIT.  #MHD_REQUEST_CLOSED is the terminal state
 * and #MHD_REQUEST_INIT the initial state.
 *
 * Note that transitions for *reading* happen only after the input has
 * been processed; transitions for *writing* happen after the
 * respective data has been put into the write buffer (the write does
 * not have to be completed yet).  A transition to
 * #MHD_REQUEST_CLOSED or #MHD_REQUEST_INIT requires the write
 * to be complete.
 */
enum MHD_REQUEST_STATE
{
  /**
   * Request just started (no headers received).
   * Waiting for the line with the request type, URL and version.
   */
  MHD_REQUEST_INIT = 0,

  /**
   * 1: We got the URL (and request type and version).  Wait for a header line.
   */
  MHD_REQUEST_URL_RECEIVED = MHD_REQUEST_INIT + 1,

  /**
   * 2: We got part of a multi-line request header.  Wait for the rest.
   */
  MHD_REQUEST_HEADER_PART_RECEIVED = MHD_REQUEST_URL_RECEIVED + 1,

  /**
   * 3: We got the request headers.  Process them.
   */
  MHD_REQUEST_HEADERS_RECEIVED = MHD_REQUEST_HEADER_PART_RECEIVED + 1,

  /**
   * 4: We have processed the request headers.  Send 100 continue.
   */
  MHD_REQUEST_HEADERS_PROCESSED = MHD_REQUEST_HEADERS_RECEIVED + 1,

  /**
   * 5: We have processed the headers and need to send 100 CONTINUE.
   */
  MHD_REQUEST_CONTINUE_SENDING = MHD_REQUEST_HEADERS_PROCESSED + 1,

  /**
   * 6: We have sent 100 CONTINUE (or do not need to).  Read the message body.
   */
  MHD_REQUEST_CONTINUE_SENT = MHD_REQUEST_CONTINUE_SENDING + 1,

  /**
   * 7: We got the request body.  Wait for a line of the footer.
   */
  MHD_REQUEST_BODY_RECEIVED = MHD_REQUEST_CONTINUE_SENT + 1,

  /**
   * 8: We got part of a line of the footer.  Wait for the
   * rest.
   */
  MHD_REQUEST_FOOTER_PART_RECEIVED = MHD_REQUEST_BODY_RECEIVED + 1,

  /**
   * 9: We received the entire footer.  Wait for a response to be queued
   * and prepare the response headers.
   */
  MHD_REQUEST_FOOTERS_RECEIVED = MHD_REQUEST_FOOTER_PART_RECEIVED + 1,

  /**
   * 10: We have prepared the response headers in the writ buffer.
   * Send the response headers.
   */
  MHD_REQUEST_HEADERS_SENDING = MHD_REQUEST_FOOTERS_RECEIVED + 1,

  /**
   * 11: We have sent the response headers.  Get ready to send the body.
   */
  MHD_REQUEST_HEADERS_SENT = MHD_REQUEST_HEADERS_SENDING + 1,

  /**
   * 12: We are ready to send a part of a non-chunked body.  Send it.
   */
  MHD_REQUEST_NORMAL_BODY_READY = MHD_REQUEST_HEADERS_SENT + 1,

  /**
   * 13: We are waiting for the client to provide more
   * data of a non-chunked body.
   */
  MHD_REQUEST_NORMAL_BODY_UNREADY = MHD_REQUEST_NORMAL_BODY_READY + 1,

  /**
   * 14: We are ready to send a chunk.
   */
  MHD_REQUEST_CHUNKED_BODY_READY = MHD_REQUEST_NORMAL_BODY_UNREADY + 1,

  /**
   * 15: We are waiting for the client to provide a chunk of the body.
   */
  MHD_REQUEST_CHUNKED_BODY_UNREADY = MHD_REQUEST_CHUNKED_BODY_READY + 1,

  /**
   * 16: We have sent the response body. Prepare the footers.
   */
  MHD_REQUEST_BODY_SENT = MHD_REQUEST_CHUNKED_BODY_UNREADY + 1,

  /**
   * 17: We have prepared the response footer.  Send it.
   */
  MHD_REQUEST_FOOTERS_SENDING = MHD_REQUEST_BODY_SENT + 1,

  /**
   * 18: We have sent the response footer.  Shutdown or restart.
   */
  MHD_REQUEST_FOOTERS_SENT = MHD_REQUEST_FOOTERS_SENDING + 1,

  /**
   * 19: This request is to be closed.
   */
  MHD_REQUEST_CLOSED = MHD_REQUEST_FOOTERS_SENT + 1,

  /**
   * 20: This request is finished (only to be freed)
   */
  MHD_REQUEST_IN_CLEANUP = MHD_REQUEST_CLOSED + 1,

#ifdef UPGRADE_SUPPORT
  /**
   * Request was "upgraded" and socket is now under the
   * control of the application.
   */
  MHD_REQUEST_UPGRADE
#endif /* UPGRADE_SUPPORT */

};


/**
 * Header or cookie in HTTP request or response.
 */
struct MHD_HTTP_Header
{
  /**
   * Headers are kept in a linked list.
   */
  struct MHD_HTTP_Header *next;

  /**
   * The name of the header (key), without the colon.
   */
  char *header;

  /**
   * The value of the header.
   */
  char *value;

  /**
   * Type of the header (where in the HTTP protocol is this header
   * from).
   */
  enum MHD_ValueKind kind;

};


/**
 * State kept for each HTTP request.
 */
struct MHD_Request
{

  /**
   * Reference to the MHD_Daemon struct.
   */
  struct MHD_Daemon *daemon;

  /**
   * Connection this request is associated with.
   */
  struct MHD_Connection *connection;

  /**
   * Linked list of parsed headers.
   */
  struct MHD_HTTP_Header *headers_received;

  /**
   * Tail of linked list of parsed headers.
   */
  struct MHD_HTTP_Header *headers_received_tail;

  /**
   * The memory pool is created whenever we first read from the TCP
   * stream and destroyed at the end of each request (and re-created
   * for the next request).  In the meantime, this pointer is NULL.
   * The pool is used for all request-related data except for the
   * response (which maybe shared between requests) and the IP
   * address (which persists across individual requests).
   */
  struct MemoryPool *pool;

  /**
   * We allow the main application to associate some pointer with the
   * HTTP request, which is passed to each #MHD_AccessHandlerCallback
   * and some other API calls.  Here is where we store it.  (MHD does
   * not know or care what it is).
   */
  void *client_context;

  /**
   * Request method.  Should be GET/POST/etc.  Allocated in pool.
   */
  char *method;

  /**
   * Requested URL (everything after "GET" only).  Allocated
   * in pool.
   */
  const char *url;

  /**
   * HTTP version string (i.e. http/1.1).  Allocated
   * in pool.
   */
  char *version;

  /**
   * Close connection after sending response?
   * Functions may change value from "Unknown" or "KeepAlive" to "Must close",
   * but no functions reset value "Must Close" to any other value.
   */
  enum MHD_ConnKeepAlive keepalive;

  /**
   * Buffer for reading requests.  Allocated in pool.  Actually one
   * byte larger than @e read_buffer_size (if non-NULL) to allow for
   * 0-termination.
   */
  char *read_buffer;

  /**
   * Buffer for writing response (headers only).  Allocated
   * in pool.
   */
  char *write_buffer;

  /**
   * Last incomplete header line during parsing of headers.
   * Allocated in pool.  Only valid if state is
   * either #MHD_REQUEST_HEADER_PART_RECEIVED or
   * #MHD_REQUEST_FOOTER_PART_RECEIVED.
   */
  char *last;

  /**
   * Position after the colon on the last incomplete header
   * line during parsing of headers.
   * Allocated in pool.  Only valid if state is
   * either #MHD_REQUEST_HEADER_PART_RECEIVED or
   * #MHD_REQUEST_FOOTER_PART_RECEIVED.
   */
  char *colon;


  /**
   * Function used for reading HTTP request stream.
   */
  ReceiveCallback recv_cls;

  /**
   * Function used for writing HTTP response stream.
   */
  TransmitCallback send_cls;

#ifdef UPGRADE_SUPPORT
  /**
   * If this connection was upgraded, this points to
   * the upgrade response details such that the
   * #thread_main_connection_upgrade()-logic can perform the
   * bi-directional forwarding.
   */
  struct MHD_UpgradeResponseHandle *urh;
#endif /* UPGRADE_SUPPORT */

  /**
   * Foreign address (of length @e addr_len).
   */
  struct sockaddr_storage addr;

  /**
   * Thread handle for this connection (if we are using
   * one thread per connection).
   */
  MHD_thread_handle_ID_ pid;

  /**
   * Size of @e read_buffer (in bytes).  This value indicates
   * how many bytes we're willing to read into the buffer;
   * the real buffer is one byte longer to allow for
   * adding zero-termination (when needed).
   */
  size_t read_buffer_size;

  /**
   * Position where we currently append data in
   * @e read_buffer (last valid position).
   */
  size_t read_buffer_offset;

  /**
   * Size of @e write_buffer (in bytes).
   */
  size_t write_buffer_size;

  /**
   * Offset where we are with sending from @e write_buffer.
   */
  size_t write_buffer_send_offset;

  /**
   * Last valid location in write_buffer (where do we
   * append and up to where is it safe to send?)
   */
  size_t write_buffer_append_offset;

  /**
   * Number of bytes we had in the HTTP header, set once we
   * pass #MHD_REQUEST_HEADERS_RECEIVED.
   */
  size_t header_size;

  /**
   * How many more bytes of the body do we expect
   * to read? #MHD_SIZE_UNKNOWN for unknown.
   */
  uint64_t remaining_upload_size;

  /**
   * If we are receiving with chunked encoding, where are we right
   * now?  Set to 0 if we are waiting to receive the chunk size;
   * otherwise, this is the size of the current chunk.  A value of
   * zero is also used when we're at the end of the chunks.
   */
  uint64_t current_chunk_size;

  /**
   * If we are receiving with chunked encoding, where are we currently
   * with respect to the current chunk (at what offset / position)?
   */
  uint64_t current_chunk_offset;

  /**
   * Current write position in the actual response
   * (excluding headers, content only; should be 0
   * while sending headers).
   */
  uint64_t response_write_position;

#if defined(_MHD_HAVE_SENDFILE)
  enum MHD_resp_sender_
  {
    MHD_resp_sender_std = 0,
    MHD_resp_sender_sendfile
  } resp_sender;
#endif /* _MHD_HAVE_SENDFILE */

  /**
   * Position in the 100 CONTINUE message that
   * we need to send when receiving http 1.1 requests.
   */
  size_t continue_message_write_offset;

  /**
   * State in the FSM for this request.
   */
  enum MHD_REQUEST_STATE state;

  /**
   * What is this request waiting for?
   */
  enum MHD_RequestEventLoopInfo event_loop_info;

  /**
   * Did we ever call the "default_handler" on this request?  (this
   * flag will determine if we call the #MHD_OPTION_NOTIFY_COMPLETED
   * handler when the request closes down).
   */
  bool client_aware;

  /**
   * Are we currently inside the "idle" handler (to avoid recursively
   * invoking it).
   */
  bool in_idle;

  /**
   * Are we currently inside the "idle" handler (to avoid recursively
   * invoking it).
   */
  bool in_cleanup;

  /**
   * Are we receiving with chunked encoding?  This will be set to
   * #MHD_YES after we parse the headers and are processing the body
   * with chunks.  After we are done with the body and we are
   * processing the footers; once the footers are also done, this will
   * be set to #MHD_NO again (before the final call to the handler).
   */
  bool have_chunked_upload;
};


/**
 * State kept per HTTP connection.
 */ 
struct MHD_Connection
{

#ifdef EPOLL_SUPPORT
  /**
   * Next pointer for the EDLL listing connections that are epoll-ready.
   */
  struct MHD_Connection *nextE;

  /**
   * Previous pointer for the EDLL listing connections that are epoll-ready.
   */
  struct MHD_Connection *prevE;
#endif

  /**
   * Next pointer for the DLL describing our IO state.
   */
  struct MHD_Connection *next;

  /**
   * Previous pointer for the DLL describing our IO state.
   */
  struct MHD_Connection *prev;

  /**
   * Next pointer for the XDLL organizing connections by timeout.
   * This DLL can be either the
   * 'manual_timeout_head/manual_timeout_tail' or the
   * 'normal_timeout_head/normal_timeout_tail', depending on whether a
   * custom timeout is set for the connection.
   */
  struct MHD_Connection *nextX;

  /**
   * Previous pointer for the XDLL organizing connections by timeout.
   */
  struct MHD_Connection *prevX;

  /**
   * Reference to the MHD_Daemon struct.
   */
  struct MHD_Daemon *daemon;

  /**
   * Information about the current request we are processing
   * on this connection.
   */
  struct MHD_Request request;

  /**
   * Is the connection suspended?
   */
  bool suspended;

  /**
   * Is the connection wanting to resume?
   */
  bool resuming;

  /**
   * Set to `true` if the thread has been joined.
   */
  bool thread_joined;

  /**
   * true if #socket_fd is non-blocking, false otherwise.
   */
  bool sk_nonblck;

  /**
   * Has this socket been closed for reading (i.e.  other side closed
   * the connection)?  If so, we must completely close the connection
   * once we are done sending our response (and stop trying to read
   * from this socket).
   */
  bool read_closed;

  /**
   * Length of the foreign address.
   */
  socklen_t addr_len;

  /**
   * Last time this connection had any activity
   * (reading or writing).
   */
  time_t last_activity;

  /**
   * After how many seconds of inactivity should
   * this connection time out?  Zero for no timeout.
   */
  time_t connection_timeout;

  /**
   * Socket for this connection.  Set to #MHD_INVALID_SOCKET if
   * this connection has died (daemon should clean
   * up in that case).
   */
  MHD_socket socket_fd;


#ifdef EPOLL_SUPPORT
  /**
   * What is the state of this socket in relation to epoll?
   */
  enum MHD_EpollState epoll_state;
#endif

  
};





/**
 * State kept for each MHD daemon.  All connections are kept in two
 * doubly-linked lists.  The first one reflects the state of the
 * connection in terms of what operations we are waiting for (read,
 * write, locally blocked, cleanup) whereas the second is about its
 * timeout state (default or custom).
 */
struct MHD_Daemon
{
  /**
   * Function to call to handle incoming requests.
   */
  MHD_RequestCallback rc;

  /**
   * Closure for @e rc.
   */
  void *rc_cls;

  /** 
   * Function to call for logging.
   */
  MHD_LoggingCallback logger;

  /**
   * Closure for @e logger.
   */
  void *logger_cls;  

  /**
   * Function to call to accept/reject connections based on
   * the client's IP address.
   */
  MHD_AcceptPolicyCallback accept_policy_cb;

  /**
   * Closure for @e accept_policy_cb.
   */
  void *accept_policy_cb_cls;

  /**
   * Function to call on the full URL early for logging.
   */
  MHD_EarlyUriLogCallback early_uri_logger_cb;

  /**
   * Closure for @e early_uri_logger_cb.
   */
  void *early_uri_logger_cls;

  /**
   * Function to call whenever a connection is started or
   * closed.
   */
  MHD_NotifyConnectionCallback notify_connection_cb;

  /**
   * Closure for @e notify_connection_cb.
   */
  void *notify_connection_cb_cls;

  /**
   * Function to call to unescape sequences in URIs and URI arguments.
   * See #MHD_daemon_unescape_cb().
   */
  MHD_UnescapeCallback unescape_cb;

  /**
   * Closure for @e unescape_cb.
   */
  void *unescape_cb_cls;
  
#if HTTPS_SUPPORT
  /**
   * Which TLS backend should be used. NULL for no TLS.
   * This is merely the handle to the dlsym() object, not
   * the API.
   */ 
  void *tls_backend_lib;

  /**
   * Callback functions to use for TLS operations.
   */
  struct MHD_TLS_Plugin *tls_api;
#endif
#if ENABLE_DAUTH

  /**
   * Random values to be used by digest authentication module.
   * Size given in @e digest_auth_random_buf_size.
   */
  const void *digest_auth_random_buf;
#endif
  
  /** 
   * Socket address to bind to for the listen socket.
   */
  struct sockaddr_storage listen_sa;

  /** 
   * Number of (valid) bytes in @e listen_sa.  Zero
   * if @e listen_sa is not initialized.
   */
  size_t listen_sa_len;

  /**
   * Buffer size to use for each connection. Default
   * is #MHD_POOL_SIZE_DEFAULT.
   */
  size_t connection_memory_limit_b;

/**
 * Default minimum size by which MHD tries to increment read/write
 * buffers.  We usually begin with half the available pool space for
 * the IO-buffer, but if absolutely needed we additively grow by the
 * number of bytes given here (up to -- theoretically -- the full pool
 * space).
 */
#define BUF_INC_SIZE_DEFAULT 1024
  
  /**
   * Increment to use when growing the read buffer. Smaller
   * than @e connection_memory_limit_b.
   */
  size_t connection_memory_increment_b;

  /**
   * Desired size of the stack for threads created by MHD,
   * 0 for system default.
   */
  size_t thread_stack_limit_b;

#if ENABLE_DAUTH
  
  /**
   * Size of @e digest_auth_random_buf.
   */
  size_t digest_auth_random_buf_size;

  /**
   * Default value for @e digest_nc_length.
   */
#define DIGEST_NC_LENGTH_DEFAULT 4
  
  /**
   * Desired length of the internal array with the nonce and
   * nonce counters for digest authentication.
   */
  size_t digest_nc_length;
#endif
  
  /**
   * Default value we use for the listen backlog.
   */
#ifdef SOMAXCONN
#define LISTEN_BACKLOG_DEFAULT SOMAXCONN
#else  /* !SOMAXCONN */
#define LISTEN_BACKLOG_DEFAULT 511
#endif
  
  /**
   * Backlog argument to use for listen.  See 
   * #MHD_daemon_listen_backlog().
   */
  int listen_backlog;
  
  /**
   * Default queue length to use with fast open.
   */
#define FO_QUEUE_LENGTH_DEFAULT 50

  /**
   * Queue length to use with fast open.
   */
  unsigned int fo_queue_length;

  /**
   * Maximum number of connections MHD accepts. 0 for unlimited.
   */
  unsigned int global_connection_limit;

  /**
   * Maximum number of connections we accept per IP, 0 for unlimited.
   */
  unsigned int ip_connection_limit;

  /**
   * Default timeout in seconds for idle connections.
   */
  unsigned int connection_default_timeout_s;
  
  /** 
   * Listen socket we should use, MHD_INVALID_SOCKET means
   * we are to initialize the socket from the other options given.
   */
  MHD_socket listen_socket;

  /**
   * Inter-thread communication channel.
   */
  struct MHD_itc_ itc;
  
  /**
   * Which threading model do we use? Postive
   * numbers indicate the number of worker threads to be used.
   * Values larger than 1 imply a thread pool.
   */
  enum MHD_ThreadingModel threading_model;
  
  /**
   * When should we use TCP_FASTOPEN? 
   * See #MHD_daemon_tcp_fastopen().
   */ 
  enum MHD_FastOpenMethod fast_open_method;

  /**
   * Address family to use when listening.
   * Default is #MHD_AF_NONE (do not listen).
   */
  enum MHD_AddressFamily listen_af;

  /**
   * Sets active/desired style of the event loop.
   * (Auto only possible during initialization, later set to
   * the actual style we use.)
   */
  enum MHD_EventLoopSyscall event_loop_syscall;

  /**
   * How strictly do we enforce the HTTP protocol?
   * See #MHD_daemon_protocol_strict_level().
   */
  enum MHD_ProtocolStrictLevel protocol_strict_level;

  /** 
   * On which port should we listen on? Only effective if we were not
   * given a listen socket or a full address via
   * #MHD_daemon_bind_sa().  0 means to bind to random free port.
   */
  uint16_t listen_port;
  
  /**
   * Suppress generating the "Date:" header, this system
   * lacks an RTC (or developer is hyper-optimizing).  See
   * #MHD_daemon_suppress_date_no_clock().
   */ 
  bool suppress_date;

  /**
   * The use of the inter-thread communication channel is disabled.
   * See #MHD_daemon_disable_itc().
   */ 
  bool disable_itc;

  /**
   * Disable #MHD_action_suspend() functionality.  See
   * #MHD_daemon_disallow_suspend_resume().
   */ 
  bool disallow_suspend_resume;

  /**
   * Disable #MHD_action_upgrade() functionality.  See
   * #MHD_daemon_disallow_upgrade().
   */ 
  bool disallow_upgrade;

  /**
   * Disables optional calls to `shutdown()` and enables aggressive
   * non-blocking optimistic reads and other potentially unsafe
   * optimizations.  See #MHD_daemon_enable_turbo().
   */
  bool enable_turbo;

  /**
   * Allow reusing the address:port combination when binding.
   * See #MHD_daemon_listen_allow_address_reuse().
   */
  bool allow_address_reuse;

    
};


/**
 * Action function implementing some action to be
 * performed on a request.
 *
 * @param cls action-specfic closure
 * @param request the request on which the action is to be performed
 */
typedef void
(*ActionCallback) (void *cls,
		   const struct MHD_Request *request);


/**
 * Actions are returned by the application to drive the request
 * handling of MHD.
 */
struct MHD_Action
{

  /**
   * Function to call for the action.
   */
  ActionCallback action;

  /**
   * Closure for @a action
   */ 
  void *action_cls;
  
};


/**
 * Representation of an HTTP response.
 */
struct MHD_Response
{

  /**
   * A response *is* an action. See also
   * #MHD_action_from_response().   Hence this field
   * must be the first field in a response!
   */
  struct MHD_Action action;
  
  /**
   * Headers to send for the response.  Initially
   * the linked list is created in inverse order;
   * the order should be inverted before sending!
   */
  struct MHD_HTTP_Header *first_header;

  /**
   * Buffer pointing to data that we are supposed
   * to send as a response.
   */
  char *data;

  /**
   * Closure to give to the content reader @e crc
   * and content reader free callback @e crfc.
   */
  void *crc_cls;

  /**
   * How do we get more data?  NULL if we are
   * given all of the data up front.
   */
  MHD_ContentReaderCallback crc;

  /**
   * NULL if data must not be freed, otherwise
   * either user-specified callback or "&free".
   */
  MHD_ContentReaderFreeCallback crfc;

  /**
   * Function to call once MHD is finished with 
   * the request, may be NULL.
   */
  MHD_RequestTerminationCallback termination_cb;

  /**
   * Closure for @e termination_cb.
   */
  void *termination_cb_cls;
  
#ifdef UPGRADE_SUPPORT
  /**
   * Application function to call once we are done sending the headers
   * of the response; NULL unless this is a response created with
   * #MHD_create_response_for_upgrade().
   */
  MHD_UpgradeHandler upgrade_handler;

  /**
   * Closure for @e uh.
   */
  void *upgrade_handler_cls;
#endif /* UPGRADE_SUPPORT */

  /**
   * Mutex to synchronize access to @e data, @e size and
   * @e reference_count.
   */
  MHD_mutex_ mutex;

  /**
   * Set to #MHD_SIZE_UNKNOWN if size is not known.
   */
  uint64_t total_size;

  /**
   * At what offset in the stream is the
   * beginning of @e data located?
   */
  uint64_t data_start;

  /**
   * Offset to start reading from when using @e fd.
   */
  uint64_t fd_off;

  /**
   * Number of bytes ready in @e data (buffer may be larger
   * than what is filled with payload).
   */
  size_t data_size;

  /**
   * Size of the data buffer @e data.
   */
  size_t data_buffer_size;

  /**
   * HTTP status code of the response.
   */
  enum MHD_HTTP_StatusCode status_code;
  
  /**
   * Reference count for this response.  Free once the counter hits
   * zero.
   */
  unsigned int reference_count;

  /**
   * File-descriptor if this response is FD-backed.
   */
  int fd;

  /**
   * Only respond in HTTP 1.0 mode.
   */
  bool v10_only;
  
};



#endif