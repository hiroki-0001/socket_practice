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
            break;
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


enum error_code send_file(int socket, char *file_name)
{
	enum error_code ret = ERROR_SYSTEM;
    FILE *file = NULL;
    ssize_t read_bytes;
    char buffer[BUFFER_SIZE];

    file = fopen(file_name, "r");
    if (file == NULL) {
        ret = ERROR_FILE_OPEN;
        set_error(ERROR_FILE_OPEN, errno);
        return ret;
    }
    while ((read_bytes = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (sendn(socket, buffer, read_bytes) == -1) {
            ret = ERROR_SEND;
            set_error(ERROR_SEND, errno);
            goto end;
        } 
    }
    ret = NORMAL;
end:
    if (file) {
        fclose(file);
    }
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
    timeout.tv_sec = 1;
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

    if (debug_mode) {
        DEBUG_MACRO(false, " Created a socket %s:%s", server_ip, port_num);
    }

    if (connect(*cfd, result->ai_addr, result->ai_addrlen)) {
        ret = ERROR_CONNECT;
        set_error(ret, errno);
        goto end;
    }

    if (debug_mode) {
        DEBUG_MACRO(false, "connected to %s:%s", server_ip, port_num);
    }

    if (setsockopt(*cfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout)) {
        ret = ERROR_SOCKET;
        set_error(ret, errno);
        goto end;
    }

    if (debug_mode) {
        DEBUG_MACRO(false, "socket option configured %s:%s", server_ip, port_num);
    }

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
    struct a_message a_msg = {0};

    if ((ret = get_file_size(file_name, &file_size))) { // 送信するファイルサイズの確認
        goto end;
    }

    if ((ret = send_f_msg(cfd, file_size, file_name))) { // f_msgとしてファイルのname+sizeを送信①
        goto end;
    }

    if (debug_mode) {
        DEBUG_MACRO(false, "sended f_msg %s, file size = %llu", file_name, file_size);
    }

    if ((ret = receive_a_msg(cfd, &a_msg))) { // serverから送られてきたa_msgを受信③
        goto end;
    }

    if (debug_mode) {
        DEBUG_MACRO(false, "received a_msg :%s", (char *)&a_msg);
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
    char msg_type = {0}; // debug用

    if ((ret = send_file(cfd, file_name))) { // ファイル転送処理 ④
        goto end;
    }
    
    if (debug_mode) {
        DEBUG_MACRO(false, "sended file :%s", file_name);
    }

    if ((ret = send_shutdown(cfd))) { // SHUT_WRをserverに送信 ⑤
        goto end;
    }
    
    if (debug_mode) {
        DEBUG_MACRO(false, "sended shutdown packet");
    }

    switch (check_msg_type(cfd, &msg_type)) { // serverからの応答メッセージのタイプを確認⑥
    case 'A':
        if ((ret = receive_a_msg(cfd, &a_msg))) { // a_msgをserverから受信
            goto end;
        }
        break;
    case 'E':
        if ((ret = receive_e_msg(cfd, &e_msg))) { // e_msgをserverから受信
            goto end;
        }
        break;
    default:
        ret = ERROR_RECEIVED;
        goto end;
    }

    if (debug_mode) {
        DEBUG_MACRO(false, "received msg_type :%c", msg_type);
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

    if ((ret = connect_server(&cfd, server_ip, port_num))) {
        goto end;
    }

    if ((ret = begin_session(file_name, cfd))) {
        goto end;
    }

    if ((ret = put_session(cfd, file_name))) {
        goto end;
    }

    ret = NORMAL;

end:
    if (cfd) {
        close(cfd);
    }
    print_error();
    return ret;
}