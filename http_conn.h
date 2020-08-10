//
// Created by lixutong on 20-5-9.
//

#ifndef THREADPOOL_SVR_HTTP_CONN_H
#define THREADPOOL_SVR_HTTP_CONN_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"

namespace threadpoolsvr {

class http_conn {
public:
    static const int FILENAME_LEN = 200;        // max length of filename
    static const int READ_BUFFER_SIZE = 2048;   // size of read buffer
    static const int WRITE_BUFFER_SIZE = 1024;  // size of write buffer
    enum METHOD {   // http request method, in this server, we only have GET method
        GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH
    };
    enum CHECK_STATE {  // the state of the master state machine when parsing a client request
        CHECK_STATE_REQUESTLINE = 0,        // in process of parsing request line
        CHECK_STATE_HEADER,                 // in process of parsing header field
        CHECK_STATE_CONTENT                 // in process of parsing HTTP message body
    };
    enum HTTP_CODE {    // possible results of the server processing HTTP requests
        NO_REQUEST,             // the request is incomplete, need to continue reading the client data
        GET_REQUEST,            // get a complete client request
        BAD_REQUEST,            // there is a syntax error in the client request
        NO_RESOURCE,            // no resource file
        FORBIDEEN_REQUEST,      // client does not have sufficient access to the resource
        FILE_REQUEST,           // file request success
        INTERNAL_ERROR,         // server internal error
        CLOSED_CONNECTION       // client has closed the connection
    };
    enum LINE_STATUS {  // read status of the line
        LINE_OK = 0,            // the HTTP request line is complete
        LINE_BAD,               // the HTTP request line is bad
        LINE_OPEN               // the HTTP request line is incomplete, subsequent reads are required
    };

    http_conn() {};
    ~http_conn() {};

    void init(int sockfd, const sockaddr_in& addr);     // init the new connection
    void close_conn(bool real_close = true);            // close connection
    void process();                                     // process client request
    bool read();                                        // read operation without block
    bool write();                                       // write operation without block

    static int m_epollfd;                               // all socket events are registered in the same epoll kernel event table, so the epollfd is set to static
    static int m_user_count;                            // stat user count

private:
    void init();                                        // init connection
    HTTP_CODE process_read();                           // parse HTTP request
    bool process_write(HTTP_CODE ret);                  // fill HTTP response

    // the following set of functions is called by process_read to analyze the HTTP request
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();

    // the following set of functions is called by process_write to fill the HTTP response
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_len);
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blank_line();

    int m_sockfd;                                       // this HTTP connection's socket fd
    sockaddr_in m_address;                              // socket address of the other side
    char m_read_buf[READ_BUFFER_SIZE];                  // read buffer
    int m_read_idx;                                     // identifies the next position of the last byte of data that has been read in the read buffer
    int m_checked_idx;                                  // position of the char is being analysed
    int m_start_line;                                   // start position of the line is being analysed
    char m_write_buf[WRITE_BUFFER_SIZE];                // write buffer
    int m_write_idx;                                    // byte num to be sent in the write buffer
    CHECK_STATE m_check_state;                          // state of the master state machine
    METHOD m_method;                                    // request method
    char m_real_file[FILENAME_LEN];                     // The full path of the target file requested by the client, = doc_root + m_url (doc_root is the root directory of website)
    char *m_url;                                        // filename of the target file requested by client
    char* m_version;                                    // HTTP protocal version, we only support HTTP/1.1 */
    char *m_host;                                       // host name
    int m_content_length;                               // length of HTTP request message body
    bool m_linger;                                      // whether to keep connection of HTTP request
    char *m_file_address;                               // the starting position which the target file requested by the client is mmap to in memory
    struct stat m_file_stat;                            // status of the target file
    struct iovec m_iv[2];                               // use writev to process write operation, writev: write data from different buffer to fd
    int m_iv_count;                                     // number of memory blocks written

};

} // namespace threadpoolsvr

#endif //THREADPOOL_SVR_HTTP_CONN_H
