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

int parse_option(int argc, char **argv, char *port_num, char *full_file_path)
{
    int opt;
    if (argc < 2) {
        return 1;
    }
    while ((opt = getopt(argc, argv, "p:s:d")) != -1) {
        switch (opt) {
        case 'd':
            debug_mode = true;
            break;
        case 'p':
            strcpy(port_num, optarg);
            break;
        case 's':
            strcpy(full_file_path, optarg);
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

enum error_code connect_client(int *lfd, char *port_num)
{
    enum error_code ret = ERROR_SYSTEM;
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    
    struct timeval timeout; // SO_RCVTIMEOの設定値
    timeout.tv_sec = 20;
    timeout.tv_usec = 0;

    int opt_val = 1; // SO_REUSEADDRの設定値

    // getaddrinfo()の準備
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    int status = getaddrinfo(NULL, port_num, &hints, &result);
    if (status) {
        ret = ERROR_SYSTEM;
        set_error(ERROR_SYSTEM, status);
        goto end;
    }

    // ソケットの生成とアドレスのバインド
    *lfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (*lfd == -1) {
        ret = ERROR_SOCKET;
        set_error(ret, errno);
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, " Created a socket on port %s", port_num);

    if (setsockopt(*lfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout)) {
        ret = ERROR_SOCKET;
        set_error(ret, errno);
        goto end;
    }

    if (setsockopt(*lfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof opt_val)) {
        ret = ERROR_SOCKET;
        set_error(ret, errno);
        goto end;
    }
    
    DEBUG_MACRO(debug_mode, true, " Set socket options SO_RCVTIMEO and SO_REUSEADDR");
    
    if (bind(*lfd, result->ai_addr, result->ai_addrlen)) {
        ret = ERROR_BIND;
        set_error(ret, errno);
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, " Bound to port %s", port_num);

    if (listen(*lfd, SOMAXCONN)) {
        ret = ERROR_LISTEN;
        set_error(ret, errno);
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, " Listening on port %s", port_num);
    
    ret = NORMAL;
end:
    if (result) {
        freeaddrinfo(result);
    }
    return ret;
    
}

enum error_code verify_data_size(struct f_message f_msg, char *file_name)
{
    enum error_code ret = ERROR_SYSTEM;
    unsigned long long recv_file_size;
    if (get_file_size(file_name, &recv_file_size)) {
        ret = ERROR_SYSTEM;
        goto end;
    }

    if (f_msg.file_size == recv_file_size) { // 注意！！ デバッグ用で変更 正しくは ==
        ret = NORMAL;
        goto end;
    } else {
        ret = ERROR_DIFF_FILESIZE;
    }

end:
    return ret;

}

enum error_code begin_session(int cfd, struct f_message *f_msg)
{
    enum error_code ret = ERROR_SYSTEM;
    

    if (receive_f_msg(cfd, f_msg)) { // clientからのf_msgを受信①
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, "received f_msg %s:%llu", f_msg->file_name, f_msg->file_size);

    if ((ret = send_a_msg(cfd))) { // serverに対してa_msgを送信③
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, "sended a_msg");

    ret = NORMAL;
end:
    return ret;
}

enum error_code put_session(int lfd, char *file_path)
{
    enum error_code ret = ERROR_SYSTEM;
    int cfd = -1;
    struct f_message f_msg = {0};

    for (;;) {
        cfd = accept(lfd, NULL, NULL);
        if (cfd == -1) {
            continue;
        }

        if ((ret = begin_session(cfd, &f_msg))) {
            goto end;
        }

        DEBUG_MACRO(debug_mode, true, "==== begin session success ====");
        
        if (receive_file(cfd, file_path)) { // clientから送られるファイルを受け取り、保存する④
            goto end;
        }

        DEBUG_MACRO(debug_mode, true, "received file :%s", file_path);
        
        if ((ret = verify_data_size(f_msg, file_path))) { // ファイルのデータサイズ検証⑥ サイズに問題なければ、a_msgをclientに送信
            if (ret == ERROR_DIFF_FILESIZE) {
                if ((ret = send_e_msg(cfd, "The specified file size does not match the received file size."))) {
                    goto end;
                }
                goto end; // DIFF FILESIZEの場合はここで終了
            } else {
                goto end; // verify_data_sizeでERROR_SYSTEMが返った場合はここで終了
            }
        }

        DEBUG_MACRO(debug_mode, true, "verified file size :%llu", f_msg.file_size);

        if ((ret = send_a_msg(cfd))) { // a_msgをclientに送信 ⑦
            goto end;
        }

        DEBUG_MACRO(debug_mode, true, "sended a_msg");

        ret = NORMAL;
        goto end; // 1回の接続で1ファイル受信したら終了
    }

end:
    if (cfd) {
        close(cfd);
    }
}

int main(int argc, char *argv[])
{
    enum error_code ret = ERROR_SYSTEM;
    char port_num[PORTNUM_MAX_LEN] = {0};
    char default_file_name[FILENAME_MAX_LEN] = "/recvfile.txt"; 
    char current_dir[MAX_PATH_LEN] = {0}; // 実行場所のpath
    char full_file_path[MAX_PATH_LEN * 2] = {0};
    int lfd = -1;

    if (parse_option(argc, argv, port_num, full_file_path)) { // オプション解析
        ret = ERROR_ARGUMENT;
        set_error(ret, errno);
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, "==== parse_option success ====");

    // nochdir = 1を指定して、daemon()がカレントディレクトリを変更しないようにする
    if (daemon(1, 0) != 0) { 
        ret = ERROR_SYSTEM;
        set_error(ret, errno);
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, "==== daemonized success ====");

    if (*full_file_path == '\0') { // -sオプションがない場合、カレントディレクトリに指定の名称で保存
        if (getcwd(current_dir, sizeof(current_dir)) != NULL) {
            snprintf(full_file_path, sizeof(full_file_path), "%s%s", current_dir, default_file_name);
        } else {
            ret = ERROR_SYSTEM;
            set_error(ret, errno);
            goto end;
        }
    }

    if ((ret = connect_client(&lfd, port_num))) { // サーバー接続処理
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, "==== connect client success ====");
 
    if ((ret = put_session(lfd, full_file_path))) { // ファイル転送処理
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, "==== put session success ====");
    
end:
    if (lfd) {
        close(lfd);
    }

    print_error();
    return ret;
}