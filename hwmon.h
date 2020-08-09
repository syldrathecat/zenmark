#ifndef SYSMON_HWMON_H
#define SYSMON_HWMON_H

#include <stddef.h>

struct hwmon_device_list
{
	struct hwmon_device_list* next;
	/*const*/ char* name;
	/*const*/ char* driver;
	/*const*/ char* path;
};

struct hwmon_device
{
	/*const*/ char* path;
	size_t path_len;
};

struct hwmon_device_list* hwmon_get_device_list();
void hwmon_free_device_list(struct hwmon_device_list* device);

struct hwmon_device* hwmon_open_device(struct hwmon_device_list*, const char* driver, int index);
void hwmon_close_device(struct hwmon_device*);

long long hwmon_read(struct hwmon_device*, const char* prop);
size_t hwmon_read_str(struct hwmon_device*, const char* prop, char* buf, size_t size);

void hwmon_write(struct hwmon_device*, const char* prop, long long value);
void hwmon_write_str(struct hwmon_device*, const char* prop, char* buf, size_t size);

#endif // SYSMON_HWMON_H
