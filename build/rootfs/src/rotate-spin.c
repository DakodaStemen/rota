// SPDX-License-Identifier: MIT
// rotate-spin [LOOPS]   fixed dependent-add busy loop for the cpufreq timing plan (docs/research/03-cpufreq.md, step 1).
// Runs LOOPS x 16 dependent 'add x0,x0,#1' (default 25,000,000 => 4e8 adds), prints seconds and an effective MHz that
// ASSUMES 1 cycle per add (true for A55/A75 dependent integer adds; loop overhead dual-issues). Compare ratios across OPPs;
// do not read the absolute MHz as a calibrated number. Pin it with taskset. Exit: 0 ok, 1 usage.
// Build (aarch64): gcc -O1 -o rotate-spin rotate-spin.c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv)
{
	long loops = 25000000;
	if (argc > 2) { fprintf(stderr, "usage: %s [LOOPS]\n", argv[0]); return 1; }
	if (argc == 2) loops = atol(argv[1]);
	if (loops <= 0) { fprintf(stderr, "usage: %s [LOOPS]\n", argv[0]); return 1; }
	struct timespec a, b; unsigned long x = 0; long n = loops;
	clock_gettime(CLOCK_MONOTONIC, &a);
#if defined(__aarch64__)
	__asm__ volatile("1:\n"
		"add %0,%0,#1\n add %0,%0,#1\n add %0,%0,#1\n add %0,%0,#1\n"
		"add %0,%0,#1\n add %0,%0,#1\n add %0,%0,#1\n add %0,%0,#1\n"
		"add %0,%0,#1\n add %0,%0,#1\n add %0,%0,#1\n add %0,%0,#1\n"
		"add %0,%0,#1\n add %0,%0,#1\n add %0,%0,#1\n add %0,%0,#1\n"
		"subs %1,%1,#1\n b.ne 1b\n" : "+r"(x), "+r"(n) : : "cc");
#else
	for (; n; n--) for (int i = 0; i < 16; i++) __asm__ volatile("" : "+r"(x));
#endif
	clock_gettime(CLOCK_MONOTONIC, &b);
	double s = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
	printf("secs=%.4f eff_mhz=%.1f sum=%lu\n", s, loops * 16.0 / s / 1e6, x);
	return 0;
}
