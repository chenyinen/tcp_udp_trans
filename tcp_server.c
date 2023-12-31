#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include "list.h"

#define MAX_EVENTS 10
#define PORT 9988

struct client_info {
    int client_fd;   
    int conv;
    char line_buf[1500];
    FILE *fp;
    struct sockaddr_in  client_addr;
    struct list_head list;
};
struct msg_head {
    unsigned char cmd;
    uint32_t  conv;
    uint32_t  data_len;
    unsigned char data[0];
}__attribute__((packed));

struct module_mannage {
    int server_fd;
    int epoll_fd;
    struct list_head client_list;
};

struct module_mannage server_module;

int server_init()
{
    struct sockaddr_in server_addr;
    struct epoll_event event;

    // 创建socket
    server_module.server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_module.server_fd < 0) {
        return -1;
    }
    // 设置server_addr
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("0.0.0.0");;
    server_addr.sin_port = htons(PORT);

    // 绑定地址
    if (bind(server_module.server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("socket bind fail\n");
        close(server_module.server_fd);
        return -1;
    }
    // 监听
    if (listen(server_module.server_fd, 5) < 0) {
        printf("listen fail\n");
        close(server_module.server_fd);
        return -1;
    }
     // 创建epoll
    server_module.epoll_fd = epoll_create1(0);
    if (server_module.epoll_fd  < 0) {
        printf("epoll create fail\n");
        close(server_module.server_fd);
        return -1;
    }
    // 将server_fd加入epoll监听
    event.events = EPOLLIN;
    event.data.fd = server_module.server_fd;
    if (epoll_ctl(server_module.epoll_fd, EPOLL_CTL_ADD, server_module.server_fd, &event) == -1) {
        close(server_module.server_fd);
        close(server_module.epoll_fd);
        return -1;
    }
    INIT_LIST_HEAD(&server_module.client_list);
    return 0;
}
void recv_handle(struct client_info *client)
{
    int recv_bytes;
    int recv_fd;
    int i = 0;
    struct msg_head msg;
    uint32_t  data_len;
    char buf[2048];
    int recv_total;

    recv_fd = client->client_fd;
    recv_bytes = recv(recv_fd, &msg, sizeof(msg), MSG_DONTWAIT);
    if (recv_bytes == 0) { //关闭链接
        epoll_ctl(server_module.epoll_fd, EPOLL_CTL_DEL, recv_fd, NULL);
        close(recv_fd);
        list_del(&client->list);
        if (client->fp) {
            printf("file save\n");
            fflush(client->fp);
            fsync(client->client_fd);
            fclose(client->fp);
        }
        free(client);
        return ;
    }
    if (recv_bytes != sizeof(msg)) {
        printf("recv bytes number error\n");
        return ;
    }
    data_len = ntohl(msg.data_len);
    if (data_len >= sizeof(client->line_buf)) {
        printf("recv data too long:%d\n", data_len);
        return ;
    }
    switch(msg.cmd) {
        case 0x80: { //准备传输文件
            recv_bytes = recv(recv_fd, client->line_buf, data_len, MSG_DONTWAIT);
            client->line_buf[recv_bytes] = '\0';
            snprintf(buf, sizeof(buf) - 1, "%s", client->line_buf);
            client->fp = fopen(buf, "wb");
            if (!client->fp) {
                printf("fopen %s fail:%s\n", client->line_buf, strerror(errno));
                return ;
            }
            printf("begin save file:%s\n", buf);
            memset(&msg, 0, sizeof(msg));
            msg.cmd = 0x80;
            msg.conv = htonl(client->conv);
            send(recv_fd, &msg, sizeof(msg), MSG_DONTWAIT);
            break;
        }
        case 0x81: {
            recv_total = 0;
            while(recv_total < data_len) {
                recv_bytes = recv(recv_fd, client->line_buf + recv_total, data_len - recv_total, 0);
                recv_total += recv_bytes;
            }
            fwrite(client->line_buf, recv_total, 1, client->fp);
            break;
        }
        default: {
            break;
        }
    }
    return ;
}
int create_conv()
{
    static int conv = 0;
    return ++conv;
}
int main()
{
    int ret;
    int recv_bytes;
    int  client_fd, nfds, n;
    struct sockaddr_in  client_addr;
    struct epoll_event event, events[MAX_EVENTS];
    char buffer[1024];
    struct client_info *client_node, *tmp;
    socklen_t client_addr_len;

    ret = server_init();
    if (0 != ret) {
        printf("server_init fail:%d\n", ret);
        return -1;
    }

    printf("Server is listening on port %d\n", PORT);

    while (1) {
        nfds = epoll_wait(server_module.epoll_fd, events, sizeof(events)/sizeof(events[0]), 500);
        if (nfds < 0) {
            continue;
        }
        for (n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_module.server_fd) {
                client_addr_len = sizeof(client_addr);
                client_fd = accept(server_module.server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
                if (client_fd < 0) {
                    printf("accept fail:%s\n", strerror(errno));
                    continue;
                }
                char ip_address[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &(client_addr.sin_addr), ip_address, INET_ADDRSTRLEN) != NULL) {
                    printf("Client connected: %s:%d\n", ip_address, ntohs(client_addr.sin_port));
                }
                event.events = EPOLLIN;
                event.data.fd = client_fd;
                if (epoll_ctl(server_module.epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
                    printf("epoll add fail\n");
                    close(client_fd);
                    continue;
                }
                client_node = calloc(1, sizeof(*client_node));
                client_node->client_fd = client_fd;
                client_node->conv = create_conv();
                memcpy(&client_node->client_addr, &client_addr, sizeof(client_addr));
                list_add(&client_node->list, &server_module.client_list);
                printf("Accepted a new connection\n");
            } else {
                list_for_each_entry_safe(client_node, tmp, &server_module.client_list, list) {
                    if (client_node->client_fd == events[n].data.fd) {
                        recv_handle(client_node);
                    }
                }
            }
        }
    }

    close(server_module.server_fd);
    close(server_module.epoll_fd);
    return 0;
}
