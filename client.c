#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <limits.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <signal.h>
#include <errno.h>

#define BUFF_LEN 1024
#define MAX_SIZE 1024000 /* 1000k */
#define HEAD_LEN 8

static char *shared_clip = "";
static int shared_size = 0;
static int client_sock;
static int received;


static int running;

void sigint_handler(int sig)
{
    if (sig == SIGINT)
    {
        // shutdown(client_sock, SHUT_RDWR);
        close(client_sock);
        printf("shutdown %d", client_sock);
        running = 0;
    }
}


// paste
static Display *display2;
static Window window2;
static Atom UTF82, XA_STRING2 = 31;

struct clip_data
{
    size_t size;
    char *data;   
};

struct clip_data XPasteType(Atom atom)
{
    struct clip_data result;
    XEvent event;
    int format;
    unsigned long N, size;
    char *data, *s = 0;
    Atom target,
        CLIPBOARD = XInternAtom(display2, "CLIPBOARD", 0),
        XSEL_DATA = XInternAtom(display2, "XSEL_DATA", 0);

    XConvertSelection(display2, CLIPBOARD, atom, XSEL_DATA, window2, CurrentTime);
    XSync(display2, 0);
    XNextEvent(display2, &event);

    switch (event.type)
    {
    case SelectionNotify:
        if (event.xselection.selection != CLIPBOARD)
            break;
        if (event.xselection.property)
        {
            XGetWindowProperty(event.xselection.display, event.xselection.requestor,
                               event.xselection.property, 0L, (~0L), 0, AnyPropertyType, &target,
                               &format, &size, &N, (unsigned char **)&data);
            if (target == UTF82 || target == XA_STRING2)
            {
                s = strndup(data, size);
                XFree(data);
            }else{
                s = malloc(size);
                memset(s,0,size);
                memcpy(s,data, size);
                XFree(data);
            }
            XDeleteProperty(event.xselection.display, event.xselection.requestor, event.xselection.property);
        }
    }
    result.data = s;
    result.size = size;
    return result;
}


// paste end


// copy
static Display *display;
static Window window;
static Atom targets_atom, text_atom, UTF8, XA_ATOM = 4, XA_STRING = 31, PNG;
static Atom selection;

static void XCopy(Atom selection, unsigned char *text, int size)
{
    XEvent event;
    Window owner;
    XSetSelectionOwner(display, selection, window, 0);
    if (XGetSelectionOwner(display, selection) != window)
        return;
    while (1)
    {
        XNextEvent(display, &event);
        switch (event.type)
        {
        case SelectionRequest:
            if (event.xselectionrequest.selection != selection)
                break;
            XSelectionRequestEvent *xsr = &event.xselectionrequest;
            XSelectionEvent ev = {0};
            int R = 0;
            ev.type = SelectionNotify, ev.display = xsr->display, ev.requestor = xsr->requestor,
            ev.selection = xsr->selection, ev.time = xsr->time, ev.target = xsr->target, ev.property = xsr->property;
            if (ev.target == targets_atom){
                Atom possible_targets[3] = {UTF8,XA_STRING, PNG};
                R = XChangeProperty(ev.display, ev.requestor, ev.property, XA_ATOM, 32,
                                    PropModeReplace, (unsigned char *)possible_targets, 3);
            }
            else if (ev.target == XA_STRING || ev.target == text_atom)
                R = XChangeProperty(ev.display, ev.requestor, ev.property, XA_STRING, 8, PropModeReplace, shared_clip, strlen(shared_clip));
            else if (ev.target == UTF8)
                R = XChangeProperty(ev.display, ev.requestor, ev.property, UTF8, 8, PropModeReplace, shared_clip, strlen(shared_clip));
            else if (ev.target == PNG)
                R = XChangeProperty(ev.display, ev.requestor, ev.property, ev.target, 8, PropModeReplace, shared_clip, shared_size);
            else
                ev.property = None;
            if ((R & 2) == 0)
                XSendEvent(display, ev.requestor, 0, 0, (XEvent *)&ev);
            break;
        case SelectionClear:
            if (shared_clip != NULL && *shared_clip != 0)
            {
                free(shared_clip);    
            }
            shared_clip = "";
            shared_size = 0;
            return;
        }
    }
}

void *XCopyDaemon(){
    while (1)
    {
        if (received)
        {
            received = 0;
            XCopy(selection, NULL, 0);
        }
        sleep(1);
    }
}

// copy end

void *listen_remote(void *argv){
   while (1)
   {
       unsigned char head[HEAD_LEN];
       memset(head, 0, HEAD_LEN);
       int head_rec_count;
       int rest = HEAD_LEN;
       while (rest > 0 && (head_rec_count = read(client_sock, head, rest)) > 0)
       {
           rest -= head_rec_count;
       }
       if (head_rec_count<0)
       {
           printf("errno:%d, error:%s", errno, strerror(errno));
           break;
       }
       
       unsigned long data_size = 0;
       for (size_t i = 0; i < HEAD_LEN; i++)
       {
           data_size = data_size << 8 | (unsigned long) head[i];
       }

       shared_clip = malloc(data_size);
       memset(shared_clip, 0, data_size);
       int offset = 0;
       while (1)
       {
           char buff[BUFF_LEN];
           int rec_count = read(client_sock, buff, sizeof(buff));
           if (rec_count < 0)
           {
               printf("errno:%d, error:%s", errno, strerror(errno));
               break;
           }
           if (rec_count == 0){
               printf("socket closed\n");
               break;
           }
           memcpy(shared_clip+offset, buff, rec_count);
           offset += rec_count;
           if (offset >= data_size - 1)
           {
               printf("trans over\n");
               break;
           }
       }
       
       shared_size = offset;
       printf("%s", shared_clip);
       received = 1;
   }
}

void *listen_local_clip(void *argv){
    while (1)
    {
        struct clip_data local_clip = XPasteType(XA_STRING2);
        if (local_clip.data == NULL)
        {
            sleep(1);
            continue;
        }
        
        printf("%s", local_clip.data);
        char send_buf[local_clip.size+8];
        char head[HEAD_LEN];
        memset(head, 0, 8);
        head[4] = (char)(local_clip.size >> 24 & 0xff);
        head[5] = (char)(local_clip.size >> 16 & 0xff);
        head[6] = (char)(local_clip.size >> 8 & 0xff);
        head[7] = (char)(local_clip.size >>0&0xff);
        memcpy(send_buf, head,8);
        memcpy(send_buf + 8, local_clip.data, local_clip.size);
        if (shared_clip != NULL && memcmp(shared_clip, local_clip.data, local_clip.size) == 0)
        {
            sleep(1);   
            continue;
        }
        if (shared_clip != NULL && *shared_clip != 0)
        {
            free(shared_clip);
        }
        shared_clip = local_clip.data;
        int cnt = write(client_sock, send_buf, local_clip.size+8);
        if (cnt < 0)
        {    
            printf("errno:%d, error:%s", errno, strerror(errno));
        }
        
    }
}

int main(int argc, char const *argv[])
{
    running = 1;
    setbuf(stdout, NULL);

    const char *host = "127.0.0.1";
    int port = 8099;
    if (argc == 2)
    {
        host = argv[1];
    }
    if (argc == 3)
    {
        host = argv[1];
        const char *port_s = argv[2];
        port = atoi(port_s);
    }

    display = XOpenDisplay(0);
    int N = DefaultScreen(display);
    window = XCreateSimpleWindow(display, RootWindow(display, N), 0, 0, 1, 1, 0,BlackPixel(display, N), WhitePixel(display, N));
    targets_atom = XInternAtom(display, "TARGETS", 0);
    text_atom = XInternAtom(display, "TEXT", 0);
    UTF8 = XInternAtom(display, "UTF8_STRING", 1);
    PNG = XInternAtom(display, "image/png", 0);
    selection = XInternAtom(display, "CLIPBOARD", 0);

    display2 = XOpenDisplay(0);
    int N2 = DefaultScreen(display2);
    window2 = XCreateSimpleWindow(display2, RootWindow(display2, N2), 0, 0, 1, 1, 0, BlackPixel(display, N2), WhitePixel(display2, N2));
    UTF82 = XInternAtom(display, "UTF8_STRING", 1);

    client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_addr.s_addr = inet_addr(host);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if(connect(client_sock, (struct sockaddr *)&server_addr, sizeof(server_addr))<0){
        printf("errno:%d, error:%s", errno, strerror(errno));
    }

    signal(SIGINT, sigint_handler);
    
    // 多线程
    // 线程1 监听sock, 写入公共变量
    // 线程2 监听剪切板, 发送到远程

    pthread_t copyThread;
    pthread_create(&copyThread, NULL, XCopyDaemon, NULL);

    pthread_t lthread;
    pthread_create(&lthread, NULL, listen_remote, NULL);

    pthread_t cthread;
    pthread_create(&cthread, NULL, listen_local_clip, NULL);

    while (1){
        sleep(10);
        if (!running) break;
    }
    
    return 0;
}

