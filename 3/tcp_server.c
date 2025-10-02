#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <pthread.h>
#include "error.h"
#include "common.h"
#include "socket_msg.h"

static bool debug_mode = false;

struct client_thread_args
{
    int cfd;
    char *server_base_path;
    bool debug_mode_enabled;
};

int parse_option(int argc, char **argv, char *port_num, char *full_file_path)
{
    int opt;
    if (argc < 2) {
        return -1;
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
            return -1;
        }
    }
    return 0;
}

enum error_code get_file_size(int fd, unsigned long long *file_size)
{
    struct stat file_info;

    if (fstat(fd, &file_info) != 0) {
        set_error(ERROR_SYSTEM, errno);
        return ERROR_SYSTEM;
    }
    
    *file_size = file_info.st_size;
    return NORMAL;
}

char *create_lock_file_name(char *origin_file_name)
{
    size_t len = strlen(origin_file_name);
    char *lock_file_name = (char *)malloc(len + strlen(".lock") + 1);
    if (lock_file_name == NULL) {
        set_error(ERROR_SYSTEM, errno);
        return NULL;
    }
    strcpy(lock_file_name, origin_file_name);
    strcat(lock_file_name, ".lock");
    return lock_file_name;
}

int open_lock_file(char *lock_file_name)
{
    int lock_fd = -1;

    lock_fd = open(lock_file_name, O_RDWR | O_CREAT | O_EXCL, 0644);

    if (lock_fd == -1){
        if (errno == EEXIST) {
            set_error(ERROR_LOCK_EXISTS, errno);
            return -2;
        } else {
            set_error(ERROR_LOCK_CREATE, errno);
            return -3;
        }
    }

    return lock_fd;

}

int open_recv_file(char *file_name)
{
    int file = -1;
    file = open(file_name, O_CREAT | O_RDWR, 0644);
    if (file == -1) {
        set_error(ERROR_FILE_OPEN, errno);
        return ERROR_FILE_OPEN;
    }
    return file;
}

void close_lock_file(char *lock_file_name)
{
    if (lock_file_name != NULL) {
        if (access(lock_file_name, F_OK) == 0) {
            if (unlink(lock_file_name) == -1) {
                set_error(ERROR_LOCK_REMOVE, errno);
            }
        }
        free(lock_file_name);
    }
}

enum error_code close_file_descriptor(int fd)
{
    if (close(fd) == -1) {
        set_error(ERROR_SYSTEM, errno);
        return ERROR_SYSTEM;
    }
    return NORMAL;
}

enum error_code receive_file(int socket, int file)
{
    enum error_code ret = ERROR_SYSTEM;
    ssize_t recv_bytes;
    char buffer[BUFFER_SIZE];

    while ((recv_bytes = recv(socket, buffer, BUFFER_SIZE, MSG_WAITALL)) > 0) {
        if (write(file, buffer, recv_bytes) < recv_bytes) {
            set_error(ERROR_RECEIVED, errno);
            ret = ERROR_RECEIVED;
            goto end;
        } 
    }
    if (recv_bytes < 0) {
        set_error(ERROR_RECEIVED, errno);
        ret = ERROR_RECEIVED;
        goto end;
    }
    ret = NORMAL;
end:
    return ret;
}

enum error_code setup_server(int *lfd, char *port_num)
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
    DEBUG_MACRO(debug_mode, true, " Set socket options SO_RCVTIMEO");

    if (setsockopt(*lfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof opt_val)) {
        ret = ERROR_SOCKET;
        set_error(ret, errno);
        goto end;
    }
    DEBUG_MACRO(debug_mode, true, " Set socket options SO_REUSEADDR");
    
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

enum error_code verify_data_size(unsigned long long file_size, int fd)
{
    enum error_code ret = ERROR_SYSTEM;
    unsigned long long recv_file_size;
    if ((ret = get_file_size(fd, &recv_file_size))) { // クライアントから受信したファイルサイズの取得
        goto end;
    }

    if (file_size != recv_file_size) {
        goto end;
    }
    ret = NORMAL;

end:
    return ret;
}

enum error_code concatenate_path(char *dir_path, char *file_name, char *full_path, int max_size)
{
    enum error_code ret = ERROR_SYSTEM;

    // dir_path の末尾が '/' で終わっているか確認
    size_t dir_len = strlen(dir_path);
    if (dir_path[dir_len - 1] == '/') { // '/' が既に存在する場合
        if (dir_len + strlen(file_name) >= max_size) {
            set_error(ERROR_BUFFER_OVERFLOW, errno);
            ret = ERROR_BUFFER_OVERFLOW; // バッファ不足
            goto end;
        }
        int written = snprintf(full_path, max_size, "%s%s", dir_path, file_name);
        if (written < 0 || written >= max_size) {
            set_error(ERROR_SYSTEM, errno); // システムエラー（ここではバッファ不足を含む）
            ret = ERROR_SYSTEM;
            goto end;
        }
    } else { // '/' を追加する場合
        if (dir_len + 1 + strlen(file_name) >= max_size) {
            set_error(ERROR_BUFFER_OVERFLOW, errno);
            ret = ERROR_BUFFER_OVERFLOW; // バッファ不足
            goto end;
        }
        int written = snprintf(full_path, max_size, "%s/%s", dir_path, file_name);
        if (written < 0 || written >= max_size) {
            set_error(ERROR_SYSTEM, errno);
            ret = ERROR_SYSTEM; // システムエラー（ここではバッファ不足を含む）
            goto end;
        }
    }
    ret = NORMAL;

end:
    return ret;    
}

enum error_code begin_session(int cfd, struct f_message *f_msg, int *fd, int *lock_fd, char **file_path, char **lock_file_path)
{
    enum error_code ret = ERROR_SYSTEM;
    char full_path[MAX_PATH_LEN] = {0};
    
    if ((ret = receive_f_msg(cfd, f_msg))) { // clientからのf_msgを受信①
        goto end;
    }
    DEBUG_MACRO(debug_mode, true, "received f_msg %s:%llu", f_msg->file_name, f_msg->file_size);

    if (concatenate_path(*file_path, f_msg->file_name, full_path, sizeof(full_path))) {
        goto end;
    }

    *lock_file_path = create_lock_file_name(full_path);
    if (*lock_file_path == NULL) {
        ret = ERROR_SYSTEM;
        goto end;
    }

    // ロックファイルのオープン
    *lock_fd = open_lock_file(*lock_file_path);
    if (*lock_fd < 0) { // ロックファイルのエラー処理
        switch (*lock_fd) {
        case -2:
            if ((ret = send_e_msg(cfd, "lock file exist."))) { // serverに対してa_msgを送信③
                goto end;
            }
            ret = ERROR_LOCK_EXISTS;
            break;

        case -3:
            if ((ret = send_e_msg(cfd, "lock file create error."))) { // serverに対してa_msgを送信③
                goto end;
            }
            ret = ERROR_LOCK_CREATE;
            break;
        
        default:
            if ((ret = send_e_msg(cfd, "error occurred related to the lock file."))) { // serverに対してa_msgを送信③
               goto end;
            }
            ret = ERROR_SYSTEM;
            break;
        }
        goto end;
    }

    // 受信ファイルのオープン
    *fd = open_recv_file(full_path);
    if (*fd < 0) { // 受信ファイルのエラー処理
        ret = ERROR_FILE_OPEN;
        goto end;
    }

    if ((ret = send_a_msg(cfd))) { // serverに対してa_msgを送信③
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, "sended a_msg");

    ret = NORMAL;
end:
    return ret;
}

enum error_code put_session(int cfd, unsigned long long file_size, int fd, int lock_fd, char *lock_file_path)
{
    enum error_code ret = ERROR_SYSTEM;

    if (receive_file(cfd, fd)) { // clientから送られるファイルを受け取り、保存する④
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, "received file :\n");
    
    if ((ret = verify_data_size(file_size, fd))) { // ファイルのデータサイズ検証⑥ サイズに問題なければ、a_msgをclientに送信
        if (ret != ERROR_DIFF_FILESIZE) {
            if ((ret = send_e_msg(cfd, "The specified file size does not match the received file size."))) {
                goto end;
            }
        }
        goto end; 
    }

    DEBUG_MACRO(debug_mode, true, "verified file size :%llu", file_size);

    if ((ret = send_a_msg(cfd))) { // a_msgをclientに送信 ⑦
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, "sended a_msg");

    ret = NORMAL;

end:
    ret = close_file_descriptor(fd);
    close_lock_file(lock_file_path);
    return ret;
}

void *handle_client(void *thread_args) 
{
    struct client_thread_args *args = (struct client_thread_args *)thread_args;

    int cfd = args->cfd;
    char *file_path = args->server_base_path;
    bool current_debug_mode = args->debug_mode_enabled;

    struct f_message f_msg = {0};
    char *lock_file_path = NULL;

    int fd = -1; // 受信ファイルのディスクリプタ
    int lock_fd = -1; // ロックファイルディスクリプタ

    DEBUG_MACRO(current_debug_mode, true, "NEW Client connected");
    
    if (begin_session(cfd, &f_msg, &fd, &lock_fd, &file_path, &lock_file_path)) {
        goto end;
    }
    DEBUG_MACRO(current_debug_mode, true, "==== begin session success ====");

    if (put_session(cfd, f_msg.file_size, fd, lock_fd, lock_file_path)) {
        goto end;
    }
    DEBUG_MACRO(debug_mode, true, "==== put session success ====");

end:
    if (args != NULL) {
        free(args);
    }

    return NULL;
    
}

enum error_code communication_data(int lfd, char *file_path)
{
    enum error_code ret = ERROR_SYSTEM;
    int cfd = -1;

    for (;;) {
        cfd = accept(lfd, NULL, NULL);
        if (cfd == -1) {
            continue;
        }
        DEBUG_MACRO(debug_mode, true, "accept");

        struct client_thread_args *args = malloc(sizeof(struct client_thread_args));
        if (args == NULL) {
            set_error(ERROR_SYSTEM, errno);
            ret = ERROR_SYSTEM;
            goto end;
        }

        args->cfd = cfd;
        args->server_base_path = file_path;
        args->debug_mode_enabled = debug_mode;

        pthread_t tid;
        int s;

        s = pthread_create(&tid, NULL, handle_client, (void *)args);
        if (s != 0) {
            set_error(ERROR_SYSTEM, s);
            ret = ERROR_SYSTEM;
            goto end;
        }
        pthread_detach(tid);
    }

end:
    ret = close_file_descriptor(cfd);
    return ret;
}

int main(int argc, char *argv[])
{
    enum error_code ret = ERROR_SYSTEM;
    char port_num[PORTNUM_MAX_LEN] = {0};
    char file_path[MAX_PATH_LEN] = {0};
    int lfd = -1;

    // nochdir = 1を指定して、daemon()がカレントディレクトリを変更しないようにする
    if (daemon(1, 0) != 0) { 
        ret = ERROR_SYSTEM;
        set_error(ret, errno);
        goto end;
    }

    DEBUG_MACRO(debug_mode, true, "==== daemonized success ====");

    if (parse_option(argc, argv, port_num, file_path)) { // オプション解析
        ret = ERROR_ARGUMENT;
        set_error(ret, errno);
        goto end;
    }
    DEBUG_MACRO(debug_mode, true, "==== parse_option success ====");

    if (*file_path == '\0') { // -sオプションがない場合、filepathにはカレントディレクトリを指定
        getcwd(file_path, sizeof(file_path));
    }

    if ((ret = setup_server(&lfd, port_num))) { // サーバー設定処理
        goto end;
    }
    DEBUG_MACRO(debug_mode, true, "==== setup server success ====");
 
    if ((ret = communication_data(lfd, file_path))) { // データ通信処理
        goto end;
    }
    DEBUG_MACRO(debug_mode, true, "==== communication data success ====");

    ret = NORMAL;
    
end:
    ret = close_file_descriptor(lfd);
    print_error();
    return ret;
}