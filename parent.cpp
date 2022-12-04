#include <sys/types.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/time.h>

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include <bits/stdc++.h>

#define SEC 1000000
#define TO_CHILD 1
#define TO_PARENT 2

using namespace std;

typedef struct
{
    long type;
    char text[50];
} MsgBuf;

int msg_id;
key_t key = 1234;
deque<pair<pid_t, int>> wait_queue;
deque<tuple<pid_t, int, int>> run_queue;
map<pid_t, pair<int, int>> mp;
struct itimerval ival;
bool iteration;
int cnt = 0, time_slice;

void io_wait()
{
    for (auto &i : wait_queue)
    {
        i = {i.first, i.second - time_slice};
    }
}

void io_compeltion_interrupt()
{
    int n = wait_queue.size();
    for (int i = 0; i < n; i++)
    {
        pid_t front_pid;
        int front_io_burst;
        tie(front_pid, front_io_burst) = wait_queue.front();
        wait_queue.pop_front();

        // I/O compeltion 인터럽트 발생, 해당 프로세스는 이제 ready state임. run queue로 이동.
        if (front_io_burst <= 0)
        {
            run_queue.push_back(make_tuple(front_pid, mp[front_pid].first, mp[front_pid].second));
        }
        else
        {
            wait_queue.push_back({front_pid, front_io_burst});
        }
    }
}

// run queue에서 process를 가져온다. (dispatch)
void dispatch()
{
    // run queue에 아무것도 업을 때는 쉰다.
    if (run_queue.empty())
    {
        return;
    }

    // run queue에 하나라도 있다면
    MsgBuf msg_snd, msg_rcv;

    msg_snd.type = TO_CHILD;
    sprintf(msg_snd.text, "%d", (int)time_slice);
    if (msgsnd(msg_id, (void *)&msg_snd, 50, IPC_NOWAIT) == -1)
    {
        perror("msgsnd 실패\n");
        exit(1);
    }

    // msg를 받을 자격이 있는 프로세스를 run queue에서 빼온다.
    pid_t dispatched_proc = get<0>(run_queue.front());
    run_queue.pop_front();
    //메세지 알람 전송
    kill(dispatched_proc, SIGUSR1);

    //바로 답장 올 거임
    // msg가 올 때까지 기다렸다. 수신

    msgrcv(msg_id, &msg_rcv, 50, TO_PARENT, 0);

    int reply = atoi(msg_rcv.text);
    // 만약 답장이 음수면, 그건 남은 cpu burst이다.
    if (reply < 0)
    {
        //그냥 계속 실행인 경우
        run_queue.push_back(make_tuple(dispatched_proc, -1 * reply, mp[dispatched_proc].second));
    }

    // 만약 답장이 양수면, 그건 남은 i/o burst이다.
    else
    {
        // I/O wait 인터럽트
        int proc_io_burst = reply;
        wait_queue.push_back({dispatched_proc, proc_io_burst});
    }
}

double us_to_s(int us)
{
    double ret;
    ret = (double)us / 1000000;
    return ret;
}

void print_schedule_status()
{
    printf("\nWait Queue: ");
    for (const auto &i : wait_queue)
    {
        if (i == wait_queue.back())
        {
            printf("[PID: %d] - [남은 I/O Burst: %dus(%.2lfs)]", i.first, i.second, us_to_s(i.second));
        }
        else
        {
            printf("[PID: %d] - [남은 I/O Burst: %dus(%.2lfs)], ", i.first, i.second, us_to_s(i.second));
        }
    }
    printf("\n\n");
    printf("Run Queue: ");
    for (const auto &i : run_queue)
    {
        pid_t run_pid;
        int run_cpu_burst, run_io_burst;
        tie(run_pid, run_cpu_burst, run_io_burst) = i;

        if (i == run_queue.back())
        {
            printf("[PID: %d] - [남은 CPU Burst: %dus(%.2lfs)] | [남은 I/O Burst: %dus(%.2lfs)]", run_pid, run_cpu_burst, us_to_s(run_cpu_burst), run_io_burst, us_to_s(run_io_burst));
        }
        else
        {
            printf("[PID: %d] - [남은 CPU Burst: %dus(%.2lfs)] | [남은 I/O Burst: %dus(%.2lfs)], ", run_pid, run_cpu_burst, us_to_s(run_cpu_burst), run_io_burst, us_to_s(run_io_burst));
        }
    }
    printf("\n\n");
    if (!run_queue.empty())
    {
        printf("[PID: %d] process가 %d번째 time tick에서 CPU time 획득 후, [남은 CPU Burst: %dus(%.2lfs)] | [남은 I/O Burst: %dus(%.2lfs)]\n", get<0>(run_queue.back()), cnt, get<1>(run_queue.back()), us_to_s(get<1>(run_queue.back())), get<2>(run_queue.back()), us_to_s(get<2>(run_queue.back())));
    }
}

void kill_children()
{
    for (auto &i : mp)
    {
        kill(i.first, SIGKILL);
    }
}

void check_exit_conditioin()
{
    if (cnt == 10001)
    {
        //자식 프로세스를 종료시킨다.
        kill_children();
        //메세지 큐를 삭제한다.
        msgctl(msg_id, IPC_RMID, 0);
        printf("10000 번의 time tick이 지났습니다. 스케줄러를 종료합니다.\n");
        exit(0);
    }
}

static void schedule(int sig)
{
    // schedule_dump 기록을 위한 코드, 무한 반복 스케쥴링을 위해서 주석처리
    check_exit_conditioin();
    printf("---------------\n");
    printf("%d번째 time tick (time slice는 10000us(0.01s))\n", cnt);

    if (!run_queue.empty())
    {
        printf("[PID: %d] process가 %d번째 time tick에서 CPU time 획득\n", get<0>(run_queue.front()), cnt);
    }
    else
    {
        printf("어떤 프로세스도 %d번째 time tick에서 CPU time 획득하지 못함\n", cnt);
    }

    // 1. I/O wait한다.

    io_wait();

    // 2. I/O compeltion interrupt가 발생하면, wait state -> ready state로 바꾸고, run queue에 해당 프로세스를 넣는다.
    io_compeltion_interrupt();

    // 3. run 큐에서 ready state인 프로세스 하나 pop해와서 실행(dispatch), I/O wait 인터럽트가 들어오면 wait_queue에 push
    dispatch();

    print_schedule_status();
    printf("---------------\n");

    cnt++;
}

static void exit_scheduler(int sig)
{
    kill_children();
    msgctl(msg_id, IPC_RMID, 0);
    printf("\nCtrl + C 입력. 스케줄러를 종료합니다.\n");
    exit(0);
}

int main(int argc, char *argv[])
{
    pid_t pid, child_pid, parent_pid = getpid();
    int status;
    char cpu_burst_buf[50], io_burst_buf[50], ppid_buf[50];

    srand(time(NULL));

    ival.it_interval.tv_sec = 0;
    // 1초마다 알람이 울림
    time_slice = ival.it_interval.tv_usec = 10000;

    ival.it_value.tv_sec = 1;
    ival.it_value.tv_usec = 0;

    // message queue 등록
    msg_id = msgget(key, IPC_CREAT | 0644);
    if (msg_id < 0)
    {
        perror("msgget 실패\n");
        exit(1);
    }

    // singal 처리 callback 함수 등록
    signal(SIGALRM, schedule);
    signal(SIGINT, exit_scheduler);
    printf("Simple Scheduler 프로그램\n");
    printf("[PID: %d] parent process(Simple Scheduler)입니다. 1초 후, 스케쥴링이 시작됩니다.\n", parent_pid);
    printf("\n\n");
    for (int i = 0; i < 10; i++)
    {
        // 3초 ~ 4초
        int random_cpu_burst = (random() % 2) * SEC + 1 * SEC, random_io_burst = (random() % 2) * SEC + 1 * SEC;

        sprintf(cpu_burst_buf, "%d", random_cpu_burst);
        sprintf(io_burst_buf, "%d", random_io_burst);
        sprintf(ppid_buf, "%d", parent_pid);

        pid = fork();
        if (pid > 0)
        {
            //자식 프로세스 생성과 동시에 wait queue에 push
            mp.insert({pid, {random_cpu_burst, random_io_burst}});
            run_queue.push_back(make_tuple(pid, random_cpu_burst, random_io_burst));
        }
        else if (pid == 0)
        {
            execl("./child", "./child", (const char *)cpu_burst_buf, (const char *)io_burst_buf, (const char *)ppid_buf, NULL);
            perror("execl 실행 실패\n");
        }
        else if (pid < 0)
        {
            printf("fork 실패!\n");
            exit(1);
        }
    }

    // timer 등록, 시간이 흐르기 시작함.
    // 초기 알람이 울리는 조건 0초가 흐른후
    // 그 이후 부터는 1초마다 알람이 울림
    setitimer(ITIMER_REAL, &ival, NULL);
    //타이머 설정하고 무한 루프
    while (true)
        ;
    return 0;
}