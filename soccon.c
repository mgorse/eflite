#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int sockconnect(char *fname)
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
