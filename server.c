#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/prctl.h>
#include <pthread.h>
#include <errno.h>

#define MAX_REV_LEN 1000

static int server_sock;
static int running;
static struct snode *root;

struct snode
{
    int sock;
    struct snode *pre;
    struct snode *next;
};

void sigint_handler(int sig)
{
    if (sig == SIGINT)
    {
        running = 0;
        struct snode *last = root->next;
        while (last != NULL)
        {
            close(last->sock);
            last = last->next;
            free(last);
        }
        close(server_sock);
        printf("shutdown");
    }
}

int sadd(struct snode *root, struct snode *target){
    if (root->next == NULL)
    {
        root->next = target;
        target->pre = root;
    }else
    {
        sadd(root->next, target);
    }
    return 0;
}

int sremove(struct snode *target){
    target->pre->next = target->next;
    if (target->next != NULL)
    {
        target->next->pre = target->pre;
    }
    return 0;
}


void *handle_connection(void *argv){
    struct snode *node = (struct snode *) argv;
    int client_sock = node->sock;
    while (1)
    {
        char buff[MAX_REV_LEN];
        int rec_count = read(client_sock, buff, sizeof(buff));
        if (rec_count <= 0)
        {
            break;
        }

        printf("%d\n", (int)strnlen(buff+8, MAX_REV_LEN));
        printf("%s\n", buff+8);

        // 广播
        struct snode *last = root->next;
        while (last != NULL)
        {
            if (last != node)
            {
                int wroted = write(last->sock, buff, rec_count);
                if (wroted < 0)
                {
                    printf("errno:%d, error:%s", errno, strerror(errno));
                    struct snode *tmp = last->next;
                    sremove(last);
                    last = tmp;
                    continue;
                }
                
                printf("broadcast to %d\n", last->sock);
            }
            last = last->next;
        }
    }
    close(node->sock);
    sremove(node);
    printf("closed: %d", node->sock);
    free(node);
}

int main(int argc, char const *argv[])
{

    running = 1;
    setbuf(stdout, NULL);

    int port  = 8099;
    if (argc > 1)
    {
        const char *port_s = argv[1];
        printf("%s", port_s);
        port = atoi(port_s);    
    }
    printf("port: %d", port);

    server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("0.0.0.0");
    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_sock, 100);

    signal(SIGINT, sigint_handler);

    // 分配在常量区?
    struct snode node;
    memset(&node, 0, sizeof(node));
    root = &node;

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_size = (socklen_t)sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_size);
        if (client_sock < 0)
        {
            // server 关闭时,这里报错,需要退出, 否则一直返回-1,死循环
            break;
        }

        printf("connected: %d", client_sock);

        // 这里要自己分配内存, 分配到堆上,  如果只声明, 则所有的都会是同一个地址
        struct snode *node = (struct snode *)malloc(sizeof(struct snode));
        memset(node, 0, sizeof(node));
        node->sock = client_sock;

        sadd(root, node);

        pthread_t t;
        pthread_create(&t, NULL, handle_connection, node);
        
    }

    return 0;
}

