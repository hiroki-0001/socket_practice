#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include "socket_msg.h"
#include "error.h"
#include "common.h"

/* f message */

enum error_code send_f_msg(int socket, unsigned long long file_size, char *file_name)
{
    enum error_code ret = ERROR_SYSTEM;
    struct f_message f_msg;

    f_msg.message_type = 'F';
    f_msg.file_size = file_size;
    strncpy(f_msg.file_name, file_name, sizeof(f_msg.file_name) - 1); // '\0'終端になるように
    f_msg.file_name[sizeof(f_msg.file_name) - 1] = '\0';

    if (sendn(socket, &f_msg, sizeof(struct f_message)) == -1 ) {
        ret = ERROR_SEND;
        set_error(ERROR_SEND, errno);
        goto end;
    }
    ret = NORMAL;

end:
    return ret;
}

enum error_code receive_f_msg(int socket, struct f_message *f_msg)
{
    enum error_code ret = ERROR_SYSTEM;
    ssize_t recv_bytes;

    recv_bytes = recv(socket, f_msg, sizeof(struct f_message), MSG_WAITALL);

    if (recv_bytes < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            send_reset_packet(socket);
            set_error(ERROR_TIMEOUT, errno);
            ret = ERROR_TIMEOUT;
            goto end;
        } else {
            set_error(ERROR_RECEIVED, errno);
            ret = ERROR_RECEIVED;
            goto end;
        }
    }
    ret = NORMAL;

end:
    return ret;
}

/* a message */

enum error_code send_a_msg(int socket)
{
    enum error_code ret = ERROR_SYSTEM;
    struct a_message a_msg;
    a_msg.message_type = 'A';

    if (sendn(socket, &a_msg, sizeof(struct a_message)) == -1 ) {
        ret = ERROR_SEND;
        set_error(ERROR_SEND, errno);
        goto end;
    }
    ret = NORMAL;

end:
    return ret;
}

enum error_code receive_a_msg(int socket, struct a_message *a_msg)
{
    enum error_code ret = ERROR_SYSTEM;
    ssize_t recv_bytes;

    recv_bytes = recv(socket, a_msg, sizeof(struct a_message), MSG_WAITALL);

    if (recv_bytes < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            send_reset_packet(socket);
            set_error(ERROR_TIMEOUT, errno);
            ret = ERROR_TIMEOUT;
            goto end;
        } else {
            set_error(ERROR_RECEIVED, errno);
            ret = ERROR_RECEIVED;
            goto end;
        }
    }
    ret = NORMAL;

end:
    return ret;
}

/* e message */

enum error_code send_e_msg(int socket, char *msg)
{
    enum error_code ret = ERROR_SYSTEM;
    struct e_message e_msg;
    e_msg.message_type = 'E';

    strncpy(e_msg.error_message, msg, sizeof(e_msg.error_message) - 1); // '\0'終端になるように
    e_msg.error_message[sizeof(e_msg.error_message) - 1] = '\0';

    if (sendn(socket, &e_msg, sizeof(struct e_message)) == -1 ) {
        ret = ERROR_SEND;
        set_error(ERROR_SEND, errno);
        goto end;
    }
    ret = NORMAL;

end:
    return ret;
}

enum error_code receive_e_msg(int socket, struct e_message *e_msg)
{
    enum error_code ret = ERROR_SYSTEM;
    ssize_t recv_bytes;

    recv_bytes = recv(socket, e_msg, sizeof(struct e_message), MSG_WAITALL);

    if (recv_bytes < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            send_reset_packet(socket);
            set_error(ERROR_TIMEOUT, errno);
            ret = ERROR_TIMEOUT;
            goto end;
        } else {
            set_error(ERROR_RECEIVED, errno);
            ret = ERROR_RECEIVED;
            goto end;
        }
    }
    ret = NORMAL;

end:
    return ret;
}

/**/

char check_msg_type(int socket, char *msg_type)
{
    ssize_t recv_bytes;
    char buf;

    recv_bytes = recv(socket, &buf, sizeof(char), MSG_WAITALL);

    if (recv_bytes < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            send_reset_packet(socket);
            set_error(ERROR_TIMEOUT, errno);
            return -1; //タイムアウト
        } else {
            set_error(ERROR_RECEIVED, errno);
            return -2; //受信エラー
        }
    }
    *msg_type = buf;
    return buf;
}

enum error_code send_first_msg(int socket, char send_msg_type)
{
    enum error_code ret = ERROR_SYSTEM;

    if (sendn(socket, &send_msg_type, sizeof(char)) == -1) {
        ret = ERROR_SEND;
        set_error(ERROR_SEND, errno);
        goto end;
    }
    ret = NORMAL;

end:
    return ret;
}