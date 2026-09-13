/*
 * PPC64 libroot syscall ABI conformance smoke test.
 *
 * This deliberately tests the public libroot entry points rather than
 * calling the kernel dispatcher directly.  The purpose is to catch
 * register-width/truncation errors in the PPC64 syscall glue.
 */
#include <OS.h>
#include <SupportDefs.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syscalls.h>

#if !defined(__powerpc64__)
int main() { puts("SKIP: PPC64 syscall ABI test"); return 0; }
#else

static int
check(bool condition, const char* name)
{
	if (!condition) {
		printf("FAIL: %s\n", name);
		return 1;
	}
	printf("PASS: %s\n", name);
	return 0;
}

int
main()
{
	int failures = 0;

	// Exercise scalar return and 64-bit handle propagation.
	thread_id thread = find_thread(NULL);
	failures += check(thread >= 0, "find_thread return width");

	team_id team = getpid();
	failures += check(team >= 0, "getpid return width");

	// Exercise pointer arguments and errno/result handling through libroot.
	char buffer[32];
	memset(buffer, 0, sizeof(buffer));
	ssize_t n = read(-1, buffer, sizeof(buffer));
	failures += check(n < 0, "read invalid fd result");

	// A valid stack pointer must survive the syscall boundary unchanged.
	const char text[] = "ppc64-syscall\n";
	ssize_t written = write(1, text, sizeof(text) - 1);
	failures += check(written == (ssize_t)(sizeof(text) - 1),
		"write pointer/size arguments");

	printf("PPC64 syscall ABI conformance: %s (%d failures)\n",
		failures == 0 ? "PASS" : "FAIL", failures);
	return failures != 0;
}
#endif
