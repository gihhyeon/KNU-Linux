#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h> // fork()와 wait()를 위해 필요
#include <sys/wait.h>  // wait()를 위해 필요

int main(int argc, char *argv[]) {
    int fd;
    caddr_t addr;
    struct stat statbuf;
    pid_t pid; // fork()의 결과를 저장할 변수

    // 1. 프로그램 인자 개수 확인
    if (argc != 2) {
        fprintf(stderr, "Usage : %s filename\n", argv[0]);
        exit(1);
    }

    // 2. 파일 정보 가져오기 (파일 크기 확인)
    if (stat(argv[1], &statbuf) == -1) {
        perror("stat");
        exit(1);
    }

    // 3. 파일 열기
    if ((fd = open(argv[1], O_RDWR)) == -1) {
        perror("open");
        exit(1);
    }

    // 4. 파일을 메모리에 매핑 (공유 모드)
    addr = mmap(NULL, statbuf.st_size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, (off_t)0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    close(fd); // 매핑 후에는 파일 디스크립터가 더 이상 필요 없음

    // 5. 프로세스 복제 (fork)
    switch (pid = fork()) {
        case -1: // fork 실패
            perror("fork");
            exit(1);

        case 0 : /* 자식 프로세스 */
            printf("1. Child Process : addr=%s\n", addr);
            sleep(1);
            addr[0] = 'x'; // 공유 메모리 수정
            printf("2. Child Process : addr=%s\n", addr);
            sleep(2);
            printf("3. Child Process : addr=%s\n", addr);
            exit(0); // 자식 프로세스 종료

        default : /* 부모 프로세스 */
            printf("1. Parent process : addr=%s\n", addr);
            sleep(2);
            printf("2. Parent process : addr=%s\n", addr);
            addr[1] = 'y'; // 공유 메모리 수정
            printf("3. Parent process : addr=%s\n", addr);
            wait(NULL); // 자식 프로세스가 끝날 때까지 기다림
            break;
    }

    // 6. 메모리 매핑 해제
    if (munmap(addr, statbuf.st_size) == -1) {
        perror("munmap");
        exit(1);
    }
    
    return 0;
}
