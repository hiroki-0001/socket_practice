#ifndef SOCKET_MSG_H
#define SOCKET_MSG_H

#define FILENAME_MAX_LEN 200 // ファイル名の最大長
#define MAX_PATH_LEN 1024
#define BUFFER_SIZE 1024   	 // ファイル転送に使用するバッファサイズ

#pragma pack(push, 1) 

struct f_message
{
    char message_type;
    unsigned long long file_size;
    char file_name[FILENAME_MAX_LEN];
};

struct a_message
{
    char message_type;
};

struct e_message
{
    char message_type;
    char error_message[BUFFER_SIZE];
};

#pragma pack(pop) 

enum error_code send_f_msg(int socket, unsigned long long file_size, char *file_name);

enum error_code receive_f_msg(int socket, struct f_message *f_msg);

enum error_code send_a_msg(int socket);

enum error_code receive_a_msg(int socket, struct a_message *a_msg);

enum error_code send_e_msg(int socket, char *e_msg);

enum error_code receive_e_msg(int socket, struct e_message *e_msg);

char check_msg_type(int socket, char *msg_type);

enum error_code send_first_msg(int socket, char msg_type);

#endif // SOCKET_MSG_H