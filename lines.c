#include <unistd.h>
#include <errno.h>
#include "lines.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int sendMessage(int socket, char *buffer, int len)
{
    int r;
    int l = len;
    do {
        r = write(socket, buffer, l);
        l = l -r;
        buffer = buffer + r;
    } while ((l>0) && (r>=0));

    if (r < 0)
        return (-1);   /* fail */
    else
        return(0);	/* full length has been sent */
}

int recvMessage(int socket, char *buffer, int len)
{
    int r;
    int l = len;


    do {
        r = read(socket, buffer, l);
        l = l -r ;
        buffer = buffer + r;
    } while ((l>0) && (r>=0));

    if (r < 0)
        return (-1);   /* fallo */
    else
        return(0);	/* full length has been receive */
}

ssize_t readLine(int fd, void *buffer, size_t n)
{
    ssize_t numRead;  /* num of bytes fetched by last read() */
    size_t totRead;	  /* total bytes read so far */
    char *buf;
    char ch;


    if (n <= 0 || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    buf = buffer;
    totRead = 0;

    for (;;) {
        numRead = read(fd, &ch, 1);	/* read a byte */

        if (numRead == -1) {
            if (errno == EINTR)	/* interrupted -> restart read() */
                continue;
            else
                return -1;		/* some other error */
        } else if (numRead == 0) {	/* EOF */
            if (totRead == 0)	/* no byres read; return 0 */
                return 0;
            else
                break;
        } else {			/* numRead must be 1 if we get here*/
            if (ch == '\n')
                break;
            if (ch == '\0')
                break;
            if (totRead < n - 1) {		/* discard > (n-1) bytes */
                totRead++;
                *buf++ = ch;
            }
        }
    }
    *buf = '\0';
    return totRead;
}


int recvInt32(int socket, int *dest) {
    int32_t int_net;
    if (recvMessage(socket, (char *) &int_net, sizeof(int32_t)) == -1) {
        perror("Error recvMessage (recvInt32)");
        return -1;
    }
    *dest = ntohl(int_net);
    return 0;
}

int recvV_value2(int socket, double *V_value2, int N_value2) {
    for (int i = 0; i < N_value2; ++i) {
        double val;
        char buffer[sizeof(double)]; // buffer para almacenar los bytes recibidos
        // Recibir los bytes en el buffer
        if (recvMessage(socket, buffer, sizeof(double)) == -1) {
            perror("Error recvMessage (recvV_value2)");
            return -1;
        }
        // Deserializar el buffer en un double
        memcpy(&val, buffer, sizeof(double));
        // Almacenar el valor deserializado en V_value2
        V_value2[i] = val;
    }
    return 0;
}

int sendInt32(int socket, int num) {
    int32_t num_net = htonl(num);
    if (sendMessage(socket, (char *) &num_net, sizeof(int32_t)) == -1) {
        perror("Error sendMessage (sendInt32)");
        return -1;
    }
    return 0;
}

int sendV_value2(int socket, double* V_value2, int N_value2) {
    for (int i = 0; i < N_value2; ++i) {
        double val = V_value2[i];
        char buffer[sizeof(double)]; // Buffer para almacenar la representación de bytes del double
        // Serializar el double en el buffer
        memcpy(buffer, &val, sizeof(double));
        // Enviar el buffer al servidor
        if (sendMessage(socket, buffer, sizeof(double)) == -1) {
            perror("Error sendMessage (sendV_value2)");
            return -1;
        }
    }
    return 0;
}