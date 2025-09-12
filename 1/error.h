#ifndef _ERROR_H_
#define _ERROR_H_

enum error_code {
        NORMAL,
        ERROR_ARGUMENT,
        ERROR_FILE_OPEN,
        ERROR_SOCKET,
        ERROR_BIND,
        ERROR_CONNECT,
        ERROR_SEND,
        ERROR_RECEIVED,
        ERROR_LISTEN,
        ERROR_ACCEPT,
        ERROR_SYSTEM
};

void set_error(enum error_code ecode, int s_error);

void print_error(void);

#endif