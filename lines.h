#include <unistd.h>
#include <arpa/inet.h>

int sendMessage(int socket, char *buffer, int len);
int recvMessage(int socket, char *buffer, int len);
ssize_t readLine(int fd, void *buffer, size_t n);
int recvInt32(int socket, int *dest);
int recvV_value2(int socket, double *V_value2, int N_value2);
int sendInt32(int socket, int num);
int sendV_value2(int socket, double* V_value2, int N_value2);