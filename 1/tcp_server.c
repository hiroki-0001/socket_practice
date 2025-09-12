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
    FILE *file;
    ssize_t bytes_revd;
    char buffer[BUFFER_SIZE];

    file = fopen(file_name, "w");
    if (file == NULL) {
        ret = ERROR_FILE_OPEN;
        set_error(ERROR_FILE_OPEN, errno);
        goto end;
    }
    while ((bytes_revd = recv(socket, buffer, BUFFER_SIZE, 0)) > 0) {
        if (fwrite(buffer, 1, bytes_revd, file) < bytes_revd) {
            ret = ERROR_RECEIVED;
            set_error(ERROR_RECEIVED, errno);
            goto end;
        } 
    }
    if (bytes_revd < 0) {
        ret = ERROR_RECEIVED;
        set_error(ERROR_RECEIVED, errno);
        goto end;
    }
    ret = NORMAL;
end:
    fclose(file);
    return ret;
}

int read_file_and_print(char *file_name)
{
    FILE *file;
    ssize_t bytes_read;
    char buffer[BUFFER_SIZE];

    file = fopen(file_name, "r");
    if (file == NULL) {
        set_error(ERROR_FILE_OPEN, errno);
        return ERROR_FILE_OPEN;
    }
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (fwrite(buffer, 1, bytes_read, stdout) < bytes_read) {
            set_error(ERROR_SYSTEM, errno);
            fclose(file);
            return ERROR_SYSTEM;
        } 
    }
    fclose(file);
    return NORMAL;
}

enum error_code handle_tcp_client(int clnt_socket)
{
    enum error_code ret = ERROR_SYSTEM;
    char buffer[BUFFER_SIZE];
    ssize_t recv_msg_size;

    // クライアントからのデータ受信
    if ((recv_msg_size = recv(clnt_socket, buffer, BUFFER_SIZE, 0)) < 0) {
        ret = ERROR_RECEIVED;
        goto end;
    }

    while (recv_msg_size > 0) {
        if (send(clnt_socket, buffer, recv_msg_size, 0) != recv_msg_size) {
            ret = ERROR_SEND;
            goto end;
        }
        if ((recv_msg_size = recv(clnt_socket, buffer, BUFFER_SIZE, 0)) < 0) {
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
    char port_num[FILENAME_MAX_LEN] = {0};
    int lfd, cfd, optval, reqLen;

    FILE *file;
    ssize_t bytes_read;
    
    socklen_t addrlen;
    struct sockaddr_storage claddr;
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
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    if (getaddrinfo(NULL, port_num, &hints, &result) != 0) {
        set_error(ERROR_SYSTEM, errno);
        ret = ERROR_SYSTEM;
        goto end;
    }

    // ソケットの生成とアドレスのバインド
    optval = 1;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        lfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (lfd == -1) {
            continue;
        }
        if (setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) { // socketのオプション設定
            set_error(ERROR_SYSTEM, errno);
            ret = ERROR_SYSTEM;
            goto end;
        } 
        if (bind(lfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;  /* Success */
        }
        close(lfd);
    }

    if (rp == NULL) {  // ソケットの生成とアドレスのバインドに失敗場合はconnect errorとして処理
        set_error(ERROR_CONNECT, errno);
        ret = ERROR_CONNECT;
        goto end;
    }

    if (listen(lfd, MAXPENDING) < 0) {
        ret = ERROR_LISTEN;
        set_error(ret, errno);
        goto end;
    }

    freeaddrinfo(result);

    for (;;) {
        addrlen = sizeof(struct sockaddr_storage);
        cfd = accept(lfd, (struct sockaddr *) &claddr, &addrlen);
        if (cfd == -1) {
            continue;
        }
        receive_file(cfd, "recvfile.txt");
        read_file_and_print("recvfile.txt");
    }

end:
    close(cfd);
    print_error();
    return ret;
}