#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include "error.h"
#include "common.h"

int parse_option(int argc, char **argv, char *port_num)
{
    int opt;
    if (argc < 2) {
        return 1;
    }
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
        case 'p':
            strcpy(port_num, optarg);
            break;
        default:
            return 1;
            break;
        }
    }
    return 0;
}

enum error_code receive_file(int socket, char *file_name)
{
    enum error_code ret = ERROR_SYSTEM;
    FILE *file = NULL;
    ssize_t recv_bytes;
    char buffer[BUFFER_SIZE];

    file = fopen(file_name, "w");
    if (file == NULL) {
        ret = ERROR_FILE_OPEN;
        set_error(ERROR_FILE_OPEN, errno);
        goto end;
    }
    while ((recv_bytes = recv(socket, buffer, BUFFER_SIZE, MSG_WAITALL)) > 0) {
        if (fwrite(buffer, 1, recv_bytes, file) < recv_bytes) {
            ret = ERROR_RECEIVED;
            set_error(ERROR_RECEIVED, errno);
            goto end;
        } 
    }
    if (recv_bytes < 0) {
        ret = ERROR_RECEIVED;
        set_error(ERROR_RECEIVED, errno);
        goto end;
    }
    ret = NORMAL;
end:
    if (file) {
        fclose(file);
    }
    return ret;
}

enum error_code read_file_and_print(char *file_name)
{
    enum error_code ret = ERROR_SYSTEM;
    FILE *file = NULL;
    ssize_t read_bytes;
    char buffer[BUFFER_SIZE];

    //　標準出力のバッファの設定。改行文字がないファイルだと標準出力しないので修正
    if (setvbuf(stdout, buffer, _IONBF ,read_bytes) != 0) { 
        ret = ERROR_SYSTEM;
        set_error(ERROR_SYSTEM, errno);
        goto end;
    }

    file = fopen(file_name, "r");
    if (file == NULL) {
        ret = ERROR_FILE_OPEN;
        set_error(ERROR_FILE_OPEN, errno);
        goto end;
    }
    while ((read_bytes = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (fwrite(buffer, 1, read_bytes, stdout) < read_bytes) {
            ret = ERROR_SYSTEM;
            set_error(ERROR_SYSTEM, errno);
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

enum error_code handle_tcp_client(int clnt_socket)
{
    enum error_code ret = ERROR_SYSTEM;
    char buffer[BUFFER_SIZE];
    ssize_t recv_msg_size;

    // クライアントからのデータ受信
    if ((recv_msg_size = recv(clnt_socket, buffer, BUFFER_SIZE, MSG_WAITALL)) < 0) {
        ret = ERROR_RECEIVED;
        goto end;
    }

    while (recv_msg_size > 0) {
        if (sendn(clnt_socket, buffer, recv_msg_size) != -1) {
            ret = ERROR_SEND;
            goto end;
        }
        if ((recv_msg_size = recv(clnt_socket, buffer, BUFFER_SIZE, MSG_WAITALL)) < 0) {
            ret = ERROR_RECEIVED;
            goto end;
        }
    }
end:
    set_error(ret, errno);
    close(clnt_socket);
    return ret;
}

int main(int argc, char *argv[])
{
    enum error_code ret = ERROR_SYSTEM;
    char port_num[PORTNUM_MAX_LEN] = {0};
    int lfd, cfd, optval, reqLen;

    ssize_t read_bytes;
    
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    
    // オプション解析
    if (parse_option(argc, argv, port_num)) {
        ret = ERROR_ARGUMENT;
        set_error(ret, errno);
        goto end;
    } 

    // getaddrinfo()の準備
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    int status = getaddrinfo(NULL, port_num, &hints, &result);
    if (status) {
        set_error(ERROR_SYSTEM, status);
        goto end;
    }

    // ソケットの生成とアドレスのバインド
    lfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (lfd == -1) {
        ret = ERROR_SOCKET;
        set_error(ret, errno);
        goto end;
    }

    if (bind(lfd, result->ai_addr, result->ai_addrlen)) {
        ret = ERROR_BIND;
        set_error(ret, errno);
        goto end;
    }

    if (listen(lfd, SOMAXCONN)) {
        ret = ERROR_LISTEN;
        set_error(ret, errno);
        goto end;
    }

    for (;;) {
        cfd = accept(lfd, NULL, NULL);
        if (cfd == -1) {
            continue;
        }
        if (receive_file(cfd, "recvfile.txt")) {
            goto end;
        }
        if (read_file_and_print("recvfile.txt")) {
            goto end;
        }
    }
    
end:
    if (result) {
        freeaddrinfo(result);
    }

    if (cfd) {
        close(cfd);
    }
    print_error();
    return ret;
}