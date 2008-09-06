/**
 *
 * cpulimit - a cpu limiter for Linux
 *
 * Copyright (C) 2005-2008, by:  Angelo Marletta <marlonx80@hotmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

//TODO: add documentation to public functions

#include "process.h"

#ifdef __APPLE__
#include <Carbon/Carbon.h>
#endif

// returns the start time of a process (used with pid to identify a process)
static int get_starttime(pid_t pid)
{
#ifdef __linux__
	char file[20];
	char buffer[1024];
	sprintf(file, "/proc/%d/stat", pid);
	FILE *fd = fopen(file, "r");
		if (fd==NULL) return -1;
	fgets(buffer, sizeof(buffer), fd);
	fclose(fd);
	char *p = buffer;
	p = memchr(p+1,')', sizeof(buffer) - (p-buffer));
	int sp = 20;
	while (sp--)
		p = memchr(p+1,' ',sizeof(buffer) - (p-buffer));
	//start time of the process
	int time = atoi(p+1);
	return time;
#elif defined __APPLE__
	ProcessSerialNumber psn;
	ProcessInfoRec info;
	memset(&info, 0, sizeof(ProcessInfoRec));
	info.processInfoLength = sizeof(ProcessInfoRec);
	if (GetProcessForPID(pid, &psn)) return -1;
	if (GetProcessInformation(&psn, &info)) return -1;
	return info.processLaunchDate;
#endif
}

static int get_jiffies(struct process *proc) {
#ifdef __linux__
	FILE *f = fopen(proc->stat_file, "r");
	if (f==NULL) return -1;
	fgets(proc->buffer, sizeof(proc->buffer),f);
	fclose(f);
	char *p = proc->buffer;
	p = memchr(p+1,')', sizeof(proc->buffer) - (p-proc->buffer));
	int sp = 12;
	while (sp--)
		p = memchr(p+1,' ',sizeof(proc->buffer) - (p-proc->buffer));
	//user mode jiffies
	int utime = atoi(p+1);
	p = memchr(p+1,' ',sizeof(proc->buffer) - (p-proc->buffer));
	//kernel mode jiffies
	int ktime = atoi(p+1);
	return utime+ktime;
#elif defined __APPLE__
	ProcessSerialNumber psn;
	ProcessInfoRec info;
	if (GetProcessForPID(proc->pid, &psn)) return -1;
	if (GetProcessInformation(&psn, &info)) return -1;
	return info.processActiveTime;
#endif
}

//return t1-t2 in microseconds (no overflow checks, so better watch out!)
static inline unsigned long timediff(const struct timeval *t1,const struct timeval *t2)
{
	return (t1->tv_sec - t2->tv_sec) * 1000000 + (t1->tv_usec - t2->tv_usec);
}

/*static int*/ int process_update(struct process *proc) {
	//TODO: get any process statistic here
	//check that starttime is not changed(?), update jiffies, parent, zombie status
	return 0;
}

int process_init(struct process *proc, int pid)
{
	//general members
	proc->pid = pid;
	proc->starttime = get_starttime(pid);
	proc->cpu_usage = 0;
	memset(&(proc->last_sample), 0, sizeof(struct timeval));
	proc->last_jiffies = -1;
	//system dependent members
#ifdef __linux__
//TODO: delete these members for the sake of portability?
	//test /proc file descriptor for reading
	sprintf(proc->stat_file, "/proc/%d/stat", pid);
	FILE *fd = fopen(proc->stat_file, "r");
	if (fd == NULL) return 1;
	fclose(fd);
#endif
	return 0;
}

//parameter in range 0-1
#define ALFA 0.08

int process_monitor(struct process *proc)
{
	int j = get_jiffies(proc);
	if (j<0) return -1; //error retrieving jiffies count (maybe the process is dead)
	struct timeval now;
	gettimeofday(&now, NULL);
	if (proc->last_jiffies==-1) {
		//store current time
		proc->last_sample = now;
		//store current jiffies
		proc->last_jiffies = j;
		//it's the first sample, cannot figure out the cpu usage
		proc->cpu_usage = -1;
		return 0;
	}
	//time from previous sample (in ns)
	long dt = timediff(&now, &(proc->last_sample));
	//how many jiffies in dt?
	double max_jiffies = dt * HZ / 1000000.0;
	double sample = (j - proc->last_jiffies) / max_jiffies;
	if (proc->cpu_usage == -1) {
		//initialization
		proc->cpu_usage = sample;
	}
	else {
		//usage adjustment
		proc->cpu_usage = (1-ALFA) * proc->cpu_usage + ALFA * sample;
	}

	//store current time
	proc->last_sample = now;
	//store current jiffies
	proc->last_jiffies = j;
	
	return 0;
}

int process_close(struct process *proc)
{
	if (kill(proc->pid,SIGCONT)!=0) {
		fprintf(stderr,"Process %d is already dead!\n", proc->pid);
	}
	proc->pid = 0;
	return 0;
}
