#ifndef COMMON_H
#define COMMON_H

#define FILENAME_MAX_LEN 256 // ファイル名の最大長
#define PORTNUM_MAX_LEN 6    // ポート番号の最大長（ポート番号の最大値は 65535）
#define BUFFER_SIZE 1024   	 // ファイル転送に使用するバッファサイズ

ssize_t sendn(int fd, const void *buffer, size_t n) 
{
    ssize_t num_send_data;
    size_t total_send_data;
    const char *buf;
    buf = buffer;

    for (total_send_data = 0; total_send_data < n;) {
        num_send_data = send(fd, buf, n - total_send_data, MSG_NOSIGNAL);

        if (num_send_data <= 0) {
            if (num_send_data == -1 && errno == EINTR) { // EINTR = システムコールがシグナルによって中断された際に返されるerrno
                continue;
            } else {
                return -1;
            }
        }
        total_send_data += num_send_data;
        buf += num_send_data;
    }
    return total_send_data;
}

#endif // COMMON_H