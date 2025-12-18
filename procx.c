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
#include <pthread.h>
#include <sys/errno.h>
#include <sys/msg.h>

#define SHM_NAME "/procx_shm_v7"
#define SEM_NAME "/procx_sem_v7"
#define MQ_NAME "procx_mq_v7"
#define MAX_PROCESSES 50
#define MAX_TERMINALS 2

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
    pid_t active_terminals[MAX_TERMINALS];
    int terminal_count;
} SharedData;

// Mesaj yapısı
typedef struct {
    long msg_type; // Mesaj tipi
    int command; // Komut (START/TERMINATE)
    pid_t sender_pid; // Gönderen PID
    pid_t target_pid; // Hedef process PID
} Message;

int msg_queue_id;
sem_t* procx_sem;
SharedData* shared_memory;
volatile sig_atomic_t exit_requested = 0;
pthread_t thread_id_monitor;
pthread_t thread_id_ipc;
void shutdown_system(void);

int is_numeric(const char* str) {
    if (str == NULL || *str == '\0') return 0;

    while (*str) {
        if (!isdigit(*str)) {
            return 0;
        }
        str++;
    }
    return 1;
}

int parse_command(char* line, char** argv, int max_args) {
    int argc = 0;
    char* token;
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
        if (shm_fd == -1) {
            perror("shm_open hatası");
            exit(1);
        }
        is_first = 1;
    }

    // Boyutu düzenle
    if (is_first) {
        if (ftruncate(shm_fd, sizeof(SharedData)) == -1) {
            perror("ftruncate hatası");
            close(shm_fd);
            exit(1);
        }
    }

    // Mapping işlemi
    shared_memory = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_memory == MAP_FAILED) {
        perror("mmap hatası");
        close(shm_fd);
        exit(1);
    }

    if (is_first) {
        memset(shared_memory, 0, sizeof(SharedData)); // Belleği temizle
        shared_memory->process_count = 0;
        printf("İlk process oluşturuldu\n");
    }
    else {
        printf("Shared Memorye dahil olundu\n");
    }

    printf("Mevcut process sayısı: %d\n", shared_memory->process_count);
    close(shm_fd);
}

void init_semaphore() {
    procx_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (procx_sem == SEM_FAILED) {
        perror("Semaphore hatası");
        exit(1);
    }
    printf("Semaphore oluşturuldu");
}

void init_message_queue() {
    int fd = open(MQ_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("ftok dosyası oluşturulamadı");
        exit(1);
    }
    close(fd);
    key_t key = ftok(MQ_NAME, 65);

    msg_queue_id = msgget(key, 0666 | IPC_CREAT);
    if (msg_queue_id == -1) {
        perror("Message Queue oluşturulamadı");
        exit(1);
    }
}

void register_terminal() {
    sem_wait(procx_sem);

    int registered = 0;

    for (int i = 0; i < MAX_TERMINALS; i++) {

        if (shared_memory->active_terminals[i] == 0 ) {
            shared_memory->active_terminals[i] = getpid();
            shared_memory->terminal_count++;
            registered = 1;
            printf("\n[SİSTEM] Terminal %d olarak kaydedildi.\n", i + 1);
            break;
        }
        if (shared_memory->active_terminals[i] == getpid()) {
            registered = 1;
            printf("\n[SİSTEM] Terminal tekrar bağlandı.\n");
            break;
        }
    }

    if (!registered) {
        printf("\n[HATA] Terminal sınırı (2) dolu! Program başlatılamıyor.\n");
        sem_post(procx_sem);
        exit(1); // 3. terminal giremez
    }

    sem_post(procx_sem);
}

int remove_terminal() {
    if (shared_memory == NULL) return -1;
    int terminal_count = 0;

    sem_wait(procx_sem);
    pid_t my_pid = getpid();

    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (shared_memory->active_terminals[i] == my_pid) {
            shared_memory->active_terminals[i] = 0;
            shared_memory->terminal_count--;
            terminal_count = shared_memory->terminal_count;
            break;
        }
    }
    sem_post(procx_sem);
    return terminal_count;
}

void send_message(int command, pid_t target) {
    Message msg;
    msg.command = command;
    msg.sender_pid = getpid();
    msg.target_pid = target;

    sem_wait(procx_sem);

    for (int i = 0; i < MAX_TERMINALS; i++) {
        pid_t dest_pid = shared_memory->active_terminals[i];

        // index doluysa VE o indexte ben yoksam
        if (dest_pid != 0 && dest_pid != getpid()) {
            msg.msg_type = dest_pid; // Adrese teslim
            if(msgsnd(msg_queue_id, &msg, sizeof(Message) - sizeof(long), IPC_NOWAIT) == -1) {
                if (errno == EAGAIN) {
                    perror("\n[INFO] Mesaj kuyruğu dolu. Mesaj gönderilemedi!");
                }
            }
        }
    }
    sem_post(procx_sem);
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

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (shared_memory->processes[i].is_active) {
            long elapsed_seconds = now - shared_memory->processes[i].start_time;
            char* mode_str = (shared_memory->processes[i].mode == DETACHED) ? "Detached" : "Attached";

            printf("%-8d | %-25s | %-10s | %-8d | %ld%s\n",
                   shared_memory->processes[i].pid, // PID
                   shared_memory->processes[i].command, // Command
                   mode_str, // Mode (Attached/Detached)
                   shared_memory->processes[i].owner_pid, // Owner
                   elapsed_seconds, // Süre (sayı)
                   "s" // Sürenin sonuna 's' harfi
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

    while (true) {
        if (exit_requested) {
            shutdown_system();
        }
        printf("\n");
        printf("ProcX v1.0\n");
        printf("------------------------\n");
        printf("1. Yeni Program Çalıştır\n");
        printf("2. Çalışan Programları Listele\n");
        printf("3. Program Sonlandır\n");
        printf("0. Çıkış\n");
        printf("------------------------\n");
        printf("Seçiminiz: ");

        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (exit_requested) {
                shutdown_system(); // Beklemeden çıkış yap
            }
            continue;
        }
        if (input[0] == '\n') continue;
        if (input[0] < '0' || input[0] > '3') {
            printf("Lütfen menüden geçerli bir seçenek (0-3) girin!\n");
            continue;
        }
        selection = atoi(input);
        if (selection >= 0 && selection < 4) {
            return selection;
        }
        else {
            printf("Lütfen geçerli sayı girin!\n");
        }
    }
}

void start_process(char* command, ProcessMode mode) {
    char* argv[20];
    char temp_command[256]; // orjinal command shared memorye yazmak için kopyalandı

    command[strcspn(command, "\n")] = '\0';
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
    }
    else if (pid == 0) { // child
        if (mode == DETACHED) {
            setsid();
        }
        execvp(argv[0], argv);
        perror("Execvp hatası!");
        exit(1);
    }
    else { // parent
        printf("\n[SUCCESS] Process başlatıldı: PID: %d\n", pid);
        sem_wait(procx_sem);

        int index = -1;
        for (int i = 0; i < MAX_PROCESSES; i++) {
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
        shared_memory->process_count++;


        sem_post(procx_sem);

        send_message(1, pid);

        if (mode == ATTACHED) {
            waitpid(pid, &status, 0);
            sem_wait(procx_sem);
            shared_memory->processes[index].status = TERMINATED;
            shared_memory->processes[index].is_active = 0;
            shared_memory->process_count--;
            sem_post(procx_sem);
            printf("\n[INFO] Attached process sonlandı: PID: %d\n", pid);
            send_message(2, pid);
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
        if (fgets(mode, sizeof(mode), stdin) != NULL) {
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
    int found = 0;

    sem_wait(procx_sem);
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (shared_memory->processes[i].is_active) {
            if (shared_memory->processes[i].pid == target_pid) {
                kill(target_pid, SIGTERM);
                printf("[INFO] Process %d sonlandırıldı ve listeden silindi.\n", target_pid);
                found = 1;
                sem_post(procx_sem);
                break;
            }
        }
    }
    if (!found) {
        sem_post(procx_sem);
        printf("[UYARI] PID %d listede bulunamadı!\n", target_pid);

    }
}

void get_stop_menu() {
    char c_pid[10];
    int pid;

    while (true) {
        printf("Sonlandırılacak process PID (Çıkış için exit yazın): ");
        if (fgets(c_pid, sizeof(c_pid), stdin) != NULL) {
            c_pid[strcspn(c_pid, "\n")] = '\0';
            if (strcmp(c_pid, "exit") == 0) {
                return;
            }
            if (!is_numeric(c_pid)) {
                printf("Lütfen geçerli bir sayı girin!\n");
                continue;
            }
            pid = atoi(c_pid);
            stop_process(pid);
            return;
        }
    }
}

void clean_resources() { // Bu fonksiyon güncellenecek
    int terminal_count = remove_terminal();
    if (shared_memory != NULL) {
        munmap(shared_memory, sizeof(SharedData));
        printf("[INFO] Shared Memory bağlantısı kesildi.\n");
    }

    if (procx_sem != NULL) {
        sem_close(procx_sem);
        printf("[INFO] Semaphore bağlantısı kesildi.\n");
    }

    if (terminal_count < 1) { // Son terminalse
        if (msg_queue_id != -1) {
            if (msgctl(msg_queue_id, IPC_RMID, NULL) == -1) { // IPC_RMID: Kuyruğu sistemden tamamen kaldırır
                perror("[WARN] Message Queue silinemedi");
            }
            else {
                printf("[INFO] Message Queue sistemden silindi.\n");
            }
        }
        // Sistem tamamen kapanır
        shm_unlink(SHM_NAME);
        sem_unlink(SEM_NAME);
        remove(MQ_NAME);
        printf("[INFO] Kaynaklar (SHM, SEM, MQ) sistemden silindi.\n");
    }
}

void shutdown_system() {
    printf("\n\n[SİSTEM] Kapatma sinyali algılandı. Çıkış yapılıyor...\n");

    exit_requested = 1;

    // Kendi kendime boş bir mesaj atıyorum ki msgrcv kilidi açılsın.
    Message poison_pill;
    poison_pill.msg_type = getpid(); // Kendi PID'm
    poison_pill.sender_pid = getpid();
    poison_pill.command = 99;

    msgsnd(msg_queue_id, &poison_pill, sizeof(Message) - sizeof(long), IPC_NOWAIT);

    // Terminale bağlı çocukları öldür
    if (shared_memory != NULL && procx_sem != NULL) {
        sem_wait(procx_sem);
        pid_t my_pid = getpid();

        for (int i = 0; i < MAX_PROCESSES; i++) {
            // Process aktifse ve sahibi bensem
            if (shared_memory->processes[i].is_active &&
                shared_memory->processes[i].owner_pid == my_pid) {
                if (shared_memory->processes[i].mode == ATTACHED) {
                    // Process'i işletim sistemi seviyesinde öldür
                    kill(shared_memory->processes[i].pid, SIGTERM);
                    // Her durumda Shared Memory listesinden düşüyoruz çünkü ProcX kapanıyor.
                    shared_memory->processes[i].is_active = 0;
                    shared_memory->processes[i].status = TERMINATED;
                    shared_memory->process_count--;
                    printf("[TEMİZLİK] Kapatılırken attached process sonlandırıldı: %d\n",
                           shared_memory->processes[i].pid);
                }
                else { // Detach processler
                    // Çalışmaya devam eder ama artık procx yönetiminde olmaz??
                    printf("[INFO] Detached process arka planda bırakıldı: %d\n", shared_memory->processes[i].pid);
                    shared_memory->processes[i].owner_pid = -1; // artık sahibi ben değilim
                }
            }
        }
        sem_post(procx_sem);
    }
    printf("[SİSTEM] Threadlerin kapanması bekleniyor...\n");
    pthread_join(thread_id_monitor, NULL);
    pthread_join(thread_id_ipc, NULL);

    clean_resources();
    exit(0);
}

void handle_sigint(int sig) {
    // Çıkış flagı
    exit_requested = 1;
}

void* monitor_thread(void* arg) {
    int status;
    while (!exit_requested) {
        sleep(2);
        if (exit_requested) break;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            int found = 0;
            pid_t check_pid = -1;
            pid_t owner_pid = -1;
            int is_active = 0;
            sem_wait(procx_sem);
            if (shared_memory->processes[i].is_active) {
                is_active = 1;
                owner_pid = shared_memory->processes[i].owner_pid;
                check_pid = shared_memory->processes[i].pid;
            }
            sem_post(procx_sem);

            if (!is_active) {
                continue;
            }


            if (owner_pid == getpid()) { // Çocuğun parentı ben miyim?
                if (waitpid(check_pid, &status, WNOHANG) == check_pid) { // Çocuk ölmüş mü?
                    found = 1;
                }
            }
            else { // Çocuğun parentı ben değilsem
                // kill sinyali gönderilemedi ve bunun nedeni pıd bulunaması (process ölmüş)
                if (kill(check_pid, 0) == -1 && errno == ESRCH) {
                    found = 1;
                }
            }
            if (found) {
                int cleaned = 0;
                sem_wait(procx_sem);
                // Kilidi alana kadar değişiklik oldu mu?
                if (shared_memory->processes[i].is_active && shared_memory->processes[i].pid == check_pid) {
                    shared_memory->processes[i].is_active = 0;
                    shared_memory->processes[i].status = TERMINATED;
                    shared_memory->process_count--;
                    cleaned = 1;

                    printf("\n[MONITOR] Process %d sonlandırıldı (Owner: %d).\n", check_pid, owner_pid);
                }
                sem_post(procx_sem);

                if (cleaned) {
                    send_message(2, check_pid);
                }
            }
        }
    }
    return NULL;
}

void* ipc_thread(void* arg) {
    Message message;

    while (!exit_requested) {
        if (msgrcv(msg_queue_id, &message, sizeof(message) - sizeof(long), getpid(), 0) == -1) {
            // Eğer kuyruk kapatıldıysa - Tüm terminallerden çıkış yapıldıysa
            if (errno == EIDRM || errno == EINVAL) {
                break; // Döngüyü kır ve thread'i sonlandır
            }
            perror("Mesaj kuyruktan alınamadı");
            break;
        }

        if (exit_requested) {
            break;
        }

        if (message.sender_pid == getpid()) {
            continue;
        }

        if (message.command == 1) {
            printf("\n[IPC] Yeni process başlatıldı: %d \n", message.target_pid);
        }
        else if (message.command == 2) {
            printf("\n[IPC] Process sonlandı: %d \n", message.target_pid);
        }
    }
    return NULL;
}


int main(int argc, char* argv[], char** envp) {
    init_shared_memory();
    init_semaphore();
    init_message_queue();
    // Action
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sa.sa_flags = 0; // fgets'in beklemesini önler
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    register_terminal();

    pthread_create(&thread_id_monitor, NULL, monitor_thread, NULL);
    pthread_create(&thread_id_ipc, NULL, ipc_thread, NULL);

    while (true) {
        int choice = get_menu();
        switch (choice) {
        case 0:
            // Programdan çık
            printf("ProcX Kapatılıyor...\n");
            shutdown_system();
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
