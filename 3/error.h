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
        ERROR_SYSTEM,
        ERROR_DIFF_FILESIZE,
        ERROR_TIMEOUT,
        ERROR_BUFFER_OVERFLOW,
        ERROR_LOCK_EXISTS, // ロックファイルが既に存在する場合のエラーコード
        ERROR_LOCK_CREATE, // ロックファイル作成失敗のエラーコード
        ERROR_LOCK_REMOVE  // ロックファイル削除失敗のエラーコード
};

void set_error(enum error_code ecode, int s_error);

void print_error(void);

#endif