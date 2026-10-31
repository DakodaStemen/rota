/* Creates a binder device through /dev/binderfs/binder-control (BINDER_CTL_ADD) and prints "<name> <major>:<minor>". */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
struct binderfs_device { char name[256]; unsigned int major; unsigned int minor; };
#define BINDER_CTL_ADD _IOWR('b', 1, struct binderfs_device)
int main(int argc, char **argv) {
	const char *ctl = argc > 1 ? argv[1] : "/dev/binderfs/binder-control";
	const char *name = argc > 2 ? argv[2] : "rotate-test-binder";
	struct binderfs_device d; memset(&d, 0, sizeof d);
	strncpy(d.name, name, sizeof d.name - 1);
	int fd = open(ctl, O_RDONLY | O_CLOEXEC);
	if (fd < 0) { perror("open binder-control"); return 1; }
	if (ioctl(fd, BINDER_CTL_ADD, &d) < 0) { perror("BINDER_CTL_ADD"); return 1; }
	printf("%s %u:%u\n", d.name, d.major, d.minor);
	return 0;
}
