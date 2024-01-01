#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "log.h"

#define SERVER_IP "121.40.46.148"
#define PORT 9988

#define NETWORK_MTU 1420

struct msg_head {
    unsigned char cmd;
    uint32_t  conv;
    uint32_t  data_len;
    unsigned char data[0];
}__attribute__((packed));

int main(int argc, char **argv)
{
    int client_fd;
    int ret;
    struct sockaddr_in server_addr;
    char message[1024];
    struct msg_head *msg;
    struct msg_head recv_msg;
    int option;
    char file[1024];
    FILE *fp;
    long fileSize;
    void *p;
    int i;

    log_level_string(0);

    while ((option = getopt(argc, argv, "p:")) != -1) {
        switch (option) {
            case 'p':
                snprintf(file, sizeof(file)-1, "%s", optarg);
                break;
            default:
                printf("Invalid option");
                return 1;
        }
    }
    fp = fopen(file, "rb");
    if (!fp) {
        log_error("open file:%s error:%s", file, strerror(errno));
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *fileContent = (char *)malloc(fileSize);
    fread(fileContent, 1, fileSize, fp);
    // 创建socket
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 设置server_addr
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(PORT);

    // 连接服务器
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    log_debug("Connected to server");
    p = strrchr(file, '/');
    if (p) {
        p++;
    }
    else {
        p = file;
    }
    msg = calloc(1, sizeof(*msg)+1500);
    memset(msg, 0, sizeof(msg));
    msg->cmd = 0x80;
    msg->data_len = htonl(strlen(p));
    strcpy(msg->data, p);

    send(client_fd, msg, sizeof(*msg) + strlen(p), 0);
    recv(client_fd, &recv_msg, sizeof(recv_msg), 0);
    
    int pack;
    float progress;
    for (i=0; i<fileSize / NETWORK_MTU; i++) {
        msg->cmd = 0x81;
        msg->conv = recv_msg.conv;
        msg->data_len = htonl(NETWORK_MTU);
        memcpy(msg->data, &fileContent[NETWORK_MTU*i], NETWORK_MTU);
        send(client_fd, msg, sizeof(*msg) + NETWORK_MTU, 0);
        progress = (float)100 *i*NETWORK_MTU/ fileSize;
        printf("file progress:%.2f\r", progress);
        fflush(stdout);
    }
    if (fileSize % NETWORK_MTU != 0) {
        msg->data_len = htonl(fileSize % NETWORK_MTU);
        memcpy(msg->data, &fileContent[fileSize / NETWORK_MTU * NETWORK_MTU], fileSize % NETWORK_MTU);
        send(client_fd, msg, sizeof(*msg) + fileSize % NETWORK_MTU, 0);
    }

    close(client_fd);

    log_debug("file trans over");

    return 0;
}
