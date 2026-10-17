#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
int launcher_test_socket(int,int,int);
int launcher_test_connect(int,const struct sockaddr *,socklen_t);
ssize_t launcher_test_send(int,const void *,size_t,int);
ssize_t launcher_test_read(int,void *,size_t);
int launcher_test_poll(struct pollfd *,nfds_t,int);
int launcher_test_getsockopt(int,int,int,void *,socklen_t *);
int launcher_test_fcntl(int,int,...);
#define socket launcher_test_socket
#define connect launcher_test_connect
#define send launcher_test_send
#define read launcher_test_read
#define poll launcher_test_poll
#define getsockopt launcher_test_getsockopt
#define fcntl launcher_test_fcntl

int launcher_test_kill(pid_t,int);
unsigned int launcher_test_sleep(unsigned int);
#define kill launcher_test_kill
#define sleep launcher_test_sleep
