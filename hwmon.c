#include "hwmon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#define SYS_HWMON_PATH "/sys/class/hwmon/"
#define SYS_HWMON_PATH_LEN ((sizeof SYS_HWMON_PATH) - 1)

static struct hwmon_device_list* create_hwmon_device_storage(const char* name, const char* driver, const char* path)
{
	struct hwmon_device_list* device = (struct hwmon_device_list*)calloc(1, sizeof(struct hwmon_device_list));
	device->name = strdup(name);
	device->driver = strdup(driver);
	device->path = strdup(path);
	return device;
}

static void destroy_hwmon_device_storage(struct hwmon_device_list* device)
{
	free((void*)device->name);
	free((void*)device->driver);
	free((void*)device->path);
	free((void*)device);
}

// result must be free()'d
static char* mk_prop_fn(struct hwmon_device* device, const char* prop)
{
	size_t prop_len = strlen(prop);
	char* pathbuf = malloc(device->path_len + 1 + prop_len);
	memcpy(pathbuf, device->path, device->path_len);
	memcpy(pathbuf + device->path_len, prop, prop_len);
	pathbuf[device->path_len + prop_len] = '\0';
	return pathbuf;
}

static size_t read_line(struct hwmon_device* device, const char* prop, char* buffer, size_t buffer_size)
{
	/*const*/ char* prop_fn = mk_prop_fn(device, prop);
	int fd = open(prop_fn, O_RDONLY);

	if (fd == 0)
	{
		buffer[0] = '\0';
		return 0;
	}

	int pos = 0;
	int result = 0;

	while ((result = read(fd, buffer + pos, (buffer_size - 1) - pos)) > 0)
		pos += result;

	buffer[pos--] = '\0';

	while (pos >= 0 && buffer[pos] == '\n')
		buffer[pos--] = '\0';

	close(fd);
	free((void*)prop_fn);

	return pos;
}

static void write_line(struct hwmon_device* device, const char* prop, char* buffer, size_t buffer_size)
{
	/*const*/ char* prop_fn = mk_prop_fn(device, prop);
	int fd = open(prop_fn, O_WRONLY);

	if (fd == 0)
		return;

	int pos = 0;
	int result = 0;

	while ((result = write(fd, buffer + pos, buffer_size - pos)) > 0)
		pos += result;

	close(fd);
	free((void*)prop_fn);
}

static long long read_value(struct hwmon_device* device, const char* prop)
{
	char buffer[32];
	read_line(device, prop, buffer, sizeof buffer);
	return atoll(buffer);
}

static void write_value(struct hwmon_device* device, const char* prop, long long value)
{
	char buffer[32];
	int result = snprintf(buffer, sizeof buffer, "%lld", value);
	write_line(device, prop, buffer, result);
}

struct hwmon_device_list* hwmon_get_device_list()
{
	struct hwmon_device_list* first_device = NULL;
	struct hwmon_device_list* last_device = NULL;

	DIR* hwmon_dir = opendir(SYS_HWMON_PATH);

	if (hwmon_dir == NULL)
		return NULL;

	struct dirent* dir_entry;

	while ((dir_entry = readdir(hwmon_dir)))
	{
		size_t name_length = strlen(dir_entry->d_name);

		// skip the . and .. directory entries
		if (strcmp(dir_entry->d_name, ".") == 0 || strcmp(dir_entry->d_name, "..") == 0)
			continue;

		size_t path_length = name_length + SYS_HWMON_PATH_LEN + 1;

		// construct the device name
		char* devnamebuf = malloc(path_length + 1);
		memcpy(devnamebuf, SYS_HWMON_PATH, SYS_HWMON_PATH_LEN);
		memcpy(devnamebuf + SYS_HWMON_PATH_LEN, dir_entry->d_name, name_length);
		devnamebuf[path_length - 1] = '/';
		devnamebuf[path_length] = '\0';

		char driver_name[255];

		{
			struct hwmon_device temp_device;
			temp_device.path = devnamebuf;
			temp_device.path_len = path_length;
			read_line(&temp_device, "name", driver_name, sizeof driver_name);
		}

		struct hwmon_device_list* new_device = create_hwmon_device_storage(dir_entry->d_name, driver_name, devnamebuf);

		if (last_device == NULL)
			first_device = last_device = new_device;
		else
			last_device = last_device->next = new_device;

		free((void*)devnamebuf);
	}

	closedir(hwmon_dir);

	return first_device;
}

void hwmon_free_device_list(struct hwmon_device_list* device)
{
	if (!device)
		return;

	struct hwmon_device_list* next_ptr = device->next;

	while (next_ptr != NULL)
	{
		destroy_hwmon_device_storage(device);
		device = next_ptr;
		next_ptr = device->next;
	}
}

struct hwmon_device* hwmon_open_device(struct hwmon_device_list* device, const char* driver, int index)
{
	int counter = 0;

	for (; device != NULL; device = device->next)
	{
		if (strcmp(device->driver, driver) != 0)
			continue;

		int fd = open(device->path, 0);

		if (fd == 0)
			continue;

		if (counter != index)
		{
			++counter;
			continue;
		}

		struct hwmon_device* new_device = (struct hwmon_device*)calloc(1, sizeof(struct hwmon_device));
		new_device->path = strdup(device->path);
		new_device->path_len = strlen(device->path);
		return new_device;
	}

	return NULL;
}

void hwmon_close_device(struct hwmon_device* device)
{
	free((void*)device->path);
	free(device);
}

long long hwmon_read(struct hwmon_device* device, const char* prop)
{
	return read_value(device, prop);
}

size_t hwmon_read_str(struct hwmon_device* device, const char* prop, char* buf, size_t size)
{
	return read_line(device, prop, buf, size);
}

void hwmon_write(struct hwmon_device* device, const char* prop, long long value)
{
	write_value(device, prop, value);
}

void hwmon_write_str(struct hwmon_device* device, const char* prop, char* buf, size_t size)
{
	write_line(device, prop, buf, size);
}

