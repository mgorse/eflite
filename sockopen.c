/* sockopen.c -- Function to open a socket
 * $id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>

int sockopen_unix(const char *fname)
{
  int sock;
  struct sockaddr_un addr;

  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock == -1)
  {
    return -1;
  }
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, fname, sizeof(addr.sun_path));
  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
  {
    close(sock);
    return -1;
  }
  if (listen(sock, 512) == -1)
  {
    close(sock);
    return -1;
  }
  return sock;
}

int sockopen_tcp(const char *name)
{
  int port;
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in serv_addr;
  const char *p;

  p = strchr(name, ':');
  if (!p)
  {
    fprintf(stderr, "Argh! Internal error!\n");
    exit(1);
  }
  port = atoi(p + 1);
  memset(&serv_addr, 0, sizeof(struct sockaddr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);
  if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    fprintf(stderr, "Error in binding to port %d.\n", port);
    exit(1);
  }
  if (listen(sock, 5) == -1)
  {
    perror("listen");
    exit(1);
  }
  return sock;
}
int sockopen(const char *name)
{
  if (!name) return -1;
  if (strchr(name, ':')) return sockopen_tcp(name);
  else return sockopen_unix(name);
}
