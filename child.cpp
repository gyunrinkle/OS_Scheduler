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

#include <queue>

#define TO_CHILD 1
#define TO_PARENT 2

typedef struct
{
    long type;
    char text[50];
} MsgBuf;

bool is_io = false;
int msg_id, cpu_burst, io_burst;
int remain_cpu_burst;
key_t key = 1234;
pid_t parent_pid;

static void msg_alert(int sig)
{
    MsgBuf msg_rcv, msg_snd;

    // msg가 올 때까지 기다렸다. 수신
    msgrcv(msg_id, &msg_rcv, 50, TO_CHILD, 0);

    remain_cpu_burst -= atoi(msg_rcv.text);

    msg_snd.type = TO_PARENT;

    // I/O wait 인터럽트 발생
    if (remain_cpu_burst <= 0) // io로 넘어감
    {

        sprintf(msg_snd.text, "%d", io_burst);
        if (msgsnd(msg_id, (void *)&msg_snd, 50, IPC_NOWAIT) == -1)
        {
            perror("msgsnd 실패\n");
            exit(1);
        }
        remain_cpu_burst = cpu_burst;
    }
    // 계속 ready state 유지
    else
    {
        sprintf(msg_snd.text, "%d", -1 * remain_cpu_burst);
        if (msgsnd(msg_id, (void *)&msg_snd, 50, IPC_NOWAIT) == -1)
        {
            perror("msgsnd 실패\n");
            exit(1);
        }
    }
}

static void sigkill_handler(int sig)
{
    //메세지 큐를 삭제한다.
    msgctl(msg_id, IPC_RMID, 0);
    exit(0);
}

int main(int argc, char *argv[])
{
    // struct itimerval ival;
    remain_cpu_burst = cpu_burst = atoi(argv[1]);
    io_burst = atoi(argv[2]);
    parent_pid = atoi(argv[3]);

    msg_id = msgget(key, IPC_CREAT | 0644);
    if (msg_id < 0)
    {
        perror("msgget");
        exit(1);
    }
    signal(SIGUSR1, msg_alert);
    signal(SIGKILL, sigkill_handler);
    while (true)
        ;
    return 0;
}