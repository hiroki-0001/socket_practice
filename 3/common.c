#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <string.h>

#include "common.h"
#include "error.h"

enum error_code send_reset_packet(int cfd)
{
    enum error_code ret = ERROR_SYSTEM;
    struct linger ling;

    memset(&ling, 0, sizeof(ling));
    ling.l_onoff = 1;
    ling.l_linger = 0;

    if (setsockopt(cfd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling))) {
        ret = ERROR_SYSTEM;
        set_error(ERROR_SYSTEM, errno);
        goto end;
    }
    ret = NORMAL;
end:
    return ret;
}


ssize_t recvn(int fd, void *buffer, size_t n, int flag)
{
    ssize_t num_recv;
    size_t total_recv_data;
    char *buf;

    buf = buffer;
    for (total_recv_data = 0; total_recv_data < n;) {
        num_recv = recv(fd, buf, n - total_recv_data, flag);

        if (num_recv == 0) {
            return total_recv_data;
        }
        if (num_recv == -1) {
            if (errno == EINTR) { //シグナルによる割り込みは再試行
                continue;
            } else if (errno == EWOULDBLOCK || errno == EAGAIN) { // タイムアウト
                return -2;
            } else { // その他　システムエラー
                return -1;
            }
        }
        total_recv_data += num_recv;
        buf += num_recv;
    }
    return total_recv_data;
}

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

inline void get_time(char *buffer, int buf_size)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *local = localtime(&tv.tv_sec);
    char time_stamp_buf[30];
    strftime(time_stamp_buf, sizeof(time_stamp_buf), "%Y/%m/%d %H:%M:%S", local);

    char buf[50];
    snprintf(buf, sizeof(buf), "%s.%03ld", time_stamp_buf, tv.tv_usec / 1000);

    strncpy(buffer, buf, buf_size - 1);
    buffer[buf_size - 1] = '\0';

}

