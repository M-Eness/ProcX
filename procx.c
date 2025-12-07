//
// Created by Mehmet Enes on 2.12.2025.
//

#include <stdio.h>      // printf, perror vs.
#include <stdlib.h>     // exit, malloc, free
#include <string.h>
#include <signal.h>     // pid_t, kill, sinyal tipleri
#include <unistd.h>     // fork, getpid, exec, pid_t
#include <sys/types.h>  // pid_t, key_t, mode_t
#include <time.h>       // time_t, time(), ctime()
#include <fcntl.h>      // O_CREAT, O_RDWR
#include <sys/mman.h>   // shm_open, mmap

#define SHM_NAME "/procx_shm"


// Process bilgisi
typedef enum {
    ATTACHED = 0,
    DETACHED = 1
} ProcessMode;

typedef enum {
    RUNNING = 0,
    TERMINATED = 1
} ProcessStatus;

typedef struct {
    pid_t pid; // Process ID
    pid_t owner_pid; // Başlatan instance'ın PID'si
    char command[256]; // Çalıştırılan komut
    ProcessMode mode; // Attached (0) veya Detached (1)
    ProcessStatus status; // Running (0) veya Terminated (1)
    time_t start_time; // Başlangıç zamanı
    int is_active; // Aktif mi? (1: Evet, 0: Hayır)
} ProcessInfo;

// Paylaşılan bellek yapısı
typedef struct {
    ProcessInfo processes[50]; // Maksimum 50 process
    int process_count; // Aktif process sayısı
} SharedData;

// Mesaj yapısı
typedef struct {
    long msg_type; // Mesaj tipi
    int command; // Komut (START/TERMINATE)
    pid_t sender_pid; // Gönderen PID
    pid_t target_pid; // Hedef process PID
} Message;

int parse_command(char *line, char **argv, int max_args, int *detached) {
    int argc = 0;
    char *token;
    // newline varsa sil
    line[strcspn(line, "\n")] = '\0';
    *detached = 0;
    token = strtok(line, " ");
    while (token != NULL && argc < max_args) {
        if (strcmp(token, "&") == 0) {
            *detached = 1;
            token = strtok(NULL, " ");
            continue;
        }
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }
    argv[argc] = NULL;
    return argc;
}

SharedData* init_shared_memory() {
    // Shared Memory
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    int is_first = 0;

    if (shm_fd == -1) {
        shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if(shm_fd == -1) {
            perror("shm_open hatası");
            exit(1);
        }
        is_first = 1;
    }

    // Boyutu düzenle
    if(is_first) {
        if(ftruncate(shm_fd, sizeof(SharedData)) == -1) {
            perror("ftruncate hatası");
            close(shm_fd);
            exit(1);
        }
    }

    // Mapping işlemi
    SharedData *shared_memory = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shared_memory == MAP_FAILED) {
        perror("mmap hatası");
        close(shm_fd);
        exit(1);
    }

    if(is_first) {
        shared_memory->process_count = 0;
        printf("İlk process oluşturuldu\n");
    }else {
        printf("Shared Memorye dahil olundu\n");
    }

    printf("Mevcut process sayısı: %d\n", shared_memory->process_count);
    close(shm_fd);
    return shared_memory;
}

int main(int argc, char *argv[], char **envp) {

    SharedData* shared_memory = init_shared_memory();
    shm_unlink(SHM_NAME); // Geçici (her başlangıçta baştan oluşturmak için)


    return 0;
}
