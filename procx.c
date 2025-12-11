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
#include <semaphore.h> // semaphore fonksiyonları
#include <stdbool.h>
#include <ctype.h>

#define SHM_NAME "/procx_shm"
#define SEM_NAME "/procx_sem"
#define MAX_PROCESSES 50

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
    ProcessInfo processes[MAX_PROCESSES]; // Maksimum 50 process
    int process_count; // Aktif process sayısı
} SharedData;

// Mesaj yapısı
typedef struct {
    long msg_type; // Mesaj tipi
    int command; // Komut (START/TERMINATE)
    pid_t sender_pid; // Gönderen PID
    pid_t target_pid; // Hedef process PID
} Message;

sem_t *procx_sem;
SharedData *shared_memory;

int is_numeric(const char *str) {
    if (str == NULL || *str == '\0') return 0;

    while (*str) {
        if (!isdigit(*str)) {
            return 0;
        }
        str++;
    }
    return 1;
}

int parse_command(char *line, char **argv, int max_args) {
    int argc = 0;
    char *token;
    // newline varsa sil
    line[strcspn(line, "\n")] = '\0';
    token = strtok(line, " ");
    while (token != NULL && argc < max_args) {
        if (strcmp(token, "&") == 0) {
            token = strtok(NULL, " ");
            continue;
        }
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }
    argv[argc] = NULL;
    return argc;
}

void init_shared_memory() {
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
    shared_memory = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
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
}

void init_semephore() {
    procx_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (procx_sem == SEM_FAILED) {
        perror("Semaphore hatası");
        exit(1);
    }
    printf("Semaphore oluşturuldu");
}

void list_processes() {
    sem_wait(procx_sem);

    int count = 0;
    time_t now = time(NULL);

    printf("\n%35s\n", "ÇALIŞAN PROGRAMLAR");
    printf("----------------------------------------------------------------------\n");
    printf("%-8s | %-25s | %-10s | %-8s | %s\n",
           "PID", "Command", "Mode", "Owner", "Süre");
    printf("----------------------------------------------------------------------\n");

    for(int i = 0; i < MAX_PROCESSES; i++) {
        if (shared_memory->processes[i].is_active) {

            long elapsed_seconds = now - shared_memory->processes[i].start_time;
            char *mode_str = (shared_memory->processes[i].mode == DETACHED) ? "Detached" : "Attached";

            printf("%-8d | %-25s | %-10s | %-8d | %ld%s\n",
               shared_memory->processes[i].pid,      // PID
               shared_memory->processes[i].command,  // Command
               mode_str,                          // Mode (Attached/Detached)
               shared_memory->processes[i].owner_pid,// Owner
               elapsed_seconds,                   // Süre (sayı)
               "s"                                // Sürenin sonuna 's' harfi
               );
            count++;
        }
    }
    if (count == 0) {
        printf("Aktif çalışan process bulunamadı.\n");
    }

    printf("----------------------------------------------------------------------\n");
    printf("Toplam: %d process çalışıyor.\n\n", count);

    sem_post(procx_sem);
}

int get_menu() {
    char input[32];
    int selection;

    while(true) {
        printf("\n");
        printf("ProcX v1.0\n");
        printf("------------------------\n");
        printf("1. Yeni Program Çalıştır\n");
        printf("2. Çalışan Programları Listele\n");
        printf("3. Program Sonlandır\n");
        printf("0. Çıkış\n");
        printf("------------------------\n");
        printf("Seçiminiz: ");

        if(fgets(input, sizeof(input), stdin) != NULL) {
            if (input[0] == '\n') continue;
            if (input[0] < '0' || input[0] > '3') {
                printf("Lütfen menüden geçerli bir seçenek (0-3) girin!\n");
                continue;
            }
            selection = atoi(input);
            if (selection >= 0 && selection < 4) {
                return selection;
            }else {
                printf("Lütfen geçerli bir sayı girin!\n");
            }
        } else {
            printf("Lütfen sayı girin!\n");
        }
    }
}

void start_process(char* command, ProcessMode mode) {
    char *argv[20];
    char temp_command[256]; // orjinal command shared memorye yazmak için kopyalandı
    strncpy(temp_command, command, 255);
    int argument_count = parse_command(temp_command, argv, 20);
    int status;
    if (argument_count == 0) {
        printf("Komut bulunamadı!");
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        return;
    }else if (pid == 0) { // child
        if (mode== DETACHED) {
            setsid();
        }
        execvp(argv[0], argv);
        perror("Execvp hatası!");
        exit(1);
    }else { // parent
        sem_wait(procx_sem);

        int index = -1;
        for(int i = 0; i < MAX_PROCESSES; i++) {
            if (shared_memory->processes[i].is_active == 0) {
                index = i;
                break;
            }
        }

        if (index == -1) {
            printf("Hata: Process tablosu dolu (Max 50)!\n");
            kill(pid, SIGTERM); // Yer yoksa oluşturulan çocuğu öldür
            sem_post(procx_sem);
            return;
        }
        shared_memory->processes[index].pid = pid;
        shared_memory->processes[index].owner_pid = getpid();
        strcpy(shared_memory->processes[index].command, command);
        shared_memory->processes[index].is_active = 1;
        shared_memory->processes[index].mode = mode;
        shared_memory->processes[index].start_time = time(NULL);
        shared_memory->processes[index].status = RUNNING;

        sem_post(procx_sem);

        if (mode == ATTACHED) {
            waitpid(pid, &status, 0);
            sem_wait(procx_sem);
            shared_memory->processes[index].status = TERMINATED;
            shared_memory->processes[index].is_active = 0;
            sem_post(procx_sem);
        }
    }
}

void get_process_menu() {
    char komut[64];
    char mode[10];
    ProcessMode process_mode;

    while (true) {
        printf("Çalıştırılacak komutu giriniz: ");
        fgets(komut, sizeof(komut), stdin);
        printf("\nMod Seçin (0: Attached, 1: Detached): ");
        if(fgets(mode, sizeof(mode), stdin) != NULL) {
            if (mode[0] == '\n') continue;
            if (mode[0] != '0' && mode[0] != '1') {
                printf("Lütfen menüden geçerli bir seçenek (0-1) girin!\n");
                continue;
            }
            process_mode = (ProcessMode)atoi(mode);
            start_process(komut, process_mode);
            break;
        }
    }
}
void stop_process(int target_pid) {
    sem_wait(procx_sem);

    int found = 0;
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if (shared_memory->processes[i].is_active) {
            if (shared_memory->processes[i].pid == target_pid) {
                kill(target_pid, SIGTERM);
                shared_memory->processes[i].is_active = 0;
                shared_memory->processes[i].status = TERMINATED;
                shared_memory->process_count--;

                printf("[INFO] Process %d sonlandırıldı ve listeden silindi.\n", target_pid);
                found = 1;
                break;

            }
        }
    }
    if (!found) {
        printf("[UYARI] PID %d listede bulunamadı!\n", target_pid);
    }
    sem_post(procx_sem);
}


void get_stop_menu() {
    char c_pid[10];
    int pid;

    while (true) {
        printf("Sonlandırılacak process PID: ");
        if(fgets(&c_pid, sizeof(c_pid), stdin) != NULL) {
            if(!is_numeric(c_pid)){
            printf("Lütfen menüden geçerli bir seçenek (0-1) girin!\n");
            continue;
            }
            pid = atoi(c_pid);
            stop_process(pid);
        }
    }
}




int main(int argc, char *argv[], char **envp) {

    shm_unlink(SHM_NAME); // Geçici (her başlangıçta baştan oluşturmak için)
    sem_unlink(SEM_NAME); // Geçici (daha sonra bir clean fonksiyonu yazılacak)

    init_shared_memory();
    init_semephore();
    while (true) {
        int choice = get_menu();
        switch (choice) {
        case 0:
            // Programdan çık
            printf("ProcX Kapatılıyor...\n");
            return 0;
        case 1:
            // Process Başlat
            get_process_menu();
            break;
        case 2:
            list_processes();
            break;
        case 3:
            // programı sonlandır
            get_stop_menu();
            break;

        }

    }
    return 0;
}
