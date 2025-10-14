#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

uint64 MAGIC_NUM = 10;
uint MAX_TIME = 12345;
uint64 pids[64];
uint64 results[1024];

uint64 big_calculation() {
    uint64 i, j, sum = 0;
    static uint cnt = 0;
    for (i = 0; i < MAGIC_NUM; i++) {
        for (j = 0; j < i; j++) {
            if (j & 1) {
                sum -= j;
            } else {
                sum += j;
            }
        }
        cnt++;
        sum *= i;
        sum /= (i - j + 1);
    }
    return sum;
}

void loop() {
    int pid = getpid();
    uint last_running_time = 0, last_runnable_time = 0, last_sleep_time = 0;
    uint running_time, runnable_time, sleep_time;
    while (1) {
        results[pid] = big_calculation();
        if (pstate(pid, &running_time, &runnable_time, &sleep_time) < 0) {
            printf("Error: pstate failed for PID %d\n", pid);
            exit(1);
        }
        
        // 时间不能倒流
        if (running_time < last_running_time || runnable_time < last_runnable_time || sleep_time < last_sleep_time) {
            printf("Error: pstate returned invalid times for PID %d\n", pid);
            exit(-1);
        }
        last_running_time = running_time;
        last_runnable_time = runnable_time;
        last_sleep_time = sleep_time;

        if (running_time >= MAX_TIME) {
            printf("PID: %d, Running Time: %d, Runnable Time: %d, Sleep Time: %d\n", pid, running_time, runnable_time, sleep_time);
            exit(0);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: stat <n>\n");
        exit(1);
    }

    int n = atoi(argv[1]);
    if (n <= 0) {
        printf("Error: Invalid number of processes\n");
        exit(1);
    }

    uint start_cpu_time[NCPU];
    cpustate(start_cpu_time);

    int start = uptime();
    for (int i = 0; i < n; i++) {
        int pid = fork();
        if (pid < 0) {
            printf("Error: Fork failed\n");
            exit(1);
        }
        pids[i] = pid;
        if (pid == 0) {
            loop();
        }
    }

    int exitcode;
    int pid = wait(&exitcode, 0);
    if (exitcode != 0) {
        printf("Error: Child process %d exited with code %d\n", pid, exitcode);
        exit(exitcode);
    }

    for (int i = 0; i < n; ++i) {
        kill(pids[i]);
    }

    for (int i = 0; i < n; ++i) {
        if (pids[i] == pid) {
            continue;
        }
        uint running_time, runnable_time, sleep_time;
        if (pstate(pids[i], &running_time, &runnable_time, &sleep_time) < 0) {
            printf("Error: pstate failed for PID %d\n", pids[i]);
            exit(1);
        }
        printf("PID: %d, Running Time: %d, Runnable Time: %d, Sleep Time: %d\n", pids[i], running_time, runnable_time, sleep_time);
    }

    uint end_cpu_time[NCPU];
    uint total_cpu_time = 0;
    cpustate(end_cpu_time);
    for (int i = 0; i < NCPU; ++i) {
        printf("CPU %d: Running Time: %d\n", i, end_cpu_time[i] - start_cpu_time[i]);
        total_cpu_time += end_cpu_time[i] - start_cpu_time[i];
    }
    printf("Total CPU Running Time: %d\n", total_cpu_time);

    int end = uptime();
    printf("Stat Time: %d\n", end - start);

    exit(0);
}