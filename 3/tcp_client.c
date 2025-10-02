#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <fcntl.h>
#include "error.h"
#include "common.h"
#include "socket_msg.h"

static bool debug_mode = false;

int parse_option(int argc, char **argv, char *host_name ,char *port_num, char *file_name)
{
    int opt;
    if (argc < 2) {
        return 1;
    }
    while ((opt = getopt(argc, argv, "h:p:f:d")) != -1) {
        switch (opt) {
        case 'd':
            debug_mode = true;
            break;
        case 'h':
            strcpy(host_name, optarg);
            break;
        case 'p':
            strcpy(port_num, optarg);
            break;
        case 'f':
            strcpy(file_name, optarg);
            break;
        default:
            return 1;
        }
    }
    return 0;
}

enum error_code get_file_size(const char *file_name, unsigned long long *file_size)
{
    struct stat stat_buf;

    if (stat(file_name, &stat_buf) == 0) {
        *file_size = stat_buf.st_size;
        return NORMAL;
    }

    set_error(ERROR_SYSTEM, errno);
    return ERROR_SYSTEM;
}

enum error_code close_file_descriptor(int fd)
{
    if (close(fd) == -1) {
        set_error(ERROR_SYSTEM, errno);
        return ERROR_SYSTEM;
    }
    return NORMAL;
}

enum error_code send_file(int socket, char *file_name)
{
	enum error_code ret = ERROR_SYSTEM;
    int fd = -1;
    ssize_t read_bytes;
    char buffer[BUFFER_SIZE];

    fd = open(file_name, O_RDONLY);
    if (fd == -1) {
        ret = ERROR_FILE_OPEN;
        set_error(ERROR_FILE_OPEN, errno);
        return ret;
    }
    while ((read_bytes = read(fd, buffer, BUFFER_SIZE)) > 0) {
        if (sendn(socket, buffer, read_bytes) == -1) {
            ret = ERROR_SEND;
            set_error(ERROR_SEND, errno);
            goto end;
        } 
    }
    ret = NORMAL;
end:
    ret = close_file_descriptor(fd);
    return ret;
}

enum error_code send_shutdown(int cfd)
{   
	enum error_code ret = ERROR_SYSTEM;
    if (shutdown(cfd, SHUT_WR)) {
        ret = ERROR_SYSTEM;
        set_error(ERROR_SYSTEM, errno);
        goto end;
    }

    ret = NORMAL;
end:
    return ret;
}

enum error_code connect_server(int *cfd, char *server_ip, char *port_num)
{
	enum error_code ret = ERROR_SYSTEM;
    struct addrinfo hints;
    struct addrinfo *result = NULL;

    struct timeval timeout; // SO_RCVTIMEOの設定値
    timeout.tv_sec = 20;
    timeout.tv_usec = 0;

    // getaddrinfo()の準備
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    int status = getaddrinfo(server_ip, port_num, &hints, &result);
    if (status != 0) {
        set_error(ERROR_SYSTEM, status);
        goto end;
    }

    *cfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (*cfd == -1) {
        ret = ERROR_SOCKET;
        set_error(ret, errno);
        goto end;
    }

    DEBUG_MACRO(debug_mode, false, " Created a socket %s:%s", server_ip, port_num);

    if (connect(*cfd, result->ai_addr, result->ai_addrlen)) {
        ret = ERROR_CONNECT;
        set_error(ret, errno);
        goto end;
    }

    DEBUG_MACRO(debug_mode, false, "connected to %s:%s", server_ip, port_num);

    if (setsockopt(*cfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout)) {
        ret = ERROR_SOCKET;
        set_error(ret, errno);
        goto end;
    }

    DEBUG_MACRO(debug_mode, false, "socket option configured %s:%s", server_ip, port_num);

    ret = NORMAL;
end:
    if (result) {
        freeaddrinfo(result);
    }    
    return ret;

}

enum error_code begin_session(char *file_name, int cfd)
{
	enum error_code ret = ERROR_SYSTEM;
    unsigned long long file_size;
    ssize_t recv_bytes;
    char msg_type = {0}; // debug用

    struct a_message a_msg = {0};
    struct e_message e_msg = {0};

    if ((ret = get_file_size(file_name, &file_size))) { // 送信するファイルサイズの確認
        goto end;
    }

    if ((ret = send_f_msg(cfd, file_size, file_name))) { // f_msgとしてファイルのname+sizeを送信①
        goto end;
    }

    DEBUG_MACRO(debug_mode, false, "sended f_msg %s, file size = %llu", file_name, file_size);

    recv_bytes = recvn(cfd, &msg_type, sizeof(char), MSG_PEEK);

    if (recv_bytes < 0) {
        if (recv_bytes == -2) {
            send_reset_packet(cfd);
            set_error(ERROR_TIMEOUT, errno);
            ret = ERROR_TIMEOUT;
        } else {
            set_error(ERROR_RECEIVED, errno);
            ret = ERROR_RECEIVED;
        }
        goto end;
    }

    switch (msg_type) { // serverからの応答メッセージのタイプを確認⑥
    case 'A':
        if ((ret = receive_a_msg(cfd, &a_msg))) { // a_msgをserverから受信
            goto end;
        }
        DEBUG_MACRO(debug_mode, false, "received a_msg");
        break;
    case 'E':
        if ((ret = receive_e_msg(cfd, &e_msg))) { // e_msgをserverから受信
            goto end;
        }
        set_error(ERROR_LOCK_EXISTS, errno);
        ret = ERROR_LOCK_EXISTS;
        DEBUG_MACRO(debug_mode, false, "received e_msg : LOCK FILE EXIST ERORR");
        goto end;
    default:
        ret = ERROR_RECEIVED;
        goto end;
    }

    ret = NORMAL;
end:
    return ret;
}

enum error_code put_session(int cfd, char *file_name)
{
	enum error_code ret = ERROR_SYSTEM;
    struct a_message a_msg = {0};
    struct e_message e_msg = {0};
    ssize_t recv_bytes;
    char msg_type = {0}; // debug用

    if ((ret = send_file(cfd, file_name))) { // ファイル転送処理 ④
        goto end;
    }
    
    DEBUG_MACRO(debug_mode, false, "sended file :%s", file_name);
    
    if ((ret = send_shutdown(cfd))) { // SHUT_WRをserverに送信 ⑤
        goto end;
    }
    
    DEBUG_MACRO(debug_mode, false, "sended shutdown packet");

    recv_bytes = recvn(cfd, &msg_type, sizeof(char), MSG_PEEK);

    if (recv_bytes < 0) {
        if (recv_bytes == -2) {
            send_reset_packet(cfd);
            set_error(ERROR_TIMEOUT, errno);
            ret = ERROR_TIMEOUT;
        } else {
            set_error(ERROR_RECEIVED, errno);
            ret = ERROR_RECEIVED;
        }
        goto end;
    }

    switch (msg_type) { // serverからの応答メッセージのタイプを確認⑥
    case 'A':
        if ((ret = receive_a_msg(cfd, &a_msg))) { // a_msgをserverから受信
            goto end;
        }
        DEBUG_MACRO(debug_mode, false, "received a_msg");
        break;
    case 'E':
        if ((ret = receive_e_msg(cfd, &e_msg))) { // e_msgをserverから受信
            goto end;
        }
        ret = ERROR_DIFF_FILESIZE;
        DEBUG_MACRO(debug_mode, false, "received e_msg: DATA SIZE DIFFERENT ERROR");
        goto end;
    default:
        ret = ERROR_RECEIVED;
        goto end;
    }

    ret = NORMAL;

end:
    return ret;
}

int main(int argc, char *argv[])
{
	enum error_code ret = ERROR_SYSTEM;
    char server_ip[FILENAME_MAX_LEN] = {0};
    char file_name[FILENAME_MAX_LEN] = {0};
    char port_num[PORTNUM_MAX_LEN] = {0};
    int cfd = -1;

    if (parse_option(argc, argv, server_ip, port_num, file_name)) { // オプション解析
        ret = ERROR_ARGUMENT;
        set_error(ret, errno);
        goto end;
    }

    DEBUG_MACRO(debug_mode, false, "==== parse_option success ====");

    if ((ret = connect_server(&cfd, server_ip, port_num))) {
        goto end;
    }

    DEBUG_MACRO(debug_mode, false, "==== connect server success ====");

    if ((ret = begin_session(file_name, cfd))) {
        goto end;
    }

    DEBUG_MACRO(debug_mode, false, "==== begin session success ====");

    if ((ret = put_session(cfd, file_name))) {
        goto end;
    }

    DEBUG_MACRO(debug_mode, false, "==== put session success ====");

    ret = NORMAL;

end:
    ret = close_file_descriptor(cfd);
    print_error();
    return ret;
}