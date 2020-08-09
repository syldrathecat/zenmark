#ifndef ZENMARK_UTIL_HPP
#define ZENMARK_UTIL_HPP

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>

#include <charconv>
#include <optional>

#define my_perror_f(fmt, ...) \
	fprintf(stderr,  "%s: " fmt ": %s\n", __func__, __VA_ARGS__, strerror(errno))

#define my_perror_errno_f(err, fmt, ...) \
	fprintf(stderr,  "%s: " fmt ": %s\n", __func__, __VA_ARGS__, strerror((err)))

#define my_perror(msg) \
	fprintf(stderr,  "%s: %s: %s\n", __func__, msg, strerror(errno))

#define my_perror_errno(err, msg) \
	fprintf(stderr,  "%s: %s: %s\n", __func__, msg, strerror((err)))

namespace util
{

uint64_t rdtsc()
{
	uint32_t high;
	uint32_t low;

	asm (
		  "rdtsc"
		: "=d" (high), "=a" (low)
	);

	return ((uint64_t)high << 32) | low;
}

int open_cpu_msr(int cpu)
{
	char filename[100];
	snprintf(filename, sizeof filename, "/dev/cpu/%d/msr", cpu);

	int fd = open(filename, O_RDONLY);

	if (fd < 0)
		my_perror_f("open(%s, %d) failed", filename, O_RDONLY);

	return fd;
}

uint64_t rdmsr(int fd, uint32_t reg)
{
	uint64_t value;

	if (pread(fd, &value, sizeof value, reg) != sizeof value)
	{
		my_perror_f("pread(%d, ..., %d, %08x) failed", fd, sizeof value, reg);
		return 0;
	}

	return value;
}

void wrmsr(int fd, uint32_t reg, uint64_t value)
{
	pwrite(fd, &value, sizeof value, reg);
}

template <class T> std::optional<T> read_value(const char* filename)
{
	char buf[32];
	FILE* fh = fopen(filename, "rt");

	if (!fh)
	{
		my_perror_f("fopen(%s) failed", filename);
		return std::nullopt;
	}

	size_t chars_read = fread((void*)buf, 1, sizeof buf, fh);

	if (!chars_read)
	{
		my_perror_f("fread(%s) failed", filename);
		fclose(fh);
		return std::nullopt;
	}

	fclose(fh);

	T result_value;

	auto result = std::from_chars(buf, buf + chars_read, result_value);

	if (result.ec != std::errc{})
	{
		my_perror_errno((int)result.ec, "std::from_chars failed");
		return std::nullopt;
	}

	return result_value;
}

}

#endif // ZENMARK_UTIL_HPP
