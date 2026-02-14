# Nome do executável
TARGET = mirror_screen

# Compilador
CC = gcc

# Opções de compilação (O3 para otimização de performance)
CFLAGS = -Wall -O3

# Bibliotecas necessárias
LIBS = -lX11 -lXext -lbcm2835 -lXtst

# O ficheiro fonte (ajuste se o nome for diferente)
SRC = mirror_screen.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	sudo ./$(TARGET)