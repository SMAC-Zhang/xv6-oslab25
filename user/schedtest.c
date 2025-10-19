#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "user/user.h"

uint64 MAGIC_NUM = 10;
uint MAX_TIME = 12345;
uint64 pids[64];
uint64 results[1024];
int fds[64];

int n;
int test_type;
int fd;
int mypid;
int nice;

uint64 big_calculation() {
    uint64 i, j, sum = 0;
    for (i = 0; i < MAGIC_NUM; i++) {
        for (j = 0; j < i; j++) {
            if (j & 1) {
                sum -= j;
            } else {
                sum += j;
            }
        }
        sum *= i;
        sum /= (i - j + 1);
    }
    return sum;
}

// 测试1：所有进程运行时间和优先级比例相同
void test1() {
    uint running_time, runnable_time, sleep_time;
    while (1) {
        results[mypid] = big_calculation();
        if (pstate(mypid, &running_time, &runnable_time, &sleep_time) < 0) {
            printf("Error: pstate failed for PID %d\n", mypid);
            exit(1);
        }
        if (running_time >= MAX_TIME) {
            fprintf(fd, "PID: %d, nice: %d, Running Time: %d, Runnable Time: %d, Sleep Time: %d\n", mypid, nice, running_time, runnable_time, sleep_time);
            close(fd);
            exit(0);
        }
    }
}

// 测试2：优先级高的进程睡一会再起来
void test2() {
    uint running_time, runnable_time, sleep_time;
    uint last_up_time = uptime(), up_time = 0;
    static int sleeped = 0;
    while (1) {
        results[mypid] = big_calculation();
        if (pstate(mypid, &running_time, &runnable_time, &sleep_time) < 0) {
            printf("Error: pstate failed for PID %d\n", mypid);
            exit(1);
        }
        if (mypid % 2 == 0 && sleeped == 0) {
            sleep(1234);
            sleeped = 1;
        }
        up_time = uptime();
        if (mypid % 2 != 0) {
            if (up_time - last_up_time >= 50) {
                printf("Error: PID: %d is so hungry! interval: %d\n", mypid, up_time - last_up_time);
                exit(-1);
            }
        }
        last_up_time = up_time;
        if (running_time >= MAX_TIME) {
            fprintf(fd, "PID: %d, nice: %d, Running Time: %d, Runnable Time: %d, Sleep Time: %d\n", mypid, nice, running_time, runnable_time, sleep_time);
            close(fd);
            exit(0);
        }
    }
}

// 测试3：在运行到一半时突然加入三个进程
void test3() {
    uint running_time, runnable_time, sleep_time;
    uint last_up_time = uptime(), up_time = 0;
    while (1) {
        results[mypid] = big_calculation();
        if (pstate(mypid, &running_time, &runnable_time, &sleep_time) < 0) {
            printf("Error: pstate failed for PID %d\n", mypid);
            exit(1);
        }
        up_time = uptime();
        if (up_time - last_up_time >= 50) {
            printf("Error: PID: %d is so hungry! interval: %d\n", mypid, up_time - last_up_time);
            exit(-1);
        }
        last_up_time = up_time;
        if (running_time >= MAX_TIME) {
            fprintf(fd, "PID: %d, nice: %d, Running Time: %d, Runnable Time: %d, Sleep Time: %d\n", mypid, nice, running_time, runnable_time, sleep_time);
            close(fd);
            exit(0);
        }
    }
}

void cpubalance() {
    static uint end_cpu_time[NCPU];
    static uint start_cpu_time[NCPU];
    static uint start_system_time;
    static int ok = 0;
    if (!ok) {
        cpustate(start_cpu_time);
        start_system_time = uptime();
        ok = 1;
        return;
    }

    cpustate(end_cpu_time);
    fd = open("cpu.txt", O_CREATE | O_RDWR);
    for (int i = 0; i < NCPU; ++i) {
        fprintf(fd, "CPU %d: Running Time: %d\n", i, end_cpu_time[i] - start_cpu_time[i]);
    }
    fprintf(fd, "System Time: %d\n", uptime() - start_system_time);
    close(fd);
}

int create_fd(int idx) {
    char filename[32] = {0};
    itoa(idx, filename);
    for (int j = 0; j < 32; ++j) {
        if (filename[j] == 0) {
            strcpy(filename + j, ".txt");
            break;
        }
    }
    int local_fd = open(filename, O_CREATE | O_RDWR);
    if (local_fd < 0) {
        printf("Error: open %s failed\n", filename);
        exit(1);
    }
    return local_fd;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: stat <n> <test-type>\n");
        exit(1);
    }

    int n, test_type = 0;
    n = atoi(argv[1]);
    test_type = atoi(argv[2]);

    if (n <= 0 || n > 32 || test_type <= 0 || test_type > 3) {
        printf("Error: Invalid parameters\n");
        exit(1);
    }

    void(*test)() = 0;
    switch (test_type) {
        case 1:
            test = test1;
            break;
        case 2:
            test = test2;
            break;
        case 3:
            test = test3;
            break;
        default:
            printf("Error: Invalid test type\n");
            exit(1);
    }

    // 为避免输出冲突，父进程进程i的信息写进{i}p.txt
    for (int i = 0; i < n; ++i) {
        char filename[32] = {0};
        itoa(i, filename);
        for (int j = 0; j < 32; ++j) {
            if (filename[j] == 0) {
                strcpy(filename + j, "p.txt");
                break;
            }
        }
        fds[i] = open(filename, O_CREATE | O_RDWR);
        if (fd < 0) {
            printf("Error: open %s failed\n", filename);
            exit(1);
        }
    }

    cpubalance();
    for (int i = 0; i < n; i++) {
        int pid = fork();
        if (pid < 0) {
            printf("Error: Fork failed\n");
            exit(1);
        }

        if (test_type == 3) {
            static int sleeped = 0;
            if (n - i <= 3) {
                if (!sleeped) {
                    sleeped = 1;
                    sleep(1234);
                }
                nice = 1;
            } else {
                nice = 3;
            }
        }

        pids[i] = pid;
        if (pid == 0) {
            fd = create_fd(i); // 为避免输出冲突，子进程将自己的信息写进{i}.txt
            mypid = getpid();
            if (test_type == 1) {
                nice = mypid % 3 + 1; // 1, 2, 3
            } else if (test_type == 2) {
                if (mypid % 2 == 0) {
                    nice = 1;
                } else {
                    nice = 3;
                }
            }
            setnice(nice);
            test();
        }
    }

    // 对于测试2，3，父进程等待所有子进程退出，以检查是否有子进程被饿死
    if (test_type > 1) {
        for (int i = 0; i < n; ++i) {
            int exitcode;
            int pid = wait(&exitcode, 0);
            if (exitcode != 0) {
                printf("Error: Child process %d exited with code %d\n", pid, exitcode);
                exit(exitcode);
            }
        }
        cpubalance();
        exit(0);
    }

    // 对于测试1，父进程等待某个子进程退出，然后杀死其他子进程
    int exitcode;
    int pid = wait(&exitcode, 0);
    if (exitcode != 0) {
        printf("Error: Child process %d exited with code %d\n", pid, exitcode);
        exit(exitcode);
    }
    for (int i = 0; i < n; ++i) {
        kill(pids[i]);
    }
    // ensure all children have been killed
    sleep(100);
    cpubalance();

    // 父进程输出所有子进程的状态
    for (int i = 0; i < n; ++i) {
        if (pids[i] == pid) {
            close(fds[i]);
            continue;
        }
        uint running_time, runnable_time, sleep_time;
        if (pstate(pids[i], &running_time, &runnable_time, &sleep_time) < 0) {
            printf("Error: pstate failed for PID %d\n", pids[i]);
            exit(1);
        }
        fprintf(fds[i], "PID: %d, nice: %d, Running Time: %d, Runnable Time: %d, Sleep Time: %d\n", pids[i], pids[i] % 3 + 1, running_time, runnable_time, sleep_time);
        close(fds[i]);
    }

    char buf[512];
    for (int i = 0; i < n; i++) {
        int local_fd = create_fd(i);
        read(local_fd, buf, 256);
        fprintf(local_fd, "\n");
        close(local_fd);
    }

    exit(0);
}