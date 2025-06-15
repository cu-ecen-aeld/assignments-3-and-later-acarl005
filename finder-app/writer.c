#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    syslog(LOG_ERR, "invalid args");
    return 1;
  }
  char *writefile = argv[1];
  char *writestr = argv[2];

  syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

  int fd = open(writefile, O_WRONLY | O_CREAT, 0644);
  if (fd == -1) {
    syslog(LOG_ERR, "Error opening file %s: %s", writefile, strerror(errno));
    return 1;
  }

  int written_len = write(fd, writestr, strlen(writestr));
  if (written_len == -1) {
    syslog(LOG_ERR, "Error writing to file %s: %s", writefile, strerror(errno));
    return 1;
  }

  syslog(LOG_DEBUG, "%i bytes written", written_len);

  return 0;
}
