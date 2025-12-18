# Derleyici ve Bayraklar
CC = gcc
# -Wall:
# -g:
# -D...:
CFLAGS = -Wall -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

# Bağlayıcı Bayrakları (Linker Flags)
# -lpthread: Thread kullanımı için
LDFLAGS = -lpthread

# Hedef Dosya ve Kaynaklar
TARGET = procx
SRC = procx.c
OBJ = $(SRC:.c=.o)

# Varsayılan kural
all: $(TARGET)

# Bağlama (Linking) Aşaması
$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)
	@echo "--------------------------------------"
	@echo "Build Successful! Run with: ./$(TARGET)"
	@echo "--------------------------------------"

# Derleme (Compiling) Aşaması
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Temizlik (make clean)
clean:
	rm -f $(OBJ) $(TARGET)
	@echo "Cleaned up executable and object files."

# Çalıştırma kısayolu (make run)
run: $(TARGET)
	./$(TARGET)

# Valgrind ile memory leak kontrolü (make check)
check: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

.PHONY: all clean run check