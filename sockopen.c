/* sockopen.c -- Function to open a socket
 * $id$
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>

int sockopen(const char *fname)
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
