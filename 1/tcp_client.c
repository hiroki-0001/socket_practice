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

int parse_option(int argc, char **argv, char *host_name ,char *port_num, char *file_name)
{
    int opt;
    while ((opt = getopt(argc, argv, "h:p:f:")) != -1) {
        switch (opt) {
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

enum error_code send_file(int socket, char *file_name)
{
	enum error_code ret = ERROR_SYSTEM;
    FILE *file;
    ssize_t bytes_read;
    char buffer[BUFFER_SIZE];

    file = fopen(file_name, "r");
    if (file == NULL) {
        ret = ERROR_FILE_OPEN;
        set_error(ERROR_FILE_OPEN, errno);
        goto end;
    }
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (send(socket, buffer, bytes_read, 0) < 0) {
            ret = ERROR_SEND;
            set_error(ERROR_SEND, errno);
            goto end;
        } 
    }
    ret = NORMAL;
end:
    fclose(file);
    return ret;
}

int main(int argc, char *argv[])
{
	enum error_code ret = ERROR_SYSTEM;
    struct sockaddr_in echo_server_addr;
    char server_ip[FILENAME_MAX_LEN] = {0};
    char file_name[FILENAME_MAX_LEN] = {0};
    char port_num[FILENAME_MAX_LEN] = {0};
    char buffer[BUFFER_SIZE];
    unsigned int echo_string_len;
    int byte_revd, total_byte_revd;
    FILE *file;
    ssize_t bytes_read;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int cfd;

    if (parse_option(argc, argv, server_ip, port_num, file_name)) {
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
    hints.ai_flags = AI_NUMERICSERV;

    if (getaddrinfo(server_ip, port_num, &hints, &result) != 0) {
        set_error(ERROR_SYSTEM, errno);
        ret = ERROR_SYSTEM;
        goto end;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        cfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (cfd == -1) {
            continue;
        }
        if (connect(cfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;      /* Success */
        }
        close(cfd);
    }

    if (rp == NULL) { // serverとのconnectに失敗
        set_error(ERROR_CONNECT, errno);
        ret = ERROR_CONNECT;
        goto end;
    }
    
    freeaddrinfo(result);

    if ((ret = send_file(cfd, file_name)) != 0) { // ファイル転送処理
        goto end;
    }

    echo_string_len = strlen(file_name);
    total_byte_revd = 0;

end:
    close(cfd);
    print_error();
    return ret;
}