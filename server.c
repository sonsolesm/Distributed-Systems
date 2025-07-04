// servidor.c
#include <pthread.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <strings.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "lines.h"


#define MAX_THREADS 	10
#define MAX_SOCKETS 	256

// Estructura usuario
typedef struct {
    char userName[256];
    char status[256];
    char ip[256];
    char port[256];
} User;

// Estructura content
typedef struct {
    char fileName[256];
    char description[256];
} Content;

// Estructura para asociar un mutex con el fichero de contenidos del usuario
typedef struct {
    char fileName[256];
    pthread_mutex_t mutex;
} MutexMap;

// Fichero de almacenamiento de los usuarios
const char* STORAGE_DIR = "storage";
const char* USERS_FILE = "users.txt";
char usersFilePath[256];

// Buffer de sockets, almacena punteros a entero
int* buffer_sockets[MAX_SOCKETS];

// Mutex y variables condicionales para proteger la copia de sockets del buffer
int n_elementos;			// elementos en el buffer de sockets
int pos_servicio = 0;       // posición en el buffer de sockets
pthread_mutex_t mutex;
pthread_cond_t no_lleno;
pthread_cond_t no_vacio;

// Mutex para los threads
pthread_mutex_t mfin;
int fin=false;

// Mutex para el acceso al fichero de usuarios
pthread_mutex_t users_file_mutex;

// Lista dinámica de MutexMap
MutexMap* mutexList = NULL;
int mutexCount = 0;
int mutexCapacity = 10;

// Socket del servidor
int sd;
// Variable global para controlar si se ha presionado Ctrl+C
volatile sig_atomic_t terminar_servidor = 0;

/** Función de manejo de la señal SIGINT (Ctrl+C) */
void signal_ctrlc(int signal) {
    if (signal == SIGINT) {
        printf("\nSe ha presionado Ctrl+C. Terminando el servidor...\n");
        // Actualizar la variable global para indicar que se debe terminar el servidor
        terminar_servidor = 1;
        // Cerrar el socket del servidor para que deje de esperar en accept
        close(sd);
    }
}

/** Función para inicializar la mutexList */
int init_mutex_list() {
    // Inicializar la lista dinámica para los mutex de los ficheros de contenidos de los usuarios
    mutexList = malloc(sizeof(MutexMap) * mutexCapacity);
    if (!mutexList) {
        perror("Error al asignar memoria para mutexList");
        mutexCount = 0;
        mutexCapacity = 0;
        return -1;
    }
    return 0;
}

/** Función para encontrar un mutex dado un nombre de fichero */
pthread_mutex_t* get_mutex_for_file(const char* fileName) {
    // Buscar un mutex asociado al fichero de contenidos
    for (int i = 0; i < mutexCount; i++) {
        if (strcmp(mutexList[i].fileName, fileName) == 0) {
            return &mutexList[i].mutex;
        }
    }

    // Si no se encuentra, añadir uno nuevo
    if (mutexCount == mutexCapacity) {
        mutexCapacity *= 2;
        MutexMap* newList = realloc(mutexList, sizeof(MutexMap) * mutexCapacity);
        if (!newList) {
            perror("Error al redimensionar memoria para mutexList");
            return NULL;
        }
        mutexList = newList;
    }

    // Crear un nuevo mutex
    strncpy(mutexList[mutexCount].fileName, fileName, sizeof(mutexList[mutexCount].fileName) - 1);
    mutexList[mutexCount].fileName[sizeof(mutexList[mutexCount].fileName) - 1] = '\0';
    pthread_mutex_init(&mutexList[mutexCount].mutex, NULL);

    return &mutexList[mutexCount++].mutex;
}

/** Función para obtener la IP local del servidor */
void obtener_ip_local(char* buffer, size_t len) {
    // Obtener el nombre del host
    char hostname[128];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("Error al obtener el nombre del host");
        strncpy(buffer, "127.0.0.1", len);  // Usar localhost si falla
        return;
    }

    // Resolver el nombre del host para obtener la IP
    struct hostent* host_entry = gethostbyname(hostname);
    if (host_entry == NULL) {
        perror("Error al resolver el nombre del host");
        strncpy(buffer, "127.0.0.1", len);  // Usar localhost si falla
        return;
    }

    // Convertir la dirección a formato legible
    struct in_addr* address = (struct in_addr*)host_entry->h_addr_list[0];
    strncpy(buffer, inet_ntoa(*address), len);
}

/** Función para inicializar storage */
void init_storage() {
    // Comprobar si el directorio existe, si no crearlo
    struct stat st = {0};
    if (stat(STORAGE_DIR, &st) == -1) {
        mkdir(STORAGE_DIR, 0700);
    }
    // Establecer la ruta del fichero de almacenamiento de los usuarios
    snprintf(usersFilePath, sizeof(usersFilePath), "%s/%s", STORAGE_DIR, USERS_FILE);
    // Eliminar los ficheros .txt existentes
    DIR* dir = opendir(STORAGE_DIR);
    if (dir == NULL) {
        perror("Failed to open directory");
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Comprobar si el archivo termina en .txt
        if (strstr(entry->d_name, ".txt") != NULL) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", STORAGE_DIR, entry->d_name);
            remove(path);
        }
    }
    closedir(dir);
}

/** Función para inicializar un fichero */
int init_file(const char *filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        perror("Error abriendo el fichero para inicialización");
        return -1;
    }
    fclose(file); // Cerrar el fichero lo reinicia
    return 0;
}

/** Función para cargar los usuarios del fichero */
// Formato de los datos:    userName|status|ip|port
// Ejemplo:                 lorenzo|DISCONNECTED|0.0.0.0|0
User* load_users(const char *filename, int* count) {
    // Abrir el fichero para lectura
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error abriendo el fichero para lectura");
        *count = 0;
        return NULL;
    }
    // Memoria dinámica para los usuarios
    int capacity = 10;
    User* users = malloc(sizeof(User) * capacity);
    if (!users) {
        perror("Error al asignar memoria");
        *count = 0;
        fclose(file);
        return NULL;
    }

    *count = 0;
    User temp;
    // Leer los datos del fichero y crear las estructuras User
    while (fscanf(file, "%255[^|]|%255[^|]|%255[^|]|%255[^\n]\n", temp.userName, temp.status, temp.ip, temp.port) == 4) {
        if (*count == capacity) {
            // Sí se alcanza la capacidad, reservar más memoria
            capacity *= 2;
            User* new_users = realloc(users, sizeof(User) * capacity);
            if (!new_users) {
                perror("Error al redimensionar memoria");
                free(users);
                *count = 0;
                fclose(file);
                return NULL;
            }
            users = new_users;
        }
        users[(*count)++] = temp;
    }

    fclose(file);
    return users;
}

/** Función para guardar los usuarios en el fichero */
int save_users(const char *filename, User* users, int count) {
    // Abrir el fichero para lectura
    FILE *file = fopen(filename, "w");
    if (!file) {
        perror("Error abriendo el fichero para escritura");
        return -1;
    }
    // Escribir las estructuras User
    for (int i = 0; i < count; i++) {
        fprintf(file, "%s|%s|%s|%s\n", users[i].userName, users[i].status, users[i].ip, users[i].port);
    }

    fclose(file);
    return 0; // Éxito
}

/** Función para buscar un usuario en la lista de usuarios */
int find_user(const User* users, int count, const char* userName) {
    // Encontrar al usuario en la lista
    int index = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(users[i].userName, userName) == 0) {
            index = i;
            break;
        }
    }
    return index;
}


/** Función para cargar los contenidos de un usuario del fichero */
// Formato de los datos:    fileName|description
// Ejemplo:                 fileName|description
Content* load_contents(const char *filename, int* count) {
    // Abrir el fichero para lectura
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error abriendo el fichero para lectura");
        *count = 0;
        return NULL;
    }
    // Memoria dinámica para los contenidos
    int capacity = 10;
    Content* contents = malloc(sizeof(Content) * capacity);
    if (!contents) {
        perror("Error al asignar memoria");
        *count = 0;
        fclose(file);
        return NULL;
    }

    *count = 0;
    Content temp;
    // Leer los datos del fichero y crear las estructuras Content
    while (fscanf(file, "%255[^|]|%255[^\n]\n", temp.fileName, temp.description) == 2) {
        if (*count == capacity) {
            // Sí se alcanza la capacidad, reservar más memoria
            capacity *= 2;
            Content* new_contents = realloc(contents, sizeof(Content) * capacity);
            if (!new_contents) {
                perror("Error al redimensionar memoria");
                free(contents);
                *count = 0;
                fclose(file);
                return NULL;
            }
            contents = new_contents;
        }
        contents[(*count)++] = temp;
    }

    fclose(file);
    return contents;
}

/** Función para guardar los contenidos de un usuario en el fichero */
int save_contents(const char *filename, Content* contents, int count) {
    // Abrir el fichero para lectura
    FILE *file = fopen(filename, "w");
    if (!file) {
        perror("Error abriendo el fichero para escritura");
        return -1;
    }
    // Escribir las estructuras Content
    for (int i = 0; i < count; i++) {
        fprintf(file, "%s|%s\n", contents[i].fileName, contents[i].description);
    }

    fclose(file);
    return 0; // Éxito
}

/** Función para buscar un fileName en la lista de contents */
int find_content(const Content* contents, int count, const char* fileName) {
    // Encontrar el fileName en la lista
    int index = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(contents[i].fileName, fileName) == 0) {
            index = i;
            break;
        }
    }
    return index;
}


/** Servicio REGISTER */
int register_user(const char* userName) {
    int count;
    // Bloqueamos el mutex para el acceso a los datos del fichero
    pthread_mutex_lock(&users_file_mutex);
    // Obtener los usuarios del fichero
    User* users = load_users(usersFilePath, &count);
    if (!users) {
        perror("Error al cargar el fichero de usuarios");
        pthread_mutex_unlock(&users_file_mutex); // Desbloquear al terminar con el fichero
        return 2; // Error: no se pudo cargar el fichero de datos
    }

    // Buscar al usuario en la lista
    int index = find_user(users, count, userName);
    // Comprobar si el usuario ya está registrado
    if (index != -1) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 1;  // Usuario ya registrado
    }

    // Registrar nuevo usuario
    User new_user;
    strncpy(new_user.userName, userName, sizeof(new_user.userName) - 1);
    strcpy(new_user.status, "DISCONNECTED");
    strcpy(new_user.ip, "0.0.0.0");
    strcpy(new_user.port, "0");

    // Redimensionar memoria dinámica para la nueva lista de usuarios
    User* new_users = realloc(users, sizeof(User) * (count + 1));
    if (!new_users) {
        perror("Error al redimensionar memoria");
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 2;  // Error al redimensionar memoria
    }
    // Actualizar el puntero users con el nuevo bloque
    users = new_users;
    // Añadir a la lista de usuarios
    users[count] = new_user;

    // Guardar los cambios en el fichero
    if (save_users(usersFilePath, users, count + 1) != 0) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 2; // Error al guardar los datos
    }
    free(users);
    pthread_mutex_unlock(&users_file_mutex);  // Desbloquear al terminar con el fichero

    // Inicializar fichero de contenidos para el usuario (estructura de almacenamiento)
    char contentsFilePath[512];
    snprintf(contentsFilePath, sizeof(contentsFilePath), "%s/%s.txt", STORAGE_DIR, userName);
    init_file(contentsFilePath);    // contentsFilePath = storage/userName.txt

    return 0;  // Éxito
}

/** Servicio UNREGISTER */
int unregister_user(const char* userName) {
    int count;
    // Bloqueamos el mutex para el acceso a los datos del fichero
    pthread_mutex_lock(&users_file_mutex);
    // Obtener los usuarios del fichero
    User* users = load_users(usersFilePath, &count);
    if (!users) {
        perror("Error al cargar el fichero de usuarios");
        pthread_mutex_unlock(&users_file_mutex); // Desbloquear al terminar con el ficheros
        return 2; // Error: no se pudo cargar el fichero de datos
    }

    // Buscar al usuario en la lista
    int index = find_user(users, count, userName);
    // Comprobar si el usuario está registrado
    if (index == -1) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 1; // Usuario no registrado
    }

    // Eliminar al usuario de la lista moviendo los elementos restantes hacia atrás
    memmove(&users[index], &users[index + 1], (count - index - 1) * sizeof(User));
    count--;

    // Guardar los cambios en el fichero
    if (save_users(usersFilePath, users, count) != 0) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 2; // Error al guardar los datos
    }
    free(users);
    pthread_mutex_unlock(&users_file_mutex);  // Desbloquear al terminar con el fichero
    return 0;  // Éxito
}

/** Servicio CONNECT */
int connect_user(const char* userName, const char* ip, const char* port) {
    int count;
    // Bloqueamos el mutex para el acceso a los datos del fichero
    pthread_mutex_lock(&users_file_mutex);
    // Obtener los usuarios del fichero
    User* users = load_users(usersFilePath, &count);
    if (!users) {
        perror("Error al cargar el fichero de usuarios");
        pthread_mutex_unlock(&users_file_mutex); // Desbloquear al terminar con el fichero
        return 3; // Error: no se pudo cargar el fichero de datos
    }

    // Encontrar al usuario en la lista
    int index = find_user(users, count, userName);
    // Comprobar si el usuario está registrado
    if (index == -1) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 1; // Usuario no registrado
    }
    // Verificar si el usuario ya está conectado
    if (strcmp(users[index].status, "CONNECTED") == 0) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 2; // Usuario ya está conectado
    }
    // Actualizar la IP, el puerto y el estado a "CONNECTED"
    strncpy(users[index].ip, ip, sizeof(users[index].ip) - 1);
    users[index].ip[sizeof(users[index].ip) - 1] = '\0';  // Asegurar nul-terminación

    strncpy(users[index].port, port, sizeof(users[index].port) - 1);
    users[index].port[sizeof(users[index].port) - 1] = '\0';  // Asegurar nul-terminación

    strcpy(users[index].status, "CONNECTED");

    // Guardar los cambios en el fichero
    if (save_users(usersFilePath, users, count) != 0) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 3; // Error al guardar los datos
    }
    free(users);
    pthread_mutex_unlock(&users_file_mutex);  // Desbloquear al terminar con el fichero
    return 0;  // Éxito
}

/** Servicio DISCONNECT */
int disconnect_user(const char* userName) {
    int count;
    // Bloqueamos el mutex para el acceso a los datos del fichero
    pthread_mutex_lock(&users_file_mutex);
    // Obtener los usuarios del fichero
    User* users = load_users(usersFilePath, &count);
    if (!users) {
        perror("Error al cargar el fichero de usuarios");
        pthread_mutex_unlock(&users_file_mutex); // Desbloquear al terminar con el fichero
        return 3; // Error: no se pudo cargar el fichero de datos
    }

    // Encontrar al usuario en la lista
    int index = find_user(users, count, userName);
    // Comprobar si el usuario está registrado
    if (index == -1) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 1; // Usuario no registrado
    }
    // Verificar si el usuario ya está desconectado
    if (strcmp(users[index].status, "DISCONNECTED") == 0) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 2; // Usuario ya está desconectado
    }
    // Actualizar la IP, el puerto y el estado a "DISCONNECTED"
    strcpy(users[index].status, "DISCONNECTED");
    strcpy(users[index].ip, "0.0.0.0");
    strcpy(users[index].port, "0");

    // Guardar los cambios en el fichero
    if (save_users(usersFilePath, users, count) != 0) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 3; // Error al guardar los datos
    }
    free(users);
    pthread_mutex_unlock(&users_file_mutex);  // Desbloquear al terminar con el fichero
    return 0;  // Éxito
}

/** Servicio PUBLISH */
int publish_content(const char* userName, const char* fileName, const char* description) {
    int usersCount;
    // Bloqueamos el mutex para el acceso a los datos del fichero
    pthread_mutex_lock(&users_file_mutex);
    // Obtener los usuarios del fichero
    User* users = load_users(usersFilePath, &usersCount);
    if (!users) {
        perror("Error al cargar el fichero de usuarios");
        pthread_mutex_unlock(&users_file_mutex); // Desbloquear al terminar con el fichero
        return 4; // Error: no se pudo cargar el fichero de datos
    }

    // Buscar al usuario en la lista
    int userIndex = find_user(users, usersCount, userName);
    // Comprobar si el usuario está registrado
    if (userIndex == -1) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 1;  // Usuario no registrado
    }
    // Comprobar si el usuario está conectado
    if (strcmp(users[userIndex].status, "DISCONNECTED") == 0) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 2; // Usuario está desconectado
    }
    // Liberar memoria y desbloquear el mutex de usuarios
    free(users);
    pthread_mutex_unlock(&users_file_mutex);

    // Obtener el nombre del fichero de contenidos del usuario
    char contentsFilePath[512];
    snprintf(contentsFilePath, sizeof(contentsFilePath), "%s/%s.txt", STORAGE_DIR, userName);

    // Obtener el mutex asociado al fichero de contenidos del usuario
    pthread_mutex_t* contentMutex = get_mutex_for_file(contentsFilePath);
    if (!contentMutex) {
        perror("Error al obtener el mutex para el fichero de contenidos");
        return 4; // Error general
    }

    // Bloquear el mutex para acceder al fichero de contenidos
    pthread_mutex_lock(contentMutex);

    // Cargar la lista de contenidos
    int contentsCount;
    Content* contents = load_contents(contentsFilePath, &contentsCount);
    if (!contents) {
        perror("Error al cargar el fichero de contenidos");
        pthread_mutex_unlock(contentMutex);
        return 4;   // Error: no se pudo cargar el fichero de datos
    }
    // Buscar el content (fileName) en la lista de contenidos
    int contentIndex = find_content(contents, contentsCount, fileName);
    // Comprobar si el fichero ya está publicado
    if (contentIndex != -1) {
        free(contents);
        pthread_mutex_unlock(contentMutex);
        return 3; // El fichero ya está publicado
    }

    // Crear nuevo contenido
    Content newContent;
    strncpy(newContent.fileName, fileName, sizeof(newContent.fileName) - 1);
    newContent.fileName[sizeof(newContent.fileName) - 1] = '\0';    // Asegurar terminación nula
    strncpy(newContent.description, description, sizeof(newContent.description) - 1);
    newContent.description[sizeof(newContent.description) - 1] = '\0';  // Asegurar terminación nula

    // Redimensionar memoria dinámica para la nueva lista de contenidos
    Content* new_contents = realloc(contents, sizeof(Content) * (contentsCount + 1));
    if (!new_contents) {
        perror("Error al redimensionar memoria");
        free(contents);
        pthread_mutex_unlock(contentMutex);
        return 4;   // Error al redimensionar memoria
    }
    // Actualizar el puntero contents con el nuevo bloque
    contents = new_contents;
    // Añadir a la lista de contenidos
    contents[contentsCount] = newContent;

    // Guardar los cambios en el fichero
    if (save_contents(contentsFilePath, contents, contentsCount + 1) != 0) {
        free(contents);
        pthread_mutex_unlock(contentMutex);
        return 4;   // Error al guardar los datos
    }
    free(contents);
    pthread_mutex_unlock(contentMutex);  // Desbloquear al terminar con el fichero
    return 0;   // Éxito
}

/** Servicio DELETE */
int delete_content(const char* userName, const char* fileName) {
    int usersCount;
    // Bloqueamos el mutex para el acceso a los datos del fichero
    pthread_mutex_lock(&users_file_mutex);
    // Obtener los usuarios del fichero
    User* users = load_users(usersFilePath, &usersCount);
    if (!users) {
        perror("Error al cargar el fichero de usuarios");
        pthread_mutex_unlock(&users_file_mutex); // Desbloquear al terminar con el fichero
        return 4; // Error: no se pudo cargar el fichero de datos
    }

    // Buscar al usuario en la lista
    int userIndex = find_user(users, usersCount, userName);
    // Comprobar si el usuario está registrado
    if (userIndex == -1) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 1;  // Usuario no registrado
    }
    // Comprobar si el usuario está conectado
    if (strcmp(users[userIndex].status, "DISCONNECTED") == 0) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        return 2; // Usuario está desconectado
    }
    // Liberar memoria y desbloquear el mutex de usuarios
    free(users);
    pthread_mutex_unlock(&users_file_mutex);

    // Obtener el nombre del fichero de contenidos del usuario
    char contentsFilePath[512];
    snprintf(contentsFilePath, sizeof(contentsFilePath), "%s/%s.txt", STORAGE_DIR, userName);

    // Obtener el mutex asociado al fichero de contenidos del usuario
    pthread_mutex_t* contentMutex = get_mutex_for_file(contentsFilePath);
    if (!contentMutex) {
        perror("Error al obtener el mutex para el fichero de contenidos");
        return 4; // Error general
    }

    // Bloquear el mutex para acceder al fichero de contenidos
    pthread_mutex_lock(contentMutex);

    // Cargar la lista de contenidos
    int contentsCount;
    Content* contents = load_contents(contentsFilePath, &contentsCount);
    if (!contents) {
        perror("Error al cargar el fichero de contenidos");
        pthread_mutex_unlock(contentMutex);
        return 4;   // Error: no se pudo cargar el fichero de datos
    }
    // Buscar el content (fileName) en la lista de contenidos
    int contentIndex = find_content(contents, contentsCount, fileName);
    // Comprobar si el fichero ha sido publicado
    if (contentIndex == -1) {
        free(contents);
        pthread_mutex_unlock(contentMutex);
        return 3; // El fichero no ha sido publicado
    }

    // Eliminar el contenido de la lista
    memmove(&contents[contentIndex], &contents[contentIndex + 1], (contentsCount - contentIndex - 1) * sizeof(Content));
    contentsCount--;

    // Guardar los cambios en el fichero
    if (save_contents(contentsFilePath, contents, contentsCount) != 0) {
        free(contents);
        pthread_mutex_unlock(contentMutex);
        return 4;   // Error al guardar los datos
    }
    free(contents);
    pthread_mutex_unlock(contentMutex);  // Desbloquear al terminar con el fichero
    return 0;   // Éxito
}

/** Servicio LIST_USERS */
int list_users(const char* userName, int sc_local, char * buffer) {
    int resultado;
    int usersCount;
    // Bloqueamos el mutex para el acceso a los datos del fichero
    pthread_mutex_lock(&users_file_mutex);
    // Obtener los usuarios del fichero
    User* users = load_users(usersFilePath, &usersCount);
    if (!users) {
        perror("Error al cargar el fichero de usuarios");
        pthread_mutex_unlock(&users_file_mutex); // Desbloquear al terminar con el fichero
        resultado = 3; // Error: no se pudo cargar el fichero de datos
        // Devolver el resultado al cliente por su socket
        sprintf(buffer, "%d", resultado);
        if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
            perror("Error al enviar el resultado al cliente (servicio)");
            return 3;
        }
        return resultado;
    }

    // Buscar al usuario en la lista
    int userIndex = find_user(users, usersCount, userName);
    // Comprobar si el usuario está registrado
    if (userIndex == -1) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        resultado = 1;  // Usuario no registrado
        // Devolver el resultado al cliente por su socket
        sprintf(buffer, "%d", resultado);
        if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
            perror("Error al enviar el resultado al cliente (servicio)");
            return 3;
        }
        return resultado;
    }
    // Comprobar si el usuario está conectado
    if (strcmp(users[userIndex].status, "DISCONNECTED") == 0) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        resultado = 2; // Usuario está desconectado
        // Devolver el resultado al cliente por su socket
        sprintf(buffer, "%d", resultado);
        if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
            perror("Error al enviar el resultado al cliente (servicio)");
            return 3;
        }
        return resultado;
    }

    // Usuario está registrado y conectado
    resultado = 0;
    // Devolver el resultado al cliente por su socket
    sprintf(buffer, "%d", resultado);
    if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        perror("Error al enviar el resultado al cliente (servicio)");
        return 3;
    }

    // Contar cuántos usuarios están conectados
    int connectedUsersCount = 0;
    for (int i = 0; i < usersCount; i++) {
        if (strcmp(users[i].status, "CONNECTED") == 0) {
            connectedUsersCount++;
        }
    }

    // Enviar el número de usuarios conectados al cliente
    sprintf(buffer, "%d", connectedUsersCount);
    if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        perror("Error al enviar el numero de usuarios conectados (servicio)");
        return 3;
    }

    // Enviar datos de cada usuario conectado
    for (int i = 0; i < usersCount; i++) {
        if (strcmp(users[i].status, "CONNECTED") == 0) {
            // Enviar userName
            if (sendMessage(sc_local, users[i].userName, strlen(users[i].userName) + 1) == -1) {
                free(users);
                pthread_mutex_unlock(&users_file_mutex);
                perror("Error al enviar el nombre del usuario conectado (servicio)");
                return 3;
            }
            // Enviar ip
            if (sendMessage(sc_local, users[i].ip, strlen(users[i].ip) + 1) == -1) {
                free(users);
                pthread_mutex_unlock(&users_file_mutex);
                perror("Error al enviar la ip del usuariolist_users conectado (servicio)");
                return 3;
            }
            // Enviar puerto
            if (sendMessage(sc_local, users[i].port, strlen(users[i].port) + 1) == -1) {
                free(users);
                pthread_mutex_unlock(&users_file_mutex);
                perror("Error al enviar el puerto del usuario conectado (servicio)");
                return 3;
            }
        }
    }

    // Liberar memoria y desbloquear el mutex de usuarios
    free(users);
    pthread_mutex_unlock(&users_file_mutex);
    return resultado;   // Éxito
}

/** Servicio LIST_CONTENT */
int list_user_contents(const char* userName, const char* remoteUserName, int sc_local, char * buffer) {
    int resultado;
    int usersCount;
    // Bloqueamos el mutex para el acceso a los datos del fichero
    pthread_mutex_lock(&users_file_mutex);
    // Obtener los usuarios del fichero
    User* users = load_users(usersFilePath, &usersCount);
    if (!users) {
        perror("Error al cargar el fichero de usuarios");
        pthread_mutex_unlock(&users_file_mutex); // Desbloquear al terminar con el fichero
        resultado = 4; // Error: no se pudo cargar el fichero de datos
        // Devolver el resultado al cliente por su socket
        sprintf(buffer, "%d", resultado);
        if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
            perror("Error al enviar el resultado al cliente (servicio)");
            return 4;
        }
        return resultado;
    }

    // Buscar al usuario en la lista
    int userIndex = find_user(users, usersCount, userName);
    // Comprobar si el usuario está registrado
    if (userIndex == -1) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        resultado = 1;  // Usuario no registrado
        // Devolver el resultado al cliente por su socket
        sprintf(buffer, "%d", resultado);
        if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
            perror("Error al enviar el resultado al cliente (servicio)");
            return 4;
        }
        return resultado;
    }
    // Comprobar si el usuario está conectado
    if (strcmp(users[userIndex].status, "DISCONNECTED") == 0) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        resultado = 2; // Usuario está desconectado
        // Devolver el resultado al cliente por su socket
        sprintf(buffer, "%d", resultado);
        if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
            perror("Error al enviar el resultado al cliente (servicio)");
            return 4;
        }
        return resultado;
    }

    // Buscar al usuario cuyo contenido se quiere conocer en la lista
    userIndex = find_user(users, usersCount, remoteUserName);
    // Comprobar si el usuario está registrado
    if (userIndex == -1) {
        free(users);
        pthread_mutex_unlock(&users_file_mutex);
        resultado = 3;  // Usuario cuyo contenido se quiere conocer no registrado
        // Devolver el resultado al cliente por su socket
        sprintf(buffer, "%d", resultado);
        if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
            perror("Error al enviar el resultado al cliente (servicio)");
            return 4;
        }
        return resultado;
    }

    // Usuario registrado y conectado, y el usuario cuyo contenido quiere conocer está registrado
    resultado = 0;
    // Liberar memoria y desbloquear el mutex de usuarios
    free(users);
    pthread_mutex_unlock(&users_file_mutex);
    // Devolver el resultado al cliente por su socket
    sprintf(buffer, "%d", resultado);
    if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
        perror("Error al enviar el resultado al cliente (servicio)");
        return 4;
    }

    // Obtener el nombre del fichero de contenidos del usuario
    char contentsFilePath[512];
    snprintf(contentsFilePath, sizeof(contentsFilePath), "%s/%s.txt", STORAGE_DIR, remoteUserName);

    // Obtener el mutex asociado al fichero de contenidos del usuario
    pthread_mutex_t* contentMutex = get_mutex_for_file(contentsFilePath);
    if (!contentMutex) {
        perror("Error al obtener el mutex para el fichero de contenidos");
        resultado = 4;  // Error general
        sprintf(buffer, "%d", resultado);
        sendMessage(sc_local, buffer, strlen(buffer) + 1);
        if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
            perror("Error al enviar el resultado al cliente (servicio)");
            return 4;
        }
        return resultado;
    }

    // Bloquear el mutex para acceder al fichero de contenidos
    pthread_mutex_lock(contentMutex);

    // Cargar la lista de contenidos
    int contentsCount;
    Content* contents = load_contents(contentsFilePath, &contentsCount);
    if (!contents) {
        perror("Error al cargar el fichero de contenidos");
        pthread_mutex_unlock(contentMutex);
        resultado = 4; // Error: no se pudo cargar el fichero de datos
        // Devolver el resultado al cliente por su socket
        sprintf(buffer, "%d", resultado);
        if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
            perror("Error al enviar el resultado al cliente (servicio)");
            return 4;
        }
        return resultado;
    }

    // Devolver al cliente el número de contenidos
    sprintf(buffer, "%d", contentsCount);
    if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
        free(contents);
        pthread_mutex_unlock(contentMutex);
        perror("Error al enviar el numero de contenidos (servicio)");
        return 4;
    }

    // Enviar datos de cada contenido del usuario
    for (int i = 0; i < contentsCount; i++) {
        // Enviar fileName
        if (sendMessage(sc_local, contents[i].fileName, strlen(contents[i].fileName) + 1) == -1) {
            free(contents);
            pthread_mutex_unlock(contentMutex);
            perror("Error al enviar el fileName (servicio)");
            return 4;
        }
        // Enviar description
        if (sendMessage(sc_local, contents[i].description, strlen(contents[i].description) + 1) == -1) {
            free(contents);
            pthread_mutex_unlock(contentMutex);
            perror("Error al enviar description (servicio)");
            return 4;
        }
    }

    // Liberar memoria y desbloquear el mutex del fichero de contenidos
    free(contents);
    pthread_mutex_unlock(contentMutex);
    return resultado;   // Éxito
}


/** Función ejecutada por los threads del pool */
void servicio(void) {
    int sc_local;   // descriptor del socket del cliente

    for(;;) {
        pthread_mutex_lock(&mutex);
        while (n_elementos == 0) {
            if (fin==true) {
                fprintf(stderr,"Finalizando servicio thread\n");
                pthread_mutex_unlock(&mutex);
                pthread_exit(0);
            }
            pthread_cond_wait(&no_vacio, &mutex);
        }
        // Obtener el socket de un cliente del buffer
        sc_local = *buffer_sockets[pos_servicio];
        free(buffer_sockets[pos_servicio]); // liberar la memoria de la copia del descriptor
        buffer_sockets[pos_servicio] = NULL; // precaución adicional
        pos_servicio = (pos_servicio + 1) % MAX_SOCKETS;
        n_elementos --;
        pthread_cond_signal(&no_lleno);
        pthread_mutex_unlock(&mutex);

        // Recibir el código de operación (op) del cliente
        char buffer[256];
        char op[256];
        if (readLine(sc_local, op, sizeof(op)) == -1) {
            perror("Error al recibir el código de operación en readLine (servicio)");
            close(sc_local);
            continue;
        }
        // Recibir el dateTime del servicio web
        char dateTime[256];
        if (readLine(sc_local, dateTime, sizeof(dateTime)) == -1) {
            perror("Error al recibir el dateTime en readLine (servicio)");
            close(sc_local);
            continue;
        }
        // Recibir el userName del cliente
        char userName[256];
        if (readLine(sc_local, userName, sizeof(userName)) == -1) {
            perror("Error al recibir el nombre de usuario en readLine (servicio)");
            close(sc_local);
            continue;
        }

        // Procesar la petición basada en op
        if (strcmp(op, "REGISTER") == 0) {
            printf("Servicio: Procesando petición REGISTER\n");
            printf("s> OPERATION FROM %s\n", userName);
            // Registrar usuario
            int resultado = register_user(userName);
            sprintf(buffer, "%d", resultado);
            // Devolver el resultado al cliente por su socket
            if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
                perror("Error al enviar el resultado al cliente (servicio)");
                close(sc_local);
                continue;
            }
            // Cerrar la conexión
            close(sc_local);
        }
        else if (strcmp(op, "UNREGISTER") == 0) {
            printf("Servicio: Procesando petición UNREGISTER\n");
            printf("s> OPERATION FROM %s\n", userName);
            // Registrar usuario
            int resultado = unregister_user(userName);
            sprintf(buffer, "%d", resultado);
            // Devolver el resultado al cliente por su socket
            if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
                perror("Error al enviar el resultado al cliente (servicio)");
                close(sc_local);
                continue;
            }
            // Cerrar la conexión
            close(sc_local);
        }
        else if (strcmp(op, "CONNECT") == 0) {
            printf("Servicio: Procesando petición CONNECT\n");
            // Recibir la dirección IP del cliente
            char ip[256];
            if (readLine(sc_local, ip, sizeof(ip)) == -1) {
                perror("Error al recibir la IP en readLine (servicio)");
                close(sc_local);
                continue;
            }
            // Recibir el puerto del cliente
            char port[256];
            if (readLine(sc_local, port, sizeof(port)) == -1) {
                perror("Error al recibir el puerto en readLine (servicio)");
                close(sc_local);
                continue;
            }

            printf("s> OPERATION FROM %s\n", userName);

            // Conectar usuario
            int resultado = connect_user(userName, ip, port);
            sprintf(buffer, "%d", resultado);
            // Devolver el resultado al cliente por su socket
            if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
                perror("Error al enviar el resultado al cliente (servicio)");
                close(sc_local);
                continue;
            }
            // Cerrar la conexión
            close(sc_local);
        }
        else if (strcmp(op, "DISCONNECT") == 0) {
            printf("Servicio: Procesando petición DISCONNECT\n");
            printf("s> OPERATION FROM %s\n", userName);

            // Desconectar usuario
            int resultado = disconnect_user(userName);
            sprintf(buffer, "%d", resultado);
            // Devolver el resultado al cliente por su socket
            if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
                perror("Error al enviar el resultado al cliente (servicio)");
                close(sc_local);
                continue;
            }
            // Cerrar la conexión
            close(sc_local);
        }
        else if (strcmp(op, "PUBLISH") == 0) {
            printf("Servicio: Procesando petición PUBLISH\n");
            // Recibir fileName del cliente
            char fileName[256];
            if (readLine(sc_local, fileName, sizeof(fileName)) == -1) {
                perror("Error al recibir fileName en readLine (servicio)");
                close(sc_local);
                continue;
            }
            // Recibir description del cliente
            char description[256];
            if (readLine(sc_local, description, sizeof(description)) == -1) {
                perror("Error al recibir description en readLine (servicio)");
                close(sc_local);
                continue;
            }

            printf("s> OPERATION FROM %s\n", userName);

            // Publicar contenido
            int resultado = publish_content(userName, fileName, description);
            sprintf(buffer, "%d", resultado);
            // Devolver el resultado al cliente por su socket
            if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
                perror("Error al enviar el resultado al cliente (servicio)");
                close(sc_local);
                continue;
            }
            // Cerrar la conexión
            close(sc_local);
        }
        else if (strcmp(op, "DELETE") == 0) {
            printf("Servicio: Procesando petición DELETE\n");
            // Recibir fileName del cliente
            char fileName[256];
            if (readLine(sc_local, fileName, sizeof(fileName)) == -1) {
                perror("Error al recibir fileName en readLine (servicio)");
                close(sc_local);
                continue;
            }

            printf("s> OPERATION FROM %s\n", userName);

            // Eliminar contenido
            int resultado = delete_content(userName, fileName);
            sprintf(buffer, "%d", resultado);
            // Devolver el resultado al cliente por su socket
            if (sendMessage(sc_local, buffer, strlen(buffer) + 1) == -1) {
                perror("Error al enviar el resultado al cliente (servicio)");
                close(sc_local);
                continue;
            }
            // Cerrar la conexión
            close(sc_local);
        }
        else if (strcmp(op, "LIST_USERS") == 0) {
            printf("Servicio: Procesando petición LIST_USERS\n");

            printf("s> OPERATION FROM %s\n", userName);

            // Enviar usuarios conectados
            list_users(userName, sc_local, buffer);

            // Cerrar la conexión
            close(sc_local);
        }
        else if (strcmp(op, "LIST_CONTENT") == 0) {
            printf("Servicio: Procesando petición LIST_CONTENT\n");
            // Recibir el nombre del usuario cuyo contenido quiere conocer
            char remoteUserName[256];
            if (readLine(sc_local, remoteUserName, sizeof(remoteUserName)) == -1) {
                perror("Error al recibir el nombre de usuario en readLine (servicio)");
                close(sc_local);
                continue;
            }

            printf("s> OPERATION FROM %s\n", userName);

            // Enviar contenidos publicados por el usuario
            list_user_contents(userName, remoteUserName, sc_local, buffer);

            // Cerrar la conexión
            close(sc_local);
        }

        else {
            // Código de operación no reconocido
            printf("Servicio: Código de operación incorrecto\n");
        }

    } // FOR

    pthread_exit(0);

} // SERVICIO


int main(int argc, char *argv[]) {
    // Comprobar que se pasa el puerto en la línea de mandatos
    int port = 0;
    int opt;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s -p <port>\n", argv[0]);
                return -1;
        }
    }
    // Comprobar la validez del puerto
    if (port == 0) {
        fprintf(stderr, "Error: Must specify a port with -p <port>\n");
        return -1;
    }
    else if (port < 1024 || port > 65535) {
        fprintf(stderr, "Error: Port must be in the range 1024 <= port <= 65535\n");
        return -1;
    }

    // Obtener la IP local
    char ip_local[INET_ADDRSTRLEN];
    obtener_ip_local(ip_local, sizeof(ip_local));

    // Mostrar mensaje inicial
    printf("s> init server %s:%d\n", ip_local, port);

    // Registrar el manejador de señales para SIGINT (Ctrl+C)
    if (signal(SIGINT, signal_ctrlc) == SIG_ERR) {
        perror("Error al registrar el manejador de señales (servidor)\n");
        return -1;
    }

    pthread_attr_t t_attr;	// atributos de los threads
    pthread_t thid[MAX_THREADS];
    int err;
    int pos = 0;

    struct sockaddr_in server_addr,  client_addr;
    socklen_t size;
    int sc;
    int val;

    // Crear descriptor del socket del servidor
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("Error al crear el socket del servidor (servidor)\n");
        return -1;
    }

    // Modificar opciones asociadas al socket
    val = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char *) &val, sizeof(int));

    // Configurar la dirección del servidor
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(port);

    // Asignar una dirección local a un socket
    err = bind(sd, (const struct sockaddr *)&server_addr,sizeof(server_addr));
    if (err == -1) {
        perror("Error en bind (servidor)\n");
        close (sd);
        return -1;
    }

    // Preparar para aceptar conexiones
    err = listen(sd, SOMAXCONN);
    if (err == -1) {
        perror("Error en listen (servidor)\n");
        close (sd);
        return -1;
    }

    size = sizeof(client_addr);

    // Inicializar los mutex
    pthread_mutex_init(&mutex,NULL);
    pthread_cond_init(&no_lleno,NULL);
    pthread_cond_init(&no_vacio,NULL);
    pthread_mutex_init(&mfin,NULL);
    pthread_mutex_init(&users_file_mutex, NULL);
    init_mutex_list();

    // Creación del pool de threads
    pthread_attr_init(&t_attr);
    for (int i = 0; i < MAX_THREADS; i++)
        if (pthread_create(&thid[i], NULL, (void *)servicio, NULL) !=0){
            perror("Error creando el pool de threads (servidor)\n");
            close (sd);
            return -1;
        }

    // Inicializar storage y eliminar archivos .txt existentes
    init_storage();
    // Inicializar fichero de usuarios (estructura de almacenamiento)
    init_file(usersFilePath);

    // Bucle para aceptar conexiones de clientes
    while (terminar_servidor == 0) {
        //printf("\nEsperando conexión...\n");
        sc = accept(sd, (struct sockaddr *) &client_addr, (socklen_t *) &size);
        if (sc == -1) {
            if (terminar_servidor == 1) {
                // accept fue interrumpido por una señal
                break;
            }
            perror("Error en accept (servidor)\n");
            continue;   // Intentar aceptar una nueva conexión
            //return -1;
        }

        printf("Conexión aceptada de IP: %s   Puerto: %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Si no hay errores al aceptar la conexión, añadir el descriptor del socket al buffer
        int* sc_local = malloc(sizeof(int));
        if (sc_local == NULL) {
            perror("Error al asignar memoria sc_local (servidor)");
            close(sc);
            close (sd);
            return -1;
        }
        *sc_local = sc;
        pthread_mutex_lock(&mutex);
        while (n_elementos == MAX_SOCKETS) {
            pthread_cond_wait(&no_lleno, &mutex);
        }
        // Añadir la copia del descriptor (sc_local) al buffer de sockets
        buffer_sockets[pos] = sc_local;
        pos = (pos + 1) % MAX_SOCKETS;
        n_elementos++;
        pthread_cond_signal(&no_vacio);
        pthread_mutex_unlock(&mutex);
    } // WHILE

    // Si se termina el servidor
    // Cerrar sockets que puedan quedar abiertos en el buffer
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (buffer_sockets[i] != NULL) {
            close(*buffer_sockets[i]); // Cerrar el socket
            free(buffer_sockets[i]);  // Liberar la memoria asignada
            buffer_sockets[i] = NULL; // Marcar como nulo por precaución
        }
    }
    pthread_mutex_unlock(&mutex);

    // Notificar a los threads que deben terminar
    pthread_mutex_lock(&mfin);
    fin=true;
    pthread_mutex_unlock(&mfin);

    pthread_mutex_lock(&mutex);
    pthread_cond_broadcast(&no_vacio);
    pthread_mutex_unlock(&mutex);

    // Esperar a los threads
    for (int i=0;i<MAX_THREADS;i++)
        pthread_join(thid[i],NULL);

    // Destruir mutexes y variables condicionales
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&no_lleno);
    pthread_cond_destroy(&no_vacio);
    pthread_mutex_destroy(&mfin);
    pthread_mutex_destroy(&users_file_mutex);
    if (mutexList != NULL) {
        for (int i = 0; i < mutexCount; i++) {
            pthread_mutex_destroy(&mutexList[i].mutex);
        }
        free(mutexList); // Liberar el arreglo de estructuras MutexMap
    }

    // Cerrar el socket del servidor
    close (sd);

    return 0;
}
