# Nombre de los archivos ejecutables a generar
BIN_FILES = server

# Compilador
CC = gcc

# Opciones de compilación
CPPFLAGS =
CFLAGS = -Wall -g
LDFLAGS = -L$(INSTALL_PATH)/lib/
LDLIBS = -lpthread

# Regla por defecto para construir todos los archivos binarios
all: $(BIN_FILES)

# Regla para construir el server
server: server.o lines.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

# Regla genérica para compilar archivos fuente .c
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

# Regla para limpiar los archivos generados
clean:
	rm -f $(BIN_FILES) *.o

# Evita conflictos con archivos que tengan el mismo nombre que las reglas
.PHONY : all clean
