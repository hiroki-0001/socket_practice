#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "error.h"

static struct {
        enum error_code num;
        int s_errno;
} error = { 0 };

void set_error(enum error_code ecode, int s_errno)
{
        if (error.num == NORMAL) {
                error.num = ecode;
                error.s_errno = s_errno;
        }
}

void print_error(void)
{
        switch (error.num) {
        case NORMAL:
                break;
        case ERROR_ARGUMENT:
                fprintf(stderr, "argument error. \n");
                break;
        case ERROR_FILE_OPEN:
                fprintf(stderr, "file open error. %s\n", strerror(error.s_errno));
                break;
        case ERROR_SOCKET:
                fprintf(stderr, "socket() failed. %s\n", strerror(error.s_errno));
                break;
        case ERROR_BIND:
                fprintf(stderr, "bind() failed. %s\n", strerror(error.s_errno));
                break;
        case ERROR_CONNECT:
                fprintf(stderr, "connect() failed. %s\n", strerror(error.s_errno));
                break;
        case ERROR_SEND:
                fprintf(stderr, "send() failed. %s\n", strerror(error.s_errno));
                break;
        case ERROR_RECEIVED:
                fprintf(stderr, "recv() failed. %s\n", strerror(error.s_errno));
                break;
        case ERROR_LISTEN:
                fprintf(stderr, "listen() failed. %s\n", strerror(error.s_errno));
                break;
        case ERROR_ACCEPT:
                fprintf(stderr, "accept() failed. %s\n", strerror(error.s_errno));
                break;
        case ERROR_SYSTEM:
                fprintf(stderr, "system error. %s\n", strerror(error.s_errno));
                break;
        case ERROR_DIFF_FILESIZE:
                fprintf(stderr, "file size diffrent error. %s\n", strerror(error.s_errno));
                break;
        case ERROR_TIMEOUT:
                fprintf(stderr, "timeout error. %s\n", strerror(error.s_errno));
                break;
        case ERROR_BUFFER_OVERFLOW:
                fprintf(stderr, " buffer overflow error. \n");
                break;
        case ERROR_LOCK_EXISTS:
                fprintf(stderr, " exist lock file. %s\n", strerror(error.s_errno));
                break;
        case ERROR_LOCK_CREATE:
                fprintf(stderr, " create lock file failed. %s\n", strerror(error.s_errno));
                break;
        case ERROR_LOCK_REMOVE:
                fprintf(stderr, " delete lock file failed. %s\n", strerror(error.s_errno));
                break;

        default:
                break;
        }
}