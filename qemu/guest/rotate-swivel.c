/* rotate-swivel: emulate the RG Rotate swivel hall sensor inside the guest.
 *   rotate-swivel emit <0|1> [seconds]  create uinput dev "rotate-swivel-emu" (EV_SW/SW_LID),
 *                                       set state (1 = closed), hold open [seconds, default until killed]
 *   rotate-swivel read                  find that device, print SW_LID via EVIOCGSW (0/1), exit 0 on success
 * Build: aarch64-linux-gnu-gcc -static -O2 -o rotate-swivel rotate-swivel.c */
#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#define NAME "rotate-swivel-emu"
#define BITS(n) (((n) + 7) / 8)
static int emit(int fd, int t, int c, int v) {
	struct input_event e; memset(&e, 0, sizeof e); e.type = t; e.code = c; e.value = v;
	return write(fd, &e, sizeof e) == sizeof e ? 0 : -1;
}
static int do_emit(int state, int secs) {
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) { perror("open /dev/uinput (modprobe uinput?)"); return 1; }
	struct uinput_setup us; memset(&us, 0, sizeof us);
	us.id.bustype = BUS_VIRTUAL; us.id.vendor = 0x1209; us.id.product = 0x0618;
	strcpy(us.name, NAME);
	if (ioctl(fd, UI_SET_EVBIT, EV_SW) || ioctl(fd, UI_SET_SWBIT, SW_LID) ||
	    ioctl(fd, UI_DEV_SETUP, &us) || ioctl(fd, UI_DEV_CREATE)) { perror("uinput setup"); return 1; }
	usleep(300000);
	if (emit(fd, EV_SW, SW_LID, state) || emit(fd, EV_SYN, SYN_REPORT, 0)) { perror("emit"); return 1; }
	printf("swivel SW_LID=%d emitted\n", state); fflush(stdout);
	if (secs > 0) sleep(secs); else for (;;) pause();
	ioctl(fd, UI_DEV_DESTROY); close(fd);
	return 0;
}
static int do_read(void) {
	DIR *d = opendir("/dev/input"); struct dirent *de;
	if (!d) { perror("/dev/input"); return 1; }
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "event", 5)) continue;
		char p[300], nm[128] = ""; snprintf(p, sizeof p, "/dev/input/%s", de->d_name);
		int fd = open(p, O_RDONLY); if (fd < 0) continue;
		ioctl(fd, EVIOCGNAME(sizeof nm), nm);
		if (!strcmp(nm, NAME)) {
			unsigned char sw[BITS(SW_MAX + 1)]; memset(sw, 0, sizeof sw);
			if (ioctl(fd, EVIOCGSW(sizeof sw), sw) < 0) { perror("EVIOCGSW"); return 1; }
			printf("%d\n", !!(sw[SW_LID / 8] & (1 << (SW_LID % 8))));
			return 0;
		}
		close(fd);
	}
	fprintf(stderr, "device %s not found\n", NAME); return 2;
}
int main(int argc, char **argv) {
	if (argc >= 3 && !strcmp(argv[1], "emit")) return do_emit(atoi(argv[2]), argc > 3 ? atoi(argv[3]) : 0);
	if (argc >= 2 && !strcmp(argv[1], "read")) return do_read();
	fprintf(stderr, "usage: %s emit <0|1> [secs] | read\n", argv[0]); return 64;
}
