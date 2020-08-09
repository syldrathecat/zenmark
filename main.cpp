#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <map>
#include <optional>
#include <vector>

#include "util.hpp"

// Incremented at base-clock rate while cpu is busy
#define MSR_MPERF 0xE7

// Incremented at actual clock rate while cpu is busy
#define MSR_APERF 0xE8

#define MSR_PSTATEDEF(n) (0xC0010064 + n)

// Test params
#define MAX_MC 8
#define BUSY_ITERS 10
#define BUSY_MS 20
#define TWEEN_SLEEP_US 0

extern "C"
{
#include "hwmon.h"
}

namespace
{

struct cpu_t
{
	int cpu_id;
	int core_id;

	cpu_set_t cpuset;
	int msr_fd;
};

struct core_result_t
{
	int max_freq;
	int avg_vcore;
};

struct core_t
{
	enum signal_id_t
	{
		SIGNAL_NONE,
		SIGNAL_START
	};

	int core_id;

	cpu_t* cpu;
	pthread_t thread_id;

	pthread_cond_t signal_cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t signal_mutex = PTHREAD_MUTEX_INITIALIZER;
	std::atomic<signal_id_t> signal_id = SIGNAL_NONE;
	std::atomic<void*> signal_data = nullptr;

	std::atomic<bool> done_flag = false;
	pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
	std::atomic<core_result_t> done_result;

	int test;

	void signal(signal_id_t id)
	{
		pthread_mutex_lock(&signal_mutex);
		signal_id.store(id);
		pthread_mutex_unlock(&signal_mutex);
		pthread_cond_signal(&signal_cond);
	}

	void start()
	{
		signal(SIGNAL_START);
	}

	core_result_t await_result()
	{
		pthread_mutex_lock(&done_mutex);

		while (!done_flag)
			pthread_cond_wait(&done_cond, &done_mutex);

		core_result_t result = done_result.load();
		done_flag = false;

		pthread_mutex_unlock(&done_mutex);

		return result;
	}
};

uint64_t tsc_freq;

int vcore_prop_available;
char vcore_prop[32];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
std::map<int, cpu_t> cpu_data;
std::map<int, core_t> core_data;
#pragma clang diagnostic pop

void busyloop(int ms)
{
	uint64_t start = util::rdtsc();

	uint64_t off = tsc_freq * ms / 1000;

	while (util::rdtsc() < start + off)
	{
		;
	}
}

void* thread_proc(void* thread_data)
{
	core_t& core = *(core_t*)thread_data;

	core.test = core.core_id;

	hwmon_device_list* hwmon_devices = hwmon_get_device_list();
	hwmon_device* zenpower = hwmon_open_device(hwmon_devices, "k10temp", 0);

	pthread_mutex_lock(&core.signal_mutex);

	while (1)
	{
		pthread_cond_wait(&core.signal_cond, &core.signal_mutex);

		switch (core.signal_id)
		{
			case core_t::SIGNAL_START:
				//printf("Core %d goes Meow!\n", core.core_id);
			{
				int vcore_acc = 0;
				uint64_t max_ticks = 0;

				// Warm-up
				sleep(0);
				busyloop(BUSY_MS);

				for (int i = 0; i < BUSY_ITERS; ++i)
				{
					usleep(TWEEN_SLEEP_US);

					uint64_t start = util::rdmsr(core.cpu->msr_fd, MSR_APERF);
					busyloop(BUSY_MS);
					uint64_t end = util::rdmsr(core.cpu->msr_fd, MSR_APERF);

					int vcore = hwmon_read(zenpower, vcore_prop);

					uint64_t ticks = end - start;

					if (ticks > max_ticks)
						max_ticks = ticks;

					vcore_acc += vcore;
				}

				core_result_t result;

				result.max_freq = max_ticks * (1000 / BUSY_MS) / 1000000;
				result.avg_vcore = vcore_acc / BUSY_ITERS;

				pthread_mutex_lock(&core.done_mutex);
				core.done_result.store(result);
				core.done_flag = true;
				pthread_cond_signal(&core.done_cond);
				pthread_mutex_unlock(&core.done_mutex);
			}
				break;

			case core_t::SIGNAL_NONE:
				perror("OOPSIE WOOPSIE");
				break;
		}

		/*
		uint64_t start = util::rdtsc();

		while (util::rdtsc() < start + 100000000ULL)
			;*/

		// Cancel-point
		sleep(0);
	}

	hwmon_close_device(zenpower);
	hwmon_free_device_list(hwmon_devices);

	//return nullptr;
}

void setup_threads()
{
	cpu_set_t mt_cpuset;

	sched_getaffinity(0, sizeof mt_cpuset, &mt_cpuset);

	int cpu_count = CPU_COUNT(&mt_cpuset);
	int cpus_detected = 0;

	std::map<int, int> core_primary_cpu_idx;

	//cpu_data.reserve(cpu_count);

	for (int i = 0; i < CPU_SETSIZE && cpus_detected != cpu_count; ++i)
	{
		if (CPU_ISSET(i, &mt_cpuset))
		{
			++cpus_detected;

			cpu_set_t cpu_cpuset;

			CPU_ZERO(&cpu_cpuset);
			CPU_SET(i, &cpu_cpuset);

			char cpu_core_id_filename[100];

			snprintf(cpu_core_id_filename, sizeof cpu_core_id_filename, "/sys/devices/system/cpu/cpu%d/topology/core_id", i);

			int core_id = util::read_value<int>(cpu_core_id_filename).value_or(-1);

			assert(core_id >= 0);

			cpu_data[i] = cpu_t{
				i,
				core_id,
				cpu_cpuset,
				util::open_cpu_msr(i)
			};

			if (core_primary_cpu_idx.count(core_id) == 0)
				core_primary_cpu_idx.insert({core_id, i});
		}
		else
		{
			printf("Skipping core %i\n", i);
		}
	}

	printf("*** %d cores (%d CPUs) detected.\n", (int)core_primary_cpu_idx.size(), cpus_detected);

	for (auto&& [core_id, cpu_id] : core_primary_cpu_idx)
	{
		auto&& core = core_data[core_id];
		auto&& cpu = cpu_data[cpu_id];

		core.core_id = core_id;
		core.cpu = &cpu;

		printf("Creating thread core %d CPU %d\n", core_id, cpu.cpu_id);

		pthread_create(&core.thread_id, nullptr, thread_proc, &core);
		pthread_setaffinity_np(core.thread_id, sizeof cpu.cpuset, &cpu.cpuset);
	}
}

void cleanup_threads()
{
	for (auto&& [core_id, core] : core_data)
	{
		pthread_cancel(core.thread_id);
		pthread_join(core.thread_id, nullptr);
	}
}

}

static uint64_t actual_freq;

int main()
{
	hwmon_device_list* hwmon_devices = hwmon_get_device_list();
	hwmon_device* zenpower = hwmon_open_device(hwmon_devices, "k10temp", 0);

	if (!zenpower)
	{
		puts("k10temp unavailable");
		return 1;
	}

	char in1_label[32];
	char in2_label[32];

	hwmon_read_str(zenpower, "in0_label", in1_label, sizeof in1_label);
	hwmon_read_str(zenpower, "in1_label", in1_label, sizeof in1_label);

	if (strcmp(in1_label, "Vcore"))
	{
		strcpy(vcore_prop, "in0_input");
		vcore_prop_available = 1;
	}
	else if (strcmp(in2_label, "Vcore"))
	{
		strcpy(vcore_prop, "in1_input");
		vcore_prop_available = 1;
	}

	if (!vcore_prop_available)
	{
		puts("VCore unavailable. (k10temp kernel module required)");
		return 1;
	}

	puts("*** Estimating base frequency...");
	tsc_freq = util::rdtsc();
	usleep(2000000);
	tsc_freq = (util::rdtsc() - tsc_freq) / 2;
	printf("*** TSC Frequency: %llu MHz\n", (unsigned long long)(tsc_freq + 500000) / 1000000);

	setup_threads();

	usleep(10000);

	//int max_freqs[MAX_MC];

	int n_cores = 0;

	for (auto mc_iter = core_data.begin(); mc_iter != core_data.end(); ++mc_iter, ++n_cores)
	{
		for (auto mc_iter_2 = mc_iter; mc_iter_2 != core_data.end(); ++mc_iter_2)
		{
			auto&& [core_no, core] = *mc_iter_2;

			printf("*** Core ");

			{
				auto mc_iter_3 = core_data.begin();
				for (int ii = 0; ii < n_cores; ++ii, ++mc_iter_3)
				{
					auto&& core_no = mc_iter_3->first;
					printf("%d+", core_no);
				}
			}

			printf("%d :: ", core_no);
			fflush(stdout);

			{
				auto mc_iter_3 = core_data.begin();
				for (int ii = 0; ii < n_cores; ++ii, ++mc_iter_3)
				{
					auto&& core = mc_iter_3->second;
					core.start();
				}
			}

			core.start();

			//for (int ii = 0; ii < mc; ++ii)
			//	max_freqs[ii] = core_data[ii].await_result().max_freq;

			core_result_t result0;
			core_result_t result = core.await_result();

			if (core_no > 0 && n_cores > 0)
				result0 = core_data[0].await_result();
			else
				result0 = result;

			printf(" Freq = ");

			//for (int ii = 0; ii < mc; ++ii)
			//	printf("%d+", max_freqs[ii]);

			printf ("%d-%d MHz", result.max_freq, result0.max_freq);
			printf("  VCore = %d mV\n", result.avg_vcore);

			fflush(stdout);
		}
	}

	usleep(10000);

	cleanup_threads();

	hwmon_close_device(zenpower);
	hwmon_free_device_list(hwmon_devices);
}
