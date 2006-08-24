/* soccon.c - function to open a connection to a socket
 * $Id: soccon.c,v 1.2 2002/03/06 02:25:18 mgorse Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <unistd.h>

int sockconnect_unix(const char *fname)
{
  struct sockaddr_un addr;
  int sock;

  if (!fname) return -1;
  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock == -1)
  {
    perror("socket");
    return -1;
  }
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, fname, sizeof(addr.sun_path));
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
  {
    close(sock);
    return -1;
  }
  return sock;
}

static int sockconnect_tcp(const char *port)
{
  char host[1024];
  int portnum;
  int fd;
  struct sockaddr_in servaddr;
  char *p;
  int len;

  p = strstr(port, ":");
  portnum = atoi(p + 1);
  len = p - port;
  if (len > 1023)
  {
    fprintf(stderr, "syntheport long\n");
    exit(1);
  }
  memcpy(host, port, len);
  host[len] = '\0';
  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    perror("socket");
    exit(1);
  }
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(portnum);

#ifdef HAVE_INET_PTON
  if (inet_pton(AF_INET, host, &servaddr.sin_addr) <= 0)
  {
    perror("inet_pton");
    exit(1);
  }
#else
  servaddr.sin_addr.s_addr = inet_addr(host);
#endif

  if (connect(fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
  {
    return -1;
  }
  return fd;
}

int sockconnect(const char *name)
{
  if (!name) return -1;
  if (strchr(name, ':')) return sockconnect_tcp(name);
  else return sockconnect_unix(name);
}
