//
// Created by lixutong on 20-5-9.
//

#include "http_conn.h"
using namespace threadpoolsvr;


/* status information of HTTP response */
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

/* root of website */
const char *doc_root = "/var/www/html";


/* modify fd attr to nonblocking */
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/* register fd in epoll kernel event table */
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;  // data read | ET | TCP disconnect
    if (one_shot)   event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/* remove fd from kernel event table */
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/* modify registered event of fd */
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/* close connection */
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count --;    // close a connection, count of user minus 1
    }
}

/* init the new connection */
void http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;

    // to avoid TIME_WAIT, for debugging
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, sockfd, true);
    m_user_count ++;
    init();
}

/* init connection */
void http_conn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/* loop the client data until there is no data to read or the client closes the connection */
bool http_conn::read() {
    if (m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)    // non-blocking return, no data to read
                break;
            return false;
        }
        else if (bytes_read == 0)
            return false;
        m_read_idx += bytes_read;
    }
    return true;
}

/* slave state machine, to get a complete line*/
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    // m_read_buf[0, m_checked_idx) is analysed
    // m_read_buf[m_checked_idx, m_read_idx) is to be analysed
    for ( ; m_checked_idx < m_read_idx; ++ m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx)  // \r is the last char, the line is not complete
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n') {   // get the complete line
                m_read_buf[m_checked_idx ++] = '\0';
                m_read_buf[m_checked_idx ++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;    // there isn't \n after \r, so the line is invalid
        }
        else if (temp == '\n') {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {   // get the complete line
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx ++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;    // there isn't \r before \n, so the line is invalid
        }
    }
    return LINE_OPEN;   // if there isn't \r or \n, the line is not complete
}


// HTTP request example:
// GET http://www.baidu.com/index.html HTTP/1.0
// User-Agent: Wget/1.12 (linux-gnu)
// Host: www.baidu.com
// Connection: close


/* parse HTTP request line */
/* to get request method, URL and HTTP version */
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    m_url = strpbrk(text, " \t");   // get the space or \t before url
    if (!m_url)
        return BAD_REQUEST;
    *m_url ++ = '\0';   // get url

    char *method = text;
    if (strcasecmp(method, "GET") == 0) // this server only support GET method
        m_method = GET;
    else
        return BAD_REQUEST;

    m_url += strspn(m_url, " \t");  // remove space and \t
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version ++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0) // this server only support HTTP/1.1
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0) {    // check validity of url
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0]!= '/')
        return BAD_REQUEST;
    m_check_state = CHECK_STATE_HEADER; // finish request line parse, convert state to analysis of header field
    return NO_REQUEST;
}

/* parse HTTP header field */
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    if (text[0] == '\0') {  // found an empty line, means header parsing finishes
        if (m_content_length != 0) {    // if HTTP request has message body, convert state to analysis of message body
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST; // we get a complete HTTP request
    }
    else if (strncasecmp(text, "Connection:", 11) == 0) {   // parse Connection field
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
            m_linger = true;        // keep connection
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0) {   // parse Content-Length field
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0) {  // parse Host field
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
        printf("Unkown header %s\n", text);
    return NO_REQUEST;
}

/* determine whether the message body has been read completely */
/* without parsing */
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if (m_read_idx >= m_content_length + m_checked_idx) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/* master state machine, to parse HTTP request */
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;  // status of current line
    char *text = 0;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || (line_status = parse_line()) == LINE_OK) {
        text = get_line();  // get a line (m_read_buf + m_start_line)
        m_start_line = m_checked_idx;   // record start position of next line
        printf("got 1 http line: %s\n", text);

        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {     // parse request line
                HTTP_CODE ret = parse_request_line(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER: {          // parse header field
                HTTP_CODE ret = parse_headers(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if (ret == GET_REQUEST)
                    return do_request();
                break;
            }
            case CHECK_STATE_CONTENT: {         // parse message body
                HTTP_CODE ret = parse_content(text);
                if (ret == GET_REQUEST)
                    return do_request();
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

/* when get a complete and correct HTTP request, analysis it */
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    if (!(m_file_stat.st_mode & S_IROTH))   // whether can be read by other group
        return FORBIDEEN_REQUEST;
    if (S_ISDIR(m_file_stat.st_mode))   // whether directory
        return BAD_REQUEST;
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);   // use mmap to map the file to the memory address m_file_address
    close(fd);
    return FILE_REQUEST;
}

/* upmap memory */
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/* write data to write buffer */
bool http_conn::add_response(const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= WRITE_BUFFER_SIZE - 1 - m_write_idx)
        return false;
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

/* add status line to response */
bool http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/* add header field to response */
bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

/* add content length to response header */
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

/* add connection to response header */
bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

/* add blank line to response header, means the end of the header */
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

/* add message body to response */
bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

/* write HTTP response according the HTTP request process result */
bool http_conn::process_write(http_conn::HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
                return false;
            break;
        }
        case BAD_REQUEST: {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form))
                return false;
            break;
        }
        case NO_RESOURCE: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
                return false;
            break;
        }
        case FORBIDEEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
                return false;
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

/* entry function for processing HTTP request */
/* called by thread in threadpool */
void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
        close_conn();
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

/* write HTTP response */
bool http_conn::write() {
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }
    while (true) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            if (errno == EAGAIN) {  // if write buffer has no space, wait for next EPOLLOUT event
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_to_send <= bytes_have_send) { // send HTTP response success
            unmap();
            if (m_linger) {     // keep connection
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }
            else {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}
































