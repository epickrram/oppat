/* Copyright (c) 2018 Patrick Fay
 *
 * License http://opensource.org/licenses/mit-license.php MIT License
 */

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <assert.h>
#define pid_t int
#else
#include <wordexp.h>
#endif

#include <iostream>
#include <time.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <string>
#include <cstddef>
#include <inttypes.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
//#include <arm_neon.h>
#include <ctime>
#include <cstdio>
#include <string>
#include <thread>
#include <mutex>
#include <iostream>
#include <vector>
#include <sys/types.h>
//#define _GNU_SOURCE         /* See feature_test_macros(7) */

#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#endif

#include "trace_marker.h"
#include "utils.h"

enum { // below enum has to be in same order as wrk_typs
	WRK_SPIN,
	WRK_MEM_BW,
	WRK_MEM_BW_RDWR,
	WRK_MEM_BW_2RD,
	WRK_MEM_BW_2RDWR,
	WRK_MEM_BW_2RD2WR,
	WRK_DISK_RD,
	WRK_DISK_WR,
	WRK_DISK_RDWR,
	WRK_DISK_RD_DIR,
	WRK_DISK_WR_DIR,
	WRK_DISK_RDWR_DIR,
};

static std::vector <std::string> wrk_typs = {
	"spin",
	"mem_bw",
	"mem_bw_rdwr",
	"mem_bw_2rd",
	"mem_bw_2rdwr",
	"mem_bw_2rd2wr",
	"disk_rd",
	"disk_wr",
	"disk_rdwr",
	"disk_rd_dir",
	"disk_wr_dir",
	"disk_rdwr_dir"
};


pid_t mygettid(void)
{
	pid_t tid = -1;
	//pid_t tid = (pid_t) syscall (SYS_gettid);
#ifdef __linux__
	tid = (pid_t) syscall (__NR_gettid);
#endif
#ifdef _WIN32
	tid = (int)GetCurrentThreadId();
#endif
	return tid;
}

#if 0
//get from utils.cpp now
double dclock(void)
{
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return (double)(tp.tv_sec) + 1.0e-9 * (double)(tp.tv_nsec);
}
#endif

#ifdef __linux__
double dclock_vari(clockid_t clkid)
{
	struct timespec tp;
	clock_gettime(clkid, &tp);
	return (double)(tp.tv_sec) + 1.0e-9 * (double)(tp.tv_nsec);
}
#endif

struct args_str {
	std::string work, filename, phase;
	double spin_tm;
	unsigned long rezult, loops, adder;
	double tm_beg, tm_end, perf, dura;
	int id, wrk_typ;
	std::string loops_str, adder_str, units;
	args_str(): spin_tm(0.0), rezult(0), loops(0), adder(0),
		tm_beg(0.0), tm_end(0.0), perf(0.0), dura(0.0), id(-1), wrk_typ(-1) {}
};

uint64_t do_scale(uint64_t loops, uint64_t rez, uint64_t adder, uint64_t &ops)
{
	uint64_t rezult = rez;
	for (uint64_t j = 0; j < loops; j++) {
		rezult += j;
	}
	ops += loops;
	return rezult;
}

std::vector <args_str> args;

int array_write(char *buf, int ar_sz, int strd)
{
	int res=0;
	for (int i=0; i < ar_sz-strd; i += strd) {
		buf[i] = i;
	}
	return res;
}

int array_read_write(char *buf, int ar_sz, int strd)
{
	int res=0;
	for (int i=0; i < ar_sz-strd; i += strd) {
		buf[i] += i;
	}
	return res;
}

int array_2read_write(char *dst, char *src, int ar_sz, int strd)
{
	int res=0;
	for (int i=0; i < ar_sz-strd; i += strd) {
		dst[i] += src[i];
	}
	return res;
}

int array_2read_2write(char *dst, char *src, int ar_sz, int strd)
{
	int res=0;
	for (int i=0; i < ar_sz-strd; i += strd) {
		dst[i] += res;
		src[i] += res;
		res++;
	}
	return res;
}

int array_2read(char *dst, char *src, int ar_sz, int strd)
{
	int res=0;
	for (int i=0; i < ar_sz-strd; i += strd) {
		res += dst[i] + src[i];
	}
	return res;
}

int array_read(char *buf, int ar_sz, int strd)
{
	int res=0;
	for (int i=0; i < ar_sz-strd; i += strd) {
		res += buf[i];
	}
	return res;
}

static size_t sys_getpagesize(void)
{
	size_t pg_sz = 4096; // sane? default
#ifdef __linux__
	pg_sz = sys_getpagesize();
#else
	SYSTEM_INFO siSysInfo;
	// Copy the hardware information to the SYSTEM_INFO structure.
	GetSystemInfo(&siSysInfo);
	pg_sz = siSysInfo.dwPageSize;
#endif
	return pg_sz;
}

static int alloc_pg_bufs(int num_bytes, char **buf_ptr, int pg_chunks)
{
	size_t pg_sz = sys_getpagesize();
	size_t use_bytes = num_bytes;
	if ((use_bytes % pg_sz) != 0) {
		int num_pgs = use_bytes/pg_sz;
		use_bytes = (num_pgs+1) * pg_sz;
	}
	if ((use_bytes % (pg_chunks * pg_sz)) != 0) {
		int num_pgs = use_bytes/(pg_chunks*pg_sz);
		use_bytes = (num_pgs+1) * (pg_chunks*pg_sz);
	}
	int rc=0;
#ifdef __linux__
	rc = posix_memalign((void **)buf_ptr, pg_sz, use_bytes);
#else
	*buf_ptr = (char *)_aligned_malloc(use_bytes, pg_sz);
#endif
	if (rc != 0 || *buf_ptr == NULL) {
		printf("failed to malloc %" PRId64 " bytes on alignment= %d at %s %d\n",
			use_bytes, (int)pg_sz, __FILE__, __LINE__);
		exit(1);
	}
	return pg_sz;
}

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef O_DIRECT
#define O_DIRECT 0
#endif

static size_t disk_write_dir(int myi, int ar_sz)
{
	char *buf;
	size_t byts = 0;
	int val=0, loops = args[myi].loops, pg_chunks=16;

	int pg_sz = alloc_pg_bufs(ar_sz, &buf, pg_chunks);
#ifdef _WIN32
	HANDLE fd = CreateFile(args[myi].filename.c_str(), GENERIC_WRITE,
		0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
#else
	int fd = open(args[myi].filename.c_str(), O_WRONLY|O_CREAT|O_BINARY|O_DIRECT, 0x666);
#endif
	if (!fd)
	{
		printf("Unable to open file %s at %s %d\n", args[myi].filename.c_str(), __FILE__, __LINE__);
		exit(1);
	}
	int num_pages = ar_sz / pg_sz;
	for (int i=0; i < loops; i++) {
		for (int j= 0; j < ar_sz; j+= 256) {
			buf[j] += j + val;
		}
		val++;
		for (int j=0; j < num_pages; j += pg_chunks) {
#ifdef _WIN32
			DWORD dwWritten=0;
			WriteFile(fd, buf+(j*pg_sz), pg_chunks*pg_sz, &dwWritten, NULL);
			byts += dwWritten;
#else
			byts += write(fd, buf+(j*pg_sz), pg_chunks*pg_sz);
#endif
		}
	}
	args[myi].rezult = val;
#ifdef _WIN32
	CloseHandle(fd);
#else
	close(fd);
#endif
	return byts;
}

static size_t disk_read_dir(int myi, int ar_sz)
{
	int val=0, pg_chunks=16;
	char *buf;
	size_t ret;
	size_t byts = 0;
	int loops = args[myi].loops;

	int pg_sz = alloc_pg_bufs(ar_sz, &buf, pg_chunks);
#ifdef _WIN32
	HANDLE fd = CreateFile(args[myi].filename.c_str(), GENERIC_READ,
		0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
#else
	int fd = open(args[myi].filename.c_str(), O_RDONLY | O_BINARY | O_DIRECT, 0);
#endif
	if (!fd)
	{
		printf("Unable to open file %s at %s %d\n", args[myi].filename.c_str(), __FILE__, __LINE__);
		exit(1);
	}
	int num_pages = ar_sz / pg_sz;
	for (int i=0; i < loops; i++) {
		for (int j=0; j < num_pages; j += pg_chunks) {
#ifdef _WIN32
			DWORD dwGot=0;
			ReadFile(fd, buf+(j*pg_sz), pg_chunks*pg_sz, &dwGot, NULL);
			byts += dwGot;
#else
			byts += read(fd, buf+(j*pg_sz), pg_chunks*pg_sz);
#endif
		}
		for (int j= 0; j < ar_sz; j+= 256) {
			val += buf[j];
		}
	}
#ifdef _WIN32
	CloseHandle(fd);
#else
	close(fd);
#endif
	args[myi].rezult = val;
	return byts;
}

static size_t disk_write(int myi, int ar_sz)
{
	FILE *fp;
	int val=0, pg_chunks=16;
	char *buf;
	size_t byts=0;
	int loops = args[myi].loops;

	int pg_sz = alloc_pg_bufs(ar_sz, &buf, pg_chunks);
	fp = fopen(args[myi].filename.c_str(), "wb");
	if (!fp)
	{
		printf("Unable to open file %s at %s %d\n", args[myi].filename.c_str(), __FILE__, __LINE__);
		exit(1);
	}
	int num_pages = ar_sz / pg_sz;
	for (int i=0; i < loops; i++) {
		for (int j= 0; j < ar_sz; j+= 512) {
			buf[j] += j + val;
		}
		val++;
		for (int j=0; j < num_pages; j += pg_chunks) {
			byts += fwrite(buf+(j*pg_sz), pg_chunks*pg_sz, 1, fp);
		}
	}
	byts *= pg_chunks * pg_sz;
	args[myi].rezult = val;
	fclose(fp);
	return byts;
}

static size_t disk_read(int myi, int ar_sz)
{
	FILE *fp;
	int val=0, pg_chunks=16;
	char *buf;
	size_t ret;
	size_t byts=0;
	int loops = args[myi].loops;

	int pg_sz = alloc_pg_bufs(ar_sz, &buf, pg_chunks);
	fp = fopen(args[myi].filename.c_str(), "rb");
	if (!fp)
	{
		printf("Unable to open file %s at %s %d\n", args[myi].filename.c_str(), __FILE__, __LINE__);
		exit(1);
	}
	int num_pages = ar_sz / pg_sz;
	for (int i=0; i < loops; i++) {
		for (int j=0; j < num_pages; j += pg_chunks) {
			byts += fread(buf+(j*pg_sz), pg_chunks*pg_sz, 1, fp);
		}
		for (int j= 0; j < ar_sz; j+= 512) {
			val += buf[j];
		}
	}
	byts *= pg_chunks * pg_sz;
	fclose(fp);
	args[myi].rezult = val;
	return byts;
}

float disk_all(unsigned int i)
{
	double tm_to_run = args[i].spin_tm;
	int cpu =  args[i].id;
	uint64_t j;
	uint64_t rezult = 0;
	double tm_end, tm_beg, bytes=0.0;
	int arr_sz = 1024*1024;
	uint64_t adder = args[i].adder;
	args[i].filename = "disk_tst_" + std::to_string(i);
	tm_end = tm_beg = dclock();
	while((tm_end - tm_beg) < tm_to_run) {
		switch(args[i].wrk_typ) {
			case WRK_DISK_WR:
				bytes += disk_write(i, arr_sz);
				break;
			case WRK_DISK_RDWR:
				bytes += disk_write(i, arr_sz);
			case WRK_DISK_RD:
				bytes += disk_read(i, arr_sz);
				break;
			case WRK_DISK_WR_DIR:
				bytes += disk_write_dir(i, arr_sz);
				break;
			case WRK_DISK_RDWR_DIR:
				bytes += disk_write_dir(i, arr_sz);
			case WRK_DISK_RD_DIR:
				bytes += disk_read_dir(i, arr_sz);
				break;
		}
		tm_end = dclock();
	}
	double dura2, dura;
	dura2 = dura = tm_end - tm_beg;
	if (dura2 == 0.0) {
		dura2 = 1.0;
	}
	args[i].dura = dura;
	args[i].perf = 1.0e-6 * (double)(bytes)/dura2;
	args[i].units = "MiB/sec";
	printf("cpu[%d]: tid= %d, beg/end= %f,%f, dura= %f, %s= %f\n",
		cpu, mygettid(), tm_beg, tm_end, dura, args[i].units.c_str(), args[i].perf);
	args[i].rezult = bytes;
	args[i].tm_beg = tm_beg;
	args[i].tm_end = tm_end;
	return 0;
}

float mem_bw(unsigned int i)
{
	char *dst, *src;
	double tm_to_run = args[i].spin_tm;
	int cpu =  args[i].id;
	//unsigned int i;
	uint64_t ops = 0;
	uint64_t j, loops;
	uint64_t rezult = 0;
	double tm_end, tm_beg, bytes=0.0;
	int strd = (int)args[i].loops;
	int arr_sz = (int)args[i].adder;
	if (args[i].adder_str.size() > 0 && (strstr(args[i].adder_str.c_str(), "k") || strstr(args[i].adder_str.c_str(), "K"))) {
		arr_sz *= 1024;
	}
	if (args[i].adder_str.size() > 0 && (strstr(args[i].adder_str.c_str(), "m") || strstr(args[i].adder_str.c_str(), "M"))) {
		arr_sz *= 1024*1024;
	}
	if (i==0) {
		printf("strd= %d, arr_sz= %d, %d KB, %.4f MB\n",
			strd, arr_sz, arr_sz/1024, (double)(arr_sz)/(1024.0*1024.0));
	}
	dst = (char *)malloc(arr_sz);
	array_write(dst, arr_sz, strd);
	if (args[i].wrk_typ == WRK_MEM_BW_2RDWR ||
		args[i].wrk_typ == WRK_MEM_BW_2RD ||
		args[i].wrk_typ == WRK_MEM_BW_2RD2WR) {
		src = (char *)malloc(arr_sz);
		array_write(src, arr_sz, strd);
	}
	tm_end = tm_beg = dclock();
	while((tm_end - tm_beg) < tm_to_run) {
		if (args[i].wrk_typ == WRK_MEM_BW) {
			rezult += array_read(dst, arr_sz, strd);
			bytes += arr_sz;
		}
		else if (args[i].wrk_typ == WRK_MEM_BW_RDWR) {
			rezult += array_read_write(dst, arr_sz, strd);
			bytes += 2*arr_sz;
		}
		else if (args[i].wrk_typ == WRK_MEM_BW_2RD) {
			rezult += array_2read(dst, src, arr_sz, strd);
			bytes += 2*arr_sz;
		}
		else if (args[i].wrk_typ == WRK_MEM_BW_2RDWR) {
			rezult += array_2read_write(dst, src, arr_sz, strd);
			bytes += 3*arr_sz;
		}
		else if (args[i].wrk_typ == WRK_MEM_BW_2RD2WR) {
			rezult += array_2read_2write(dst, src, arr_sz, strd);
			bytes += 4*arr_sz;
		}
		tm_end = dclock();
	}
	double dura2, dura;
	dura2 = dura = tm_end - tm_beg;
	if (dura2 == 0.0) {
		dura2 = 1.0;
	}
	args[i].dura = dura;
	args[i].units = "GB/sec";
	args[i].perf = 1.0e-9 * (double)(bytes)/(dura2);
	printf("cpu[%d]: tid= %d, beg/end= %f,%f, dura= %f, Gops= %f, %s= %f\n",
		cpu, mygettid(), tm_beg, tm_end, dura, 1.0e-9 * (double)ops, args[i].units.c_str(), args[i].perf);
	args[i].rezult = rezult;
	return rezult;
}

float simd_dot0(unsigned int i)
{
	double tm_to_run = args[i].spin_tm;
	int cpu =  args[i].id;
	//unsigned int i;
	uint64_t ops = 0;
	uint64_t j, loops;
	uint64_t rezult = 0;
	double tm_end, tm_beg;
	loops = args[i].loops;
	uint64_t adder = args[i].adder;
	tm_end = tm_beg = dclock();
	while((tm_end - tm_beg) < tm_to_run) {
#if 1
		rezult = do_scale(loops, rezult, adder, ops);
#else
		for (j = 0; j < loops; j++) {
			rezult += j;
		}
		ops += loops;
#endif
		tm_end = dclock();
	}
	double dura2, dura;
	dura2 = dura = tm_end - tm_beg;
	if (dura2 == 0.0) {
		dura2 = 1.0;
	}
	args[i].dura = dura;
	args[i].units = "Gops/sec";
	args[i].perf = 1.0e-9 * (double)(ops)/(dura2);
	printf("cpu[%d]: tid= %d, beg/end= %f,%f, dura= %f, Gops= %f, %s= %f\n",
		cpu, mygettid(), tm_beg, tm_end, dura, 1.0e-9 * (double)ops, args[i].units.c_str(), args[i].perf);
	args[i].rezult = rezult;
	return rezult;
}

float dispatch_work(int  i)
{
	float res=0.0;
	int wrk = args[i].wrk_typ;

	trace_marker_write("Begin "+wrk_typs[wrk]+" for thread= "+std::to_string(i));
	switch(wrk) {
		case WRK_SPIN:
			res = simd_dot0(i);
			break;
		case WRK_MEM_BW:
		case WRK_MEM_BW_RDWR:
		case WRK_MEM_BW_2RDWR:
		case WRK_MEM_BW_2RD2WR:
		case WRK_MEM_BW_2RD:
			res = mem_bw(i);
			break;
		case WRK_DISK_RD:
		case WRK_DISK_WR:
		case WRK_DISK_RDWR:
		case WRK_DISK_RD_DIR:
		case WRK_DISK_WR_DIR:
		case WRK_DISK_RDWR_DIR:
			res = disk_all(i);
			break;
		default:
			break;
	}
	trace_marker_write("End "+wrk_typs[wrk]+" for thread= "+std::to_string(i));
	return res;
}



std::vector <std::string> split_cmd_line(const char *argv0, const char *cmdline, int *argc)
{
	std::vector <std::string> std_argv;
	int slen = strlen(cmdline);
	int i=0, arg = -1;
	char *argv[256];
	char *cp = (char *)malloc(slen+1);
	argv[++arg] = (char *)argv0;
	strcpy(cp, cmdline);
	char qt[2];
	qt[0] = 0;
	while (i < slen) {
		if (cp[i] != ' ') {
			if (cp[i] == '"' || cp[i] == '\'') {
			   qt[0] = cp[i];
			   i++;
			}
			argv[++arg] = cp+i;
			while (((cp[++i] != ' ' && qt[0] == 0) || (qt[0] != 0 && cp[i] != qt[0])) && i < slen) {
				;
			}
			if (qt[0] != 0 && cp[i] == qt[0]) {
				qt[0] = 0;
				cp[i] = 0;
				i++;
			}
		}
		if (cp[i] == ' ') {
			cp[i] = 0;
			i++;
		}
	}
	for (i=0; i <= arg; i++) {
		//printf("arg[%d]= '%s'\n", i, argv[i]);
		std_argv.push_back(argv[i]);
	}
	*argc = (int)std_argv.size();
	return std_argv;
}

int read_options_file(std::string argv0, std::string opts, std::vector <std::vector <std::string>> &argvs)
{
	std::ifstream file2;
	file2.open (opts.c_str(), std::ios::in);
	if (!file2.is_open()) {
		printf("messed up fopen of flnm= %s at %s %d\n", opts.c_str(), __FILE__, __LINE__);
		exit(1);
	}
	std::vector <std::string> opt_lines;
	std::string line2;
	int i=0;
	while(!file2.eof()){
		std::getline (file2, line2);
		if (line2.size() > 0) {
			int argc;
			std::vector <std::string> argv;
			if (line2.size() > 0 && line2.substr(0,1) != "#") {
				opt_lines.push_back(line2);
				printf("line[%d]= '%s'\n", i++, line2.c_str());
				argv = split_cmd_line(argv0.c_str(), line2.c_str(), &argc);
#if 0
#ifdef _WIN32
				argv = CommandLineToArgvA((PCHAR)line2.c_str(), &argc);
#else
				argv = split_commandline(line2.c_str(), &argc);
#endif
#endif
				argvs.push_back(argv);
				for (uint32_t i=0; i < argv.size(); i++) {
					printf("opts_file: argv[%d]='%s'\n", i, argv[i].c_str());
				}
				printf("argc= %d\n", argc);
			}
		}
	}
	file2.close();
	return 0;
}


int main(int argc, char **argv)
{

	std::vector <std::vector <std::string>> argvs;
	std::string loops_str, adder_str;
	double t_first = dclock();
	unsigned num_cpus = std::thread::hardware_concurrency();
	double spin_tm = 2.0, spin_tm_multi=0.0;
	bool doing_disk = false;
	std::mutex iomutex;
	printf("t_first= %.9f\n", t_first);
#ifdef __linux__
	printf("t_raw= %.9f\n", dclock_vari(CLOCK_MONOTONIC_RAW));
	printf("t_coarse= %.9f\n", dclock_vari(CLOCK_MONOTONIC_COARSE));
	printf("t_boot= %.9f\n", dclock_vari(CLOCK_BOOTTIME));
#endif
	std::cout << "Start Test 1 CPU" << std::endl; // prints !!!Hello World!!!
	double t_start, t_end;
	time_t c_start, c_end;
	unsigned long adder=1, loops = 0xffffff;
	printf("usage: %s tm_secs[,tm_secs_multi] [work_type [ arg3 [ arg4 ]]]\n", argv[0]);
	printf("\twork_type: spin|mem_bw|mem_bw_rdwr|mem_bw_2rd|mem_bw_2rdwr|mem_bw_2rd2wr|disk_rd|disk_wr|disk_rdwr|disk_rd_dir|disk_wr_dir|disk_rdwr_dir\n");
	printf("if mem_bw: arg3 is stride in bytes. arg4 is array size in bytes\n");
	printf("Or first 2 options can be '-f input_file' where each line of input_file is the above cmdline options\n");
	printf("see input_files/haswell_spin_input.txt for example.\n");

	uint32_t wrk_typ = WRK_SPIN;
	std::string work = "spin";

	if (argc >= 2 && std::string(argv[1]) == "-f") {
		read_options_file(argv[0], argv[2], argvs);
	} else {
		std::vector <std::string> av;
		for (int j=0; j < argc; j++) {
			av.push_back(argv[j]);
		}
		argvs.push_back(av);
	}
	for (uint32_t j=0; j < argvs.size(); j++) {
		uint32_t i=1;
		std::string phase;
		if (argvs[j].size() > i) {
			char *cpc;
			cpc = strchr((char *)argvs[j][i].c_str(), ',');
			spin_tm = atof(argvs[j][i].c_str());
			if (cpc) {
				spin_tm_multi = atof(cpc+1);
			} else {
				spin_tm_multi = spin_tm;
			}
			printf("spin_tm single_thread= %f, multi_thread= %f at %s %d\n",
					spin_tm, spin_tm_multi, __FILE__, __LINE__);
		}
		i++; //2
		if (argvs[j].size() > i) {
			work = argvs[j][i];
			wrk_typ = UINT32_M1;
			for (uint32_t j=0; j < wrk_typs.size(); j++) {
				if (work == wrk_typs[j]) {
					wrk_typ = j;
					printf("got work= '%s' at %s %d\n", work.c_str(), __FILE__, __LINE__);
					break;
				}
			}
			if (wrk_typ == UINT32_M1) {
				printf("Error in arg 2. Must be 1 of:\n");
				for (uint32_t j=0; j < wrk_typs.size(); j++) {
					printf("\t%s\n", wrk_typs[j].c_str());
				}
				printf("Bye at %s %d\n", __FILE__, __LINE__);
				exit(1);
			}
			if (wrk_typ >= WRK_DISK_RD && wrk_typ <= WRK_DISK_RDWR_DIR) {
				doing_disk = true;
				loops = 100;
			}
			if (wrk_typ == WRK_MEM_BW || wrk_typ == WRK_MEM_BW_RDWR ||
				wrk_typ == WRK_MEM_BW_2RDWR || wrk_typ == WRK_MEM_BW_2RD ||
				wrk_typ == WRK_MEM_BW_2RD2WR) {
				loops = 64;
				adder = 80*1024*1024;
			}
		}
		i++; //3
		if (argvs[j].size() > i) {
			loops = atoi(argvs[j][i].c_str());
			loops_str = argvs[j][i];
			printf("arg3 is %s\n", loops_str.c_str());
		}
		i++; //4
		if (argvs[j].size() > i) {
			adder = atoi(argvs[j][i].c_str());
			adder_str = argvs[j][i];
			printf("arg4 is %s\n", adder_str.c_str());
		}
		i++; //5
		if (argvs[j].size() > i) {
			phase = argvs[j][i];
			printf("arg5 phase '%s'\n", phase.c_str());
		}

		std::vector<std::thread> threads(num_cpus);
		args.resize(num_cpus);
		for (uint32_t i=0; i < num_cpus; i++) {
			args[i].phase   = phase;
			args[i].spin_tm = spin_tm;
			args[i].id = i;
			args[i].loops = loops;
			args[i].adder = adder;
			args[i].loops_str = loops_str;
			args[i].adder_str = adder_str;
			args[i].work = work;
			args[i].wrk_typ = wrk_typ;
		}
		if (spin_tm > 0.0) {
			if (phase.size() > 0) {
				trace_marker_write("begin phase ST "+phase);
			}
			t_start = dclock();
			dispatch_work(0);
			t_end = dclock();
			if (phase.size() > 0) {
				std::string str = "end phase ST "+phase+", dura= "+std::to_string(args[0].dura)+", "+args[0].units+"= "+std::to_string(args[0].perf);
				trace_marker_write(str);
				printf("%s\n", str.c_str());
			}
		}
		for (unsigned i=0; i < num_cpus; i++) {
			args[i].spin_tm = spin_tm_multi;
		}
		t_start = dclock();
		printf("work= %s\n", work.c_str());

		if (!doing_disk && spin_tm_multi > 0.0) {
			if (phase.size() > 0) {
				trace_marker_write("begin phase MT "+phase);
			}
			for (unsigned i = 0; i < num_cpus; ++i) {
				threads[i] = std::thread([&iomutex, i] {
					dispatch_work(i);
				});
			}

			for (auto& t : threads) {
				t.join();
			}
			double tot = 0.0;
			for (unsigned i = 0; i < num_cpus; ++i) {
				tot += args[i].perf;
			}
			if (phase.size() > 0) {
				std::string str = "end phase MT "+phase+", dura= "+std::to_string(args[0].dura)+", "+args[0].units+"= "+std::to_string(tot);
				trace_marker_write(str);
				printf("%s\n", str.c_str());
			}
		}

		t_end = dclock();
		if (loops < 100 && !doing_disk && spin_tm_multi > 0.0) {
			std::cout << "\nExecution time on " << num_cpus << " CPUs: " << t_end - t_start << " secs" << std::endl;
		}
	}
	if (argc > 20) {
		for (unsigned i=0; i < num_cpus; i++) {
			printf("rezult[%d]= %lu\n", i, args[i].rezult);
		}
	}
	return 0;
}

