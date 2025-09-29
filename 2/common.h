#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>

#define PORTNUM_MAX_LEN 6    // ポート番号の最大長（ポート番号の最大値は 65535）
#define MAX_HEADER_LEN 90
#define MAX_DEBUG_MSG_LEN 512

enum error_code send_reset_packet(int cfd);

ssize_t recvn(int fd, void *buffer, size_t n, int flag);

ssize_t sendn(int fd, const void *buffer, size_t n);

void get_time();

# define DEBUG_MACRO(debug_mode, is_server, format, ...)\
    if (debug_mode) { \
        char time_stamp[50]; \
        get_time(time_stamp, sizeof(time_stamp)); \
        char raw_header[150]; \
        snprintf(raw_header, sizeof(raw_header), "%s %lu %s:%d", time_stamp, (unsigned long)getpid(), __FILE__, __LINE__); \
        char formatted_header[MAX_HEADER_LEN + 1]; /* 90バイト + NULL終端 */  \
        snprintf(formatted_header, sizeof(formatted_header), "%-90.90s", raw_header); \
        char message_only_buffer[MAX_DEBUG_MSG_LEN - MAX_HEADER_LEN + 1]; /* ヘッダを除いたメッセージ部分の最大長 + NULL終端 */ \
        snprintf(message_only_buffer, sizeof(message_only_buffer), format, ##__VA_ARGS__); \
        char final_log_message[MAX_DEBUG_MSG_LEN + 1]; /* 全体で512バイト + NULL終端 */ \
        snprintf(final_log_message, sizeof(final_log_message), "%s%s", formatted_header, message_only_buffer); \
        if (is_server) { \
                char file_name[FILENAME_MAX_LEN]; \
                snprintf(file_name, sizeof(file_name), "%s%lu", "trans-data-server.", (unsigned long)getpid()); \
                FILE *log_file = fopen(file_name, "a"); \
                if (log_file) { \
                    fprintf(log_file, "%s\n", final_log_message); \
                    fclose(log_file); \
                } else { \
                    fprintf(stderr, "Error: Could not open log file 'trans-data-server.pid'\n"); \
                } \
            } else { \
                fprintf(stderr, "%s\n", final_log_message); \
            } \
        } \

#endif // COMMON_H