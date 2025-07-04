from enum import Enum
from zeep import Client as ZeepClient
import argparse
import socket
import threading
import os

class client:

    # ******************** TYPES *********************
    # *
    # * @brief Return codes for the protocol methods
    class RC(Enum):
        OK = 0
        ERROR = 1
        USER_ERROR = 2

    # ****************** ATTRIBUTES ******************
    _server = None      # IP del servidor
    _port = -1          # Puerto del servidor
    _userName = None    # Nombre del usuario que se conecta
    _thread = None      # Hilo de escucha del cliente
    _users = {}         # Diccionario para almacenar los usuarios
    _lastRegisteredUser = None      # Nombre del último usuario registrado
    _lastConnectedUser = None       # Nombre del último usuario conectado

    # ******************** METHODS *******************
    @staticmethod
    def connectServer(host, port):
        """Método para conectar con el servidor"""
        try:
            # Crear el socket del servidor
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server_address = (host, int(port))
            print('Connecting to {} port {}'.format(*server_address))
            # Conectar con el servidor
            sock.connect(server_address)
            return sock
        except socket.error as e:
            print(f"Error al conectar o crear el socket del servidor: {e}")
            return None

    @staticmethod
    def recvRes(sock):
        """Método para recibir la respuesta del cliente servidor"""
        try:
            a = ''
            while True:
                msg = sock.recv(1)
                if not msg:
                    raise ValueError("Conexión cerrada por el cliente servidor")
                if msg == b'\0':
                    break
                a += msg.decode()

            return a
        except socket.error as e:
            print(f"Error de socket recibiendo la respuesta: {e}")
            raise  # Propagar el error
        except UnicodeDecodeError as e:
            print(f"Error decodificando la respuesta: {e}")
            raise  # Propagar el error

    @staticmethod
    def register(user):
        """Método para el registro de un cliente en el sistema"""
        # Conectarse al servidor
        sock = client.connectServer(client._server, client._port)
        if sock is None:
            print("REGISTER FAIL")
            return client.RC.USER_ERROR

        try:
            # Enviar cadena con la operación
            sock.sendall("REGISTER".encode()+b'\0')
            # Enviar el dateTime
            sock.sendall(str(client.dateTimeService()).encode() + b'\0')
            # Enviar el nombre de usuario
            sock.sendall(str(user).encode() + b'\0')
            # Recibir el resultado de la operación
            res = client.recvRes(sock)

            # Tratar el resultado de la operación
            if res == "0":
                print("REGISTER OK")
                # Guardar el userName del último cliente registrado (arreglo)
                client._lastRegisteredUser = user
                return client.RC.OK
            elif res == "1":
                print("USERNAME IN USE")
                return client.RC.ERROR
            elif res == "2":
                print("REGISTER FAIL")
                return client.RC.USER_ERROR

        except Exception as e:
            print(f"Error durante la operación REGISTER: {e}")
            print("REGISTER FAIL")
            return client.RC.USER_ERROR
        finally:
            # Cerrar la conexión
            sock.close()

    @staticmethod
    def unregister(user):
        """Método para darse de baja en el sistema"""
        # Conectarse al servidor
        sock = client.connectServer(client._server, client._port)
        if sock is None:
            print("UNREGISTER FAIL")
            return client.RC.USER_ERROR

        try:
            # Enviar cadena con la operación
            sock.sendall("UNREGISTER".encode()+b'\0')
            # Enviar el dateTime
            sock.sendall(str(client.dateTimeService()).encode() + b'\0')
            # Enviar el nombre de usuario
            sock.sendall(str(user).encode() + b'\0')
            # Recibir el resultado de la operación
            res = client.recvRes(sock)

            # Tratar el resultado de la operación
            if res == "0":
                print("UNREGISTER OK")
                return client.RC.OK
            elif res == "1":
                print("USERNAME DOES NOT EXIST")
                return client.RC.ERROR
            elif res == "2":
                print("UNREGISTER FAIL")
                return client.RC.USER_ERROR

        except Exception as e:
            print(f"Error durante la operación UNREGISTER: {e}")
            print("UNREGISTER FAIL")
            return client.RC.USER_ERROR
        finally:
            # Cerrar la conexión
            sock.close()

    # Auxiliar para encontrar un puerto libre
    @staticmethod
    def find_free_port():
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(('', 0))  # Le pide al sistema que encuentre un puerto libre
            return s.getsockname()[1]

    # Clase de hilos para esuchar en el cliente peticiones de otros clientes (peer to peer)
    class ClientServer(threading.Thread):
        def __init__(self, host, port):
            super().__init__()
            self.host = host
            self.port = port
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.bind((self.host, self.port))
            self.server_socket.listen(5)
            self.server_socket.settimeout(1)  # Timeout for the accept call
            self.running = True
            self.base_path = os.path.dirname(__file__)

        def run(self):
            while self.running:
                try:
                    client_socket, addr = self.server_socket.accept()
                    print(f"Accepted connection from {addr}")
                    self.handle_client(client_socket)
                except socket.timeout:
                    continue  # Handle timeout by simply looping back
                except OSError as e:
                    if not self.running:
                        break  # If running is False, this is an expected closure
                    else:
                        raise  # Otherwise, re-raise the exception as it's unexpected

        def stop(self):
            self.running = False
            self.server_socket.close()
            self.join()  # Wait for thread to finish

        def handle_client(self, client_socket):
            try:
                command = client_socket.recv(1024).decode().strip()
                if command.startswith("GET_FILE"):
                    _, filename = command.split(maxsplit=1)
                    filename = filename.strip('\x00').strip()
                    if self.check_file_exists(filename):
                        #GET_FILE OK (0)
                        client_socket.sendall("0".encode() + b'\0')
                        self.send_file(client_socket, filename)
                    else:
                        #GET_FILE FAIL / FILE NOT EXIST (1)
                        client_socket.sendall("1".encode() + b'\0')
                else:
                    #GET_FILE FAIL (2)
                    print("Command not found")
                    client_socket.sendall("2".encode() + b'\0')
            except Exception as e:
                #GET_FILE FAIL (2)
                print("Excepcion")
                print(e)
                client_socket.sendall("2".encode() + b'\0')
            finally:
                client_socket.close()

        def check_file_exists(self, filename):
            """Comprobar si el archivo existe en el directorio raíz"""
            return os.path.exists(filename)

        def send_file(self, client_socket, filename):
            try:
                with open(filename, 'rb') as f:
                    while True:
                        data = f.read(1024)
                        if not data:
                            break  # Si no hay más datos, termina el bucle
                        client_socket.sendall(data)
            except FileNotFoundError:
                #GET_FILE FAIL / FILE NOT EXIST (1)
                client_socket.sendall("1".encode() + b'\0')
            except Exception as e:
                #GET_FILE FAIL (2)
                client_socket.sendall("2".encode() + b'\0')

    @staticmethod
    def connect(user):
        """Método para conectarse al sistema"""
        # Encontrar un puerto libre
        port = client.find_free_port()
        client._thread = client.ClientServer('0.0.0.0', port)
        # Iniciar el hilo del servidor de escucha
        client._thread.start()
        # TRATAR ERRORES

        # Conectarse al servidor
        sock = client.connectServer(client._server, client._port)
        if sock is None:
            print("CONNECT FAIL")
            return client.RC.USER_ERROR

        try:
            # Enviar cadena con la operación
            sock.sendall("CONNECT".encode() + b'\0')
            # Enviar el dateTime
            sock.sendall(str(client.dateTimeService()).encode() + b'\0')
            # Enviar el nombre de usuario
            sock.sendall(str(user).encode() + b'\0')
            # Enviar la IP del cliente (ClientServer)
            sock.sendall(str(client._thread.host).encode() + b'\0')
            # Enviar el puerto de escucha del cliente (ClientServer)
            sock.sendall(str(client._thread.port).encode() + b'\0')    # el puerto de escucha no es client._port
            # Recibir el resultado de la operación
            res = client.recvRes(sock)

            # Tratar el resultado de la operación
            if res == "0":
                print("CONNECT OK")
                # Guardar el userName del cliente que se ha conectado
                client._userName = user
                client._lastConnectedUser = user
                # Hay que ejecutar client._thread.run() ????????????????
                return client.RC.OK
            elif res == "1":
                print("CONNECT FAIL, USER DOES NOT EXIST")
                # Hay que ejecutar client._thread.stop() ????????????????
                # Detener la ejecución del hilo de escucha del cliente
                client._thread.stop()
                return client.RC.ERROR
            elif res == "2":
                print("USER ALREADY CONNECTED")
                return client.RC.USER_ERROR
            elif res == "3":
                print("CONNECT FAIL")
                # Detener la ejecución del hilo de escucha del cliente
                client._thread.stop()
                return client.RC.USER_ERROR

        except Exception as e:
            print(f"Error durante la operación CONNECT: {e}")
            print("CONNECT FAIL")
            # Detener la ejecución del hilo de escucha del cliente
            client._thread.stop()
            return client.RC.USER_ERROR
        finally:
            # Cerrar la conexión
            sock.close()
        return client.RC.ERROR
    
    @staticmethod
    def disconnect(user):
        """Método para desconectarse del sistema"""
        # Conectarse al servidor
        sock = client.connectServer(client._server, client._port)
        if sock is None:
            print("DISCONNECT FAIL")
            return client.RC.USER_ERROR

        try:
            # Enviar cadena con la operación
            sock.sendall("DISCONNECT".encode() + b'\0')
            # Enviar el dateTime
            sock.sendall(str(client.dateTimeService()).encode() + b'\0')
            # Enviar el nombre de usuario
            sock.sendall(str(user).encode() + b'\0')
            # Recibir el resultado de la operación
            res = client.recvRes(sock)

            # Tratar el resultado de la operación
            if res == "0":
                print("DISCONNECT OK")
                # Vaciar el atributo que almacena el nombre del cliente conectado
                client._userName = None
                if client._thread is not None:
                    # Detener la ejecución del hilo de escucha del cliente
                    client._thread.stop()
                    print("Servicio thread cliente finalizado")
                # Detener el servidor de escucha
                return client.RC.OK
            elif res == "1":
                print("DISCONNECT FAIL / USER DOES NOT EXIST")
                if client._thread is not None:
                    # Detener la ejecución del hilo de escucha del cliente
                    client._thread.stop()
                return client.RC.ERROR
            elif res == "2":
                print("DISCONNECT FAIL / USER NOT CONNECTED")
                if client._thread is not None:
                    # Detener la ejecución del hilo de escucha del cliente
                    client._thread.stop()
                return client.RC.USER_ERROR
            elif res == "3":
                if client._thread is not None:
                    # Detener la ejecución del hilo de escucha del cliente
                    client._thread.stop()
                print("DISCONNECT FAIL")
                return client.RC.USER_ERROR

        except Exception as e:
            print(f"Error durante la operación DISCONNECT: {e}")
            print("DISCONNECT FAIL")
            if client._thread is not None:
                # Detener la ejecución del hilo de escucha del cliente
                client._thread.stop()
            return client.RC.USER_ERROR
        finally:
            # Cerrar la conexión
            sock.close()
        return client.RC.ERROR

    @staticmethod
    def publish(fileName, description):
        """Método para publicar contenidos"""
        # Validar que el nombre del archivo no contenga espacios en blanco
        if " " in fileName:
            print("PUBLISH FAIL: El nombre del archivo no puede contener espacios en blanco.")
            return client.RC.USER_ERROR
        # Validar la longitud del nombre del archivo
        if len(str(fileName).encode()) > 256:
            print("PUBLISH FAIL: El nombre del archivo excede los 256 bytes de longitud máxima.")
            return client.RC.USER_ERROR

        # Validar la longitud del nombre del archivo
        if len(str(description).encode()) > 256:
            print("PUBLISH FAIL: La descripción excede los 256 bytes de longitud máxima.")
            return client.RC.USER_ERROR

        # Conectarse al servidor
        sock = client.connectServer(client._server, client._port)
        if sock is None:
            print("PUBLISH FAIL")
            return client.RC.USER_ERROR

        try:
            # Enviar cadena con la operación
            sock.sendall("PUBLISH".encode() + b'\0')
            # Enviar el dateTime
            sock.sendall(str(client.dateTimeService()).encode() + b'\0')
            # Enviar el nombre de usuario que publica el fichero
            if client._userName is None:
                # Arreglo para recibir el error USER NOT CONNECTED
                if client._lastConnectedUser is None:
                    # Si todavía nadie se ha conectado, enviar el último registrado
                    sock.sendall(str(client._lastRegisteredUser).encode() + b'\0')
                else:
                    # Si no hay cliente conectado, enviar el último conectado
                    sock.sendall(str(client._lastConnectedUser).encode() + b'\0')
            else:
                # Si hay un cliente conectado, enviar su userName
                sock.sendall(str(client._userName).encode() + b'\0')
            # Enviar una cadena con el nombre del fichero
            sock.sendall(str(fileName).encode() + b'\0')
            # Enviar una cadena de caracteres con la descripcion del contenido
            sock.sendall(str(description).encode() + b'\0')
            # Recibir el resultado de la operación
            res = client.recvRes(sock)

            # Tratar el resultado de la operación
            if res == "0":
                print("PUBLISH OK")
                return client.RC.OK
            elif res == "1":
                print("PUBLISH FAIL, USER DOES NOT EXIST")
                return client.RC.ERROR
            elif res == "2":
                print("PUBLISH FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR
            elif res == "3":
                print("PUBLISH FAIL, CONTENT ALREADY PUBLISHED")
                return client.RC.USER_ERROR
            elif res == "4":
                print("PUBLISH FAIL")
                return client.RC.USER_ERROR

        except Exception as e:
            print(f"Error durante la operación PUBLISH: {e}")
            print("PUBLISH FAIL")
            return client.RC.USER_ERROR
        finally:
            # Cerrar la conexión
            sock.close()
        return client.RC.ERROR

    @staticmethod
    def delete(fileName):
        """Método para eliminar contenidos"""
        # Validar la longitud del nombre del archivo
        if len(str(fileName).encode()) > 256:
            print("PUBLISH FAIL: El nombre del archivo excede los 256 bytes de longitud máxima.")
            return client.RC.USER_ERROR

        # Conectarse al servidor
        sock = client.connectServer(client._server, client._port)
        if sock is None:
            print("DELETE FAIL")
            return client.RC.USER_ERROR

        try:
            # Enviar cadena con la operación
            sock.sendall("DELETE".encode()+b'\0')
            # Enviar el dateTime
            sock.sendall(str(client.dateTimeService()).encode() + b'\0')
            # Enviar el nombre de usuario que realiza la operación de borrado
            if client._userName is None:
                # Arreglo para recibir el error USER NOT CONNECTED
                if client._lastConnectedUser is None:
                    # Si todavía nadie se ha conectado, enviar el último registrado
                    sock.sendall(str(client._lastRegisteredUser).encode() + b'\0')
                else:
                    # Si no hay cliente conectado, enviar el último conectado
                    sock.sendall(str(client._lastConnectedUser).encode() + b'\0')
            else:
                # Si hay un cliente conectado, enviar su userName
                sock.sendall(str(client._userName).encode() + b'\0')
            # Enviar una cadena con el nombre del fichero
            sock.sendall(str(fileName).encode() + b'\0')
            # Recibir el resultado de la operación
            res = client.recvRes(sock)

            # Tratar el resultado de la operación
            if res == "0":
                print("DELETE OK")
                return client.RC.OK
            elif res == "1":
                print("DELETE FAIL, USER DOES NOT EXIST")
                return client.RC.ERROR
            elif res == "2":
                print("DELETE FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR
            elif res == "3":
                print("DELETE FAIL, CONTENT NOT PUBLISHED")
                return client.RC.USER_ERROR
            elif res == "4":
                print("DELETE FAIL")
                return client.RC.USER_ERROR

        except Exception as e:
            print(f"Error durante la operación DELETE: {e}")
            print("DELETE FAIL")
            return client.RC.USER_ERROR
        finally:
            # Cerrar la conexión
            sock.close()
        return client.RC.ERROR

    @staticmethod
    def listusers():
        """Método para conocer todos los usuarios conectados en el sistema"""
        # Conectarse al servidor
        sock = client.connectServer(client._server, client._port)
        if sock is None:
            print("LIST_USERS FAIL")
            return client.RC.USER_ERROR

        try:
            # Enviar cadena con la operación
            sock.sendall("LIST_USERS".encode() + b'\0')
            # Enviar el dateTime
            sock.sendall(str(client.dateTimeService()).encode() + b'\0')
            # Enviar el nombre de usuario que realiza la operación
            if client._userName is None:
                # Arreglo para recibir el error USER NOT CONNECTED
                if client._lastConnectedUser is None:
                    # Si todavía nadie se ha conectado, enviar el último registrado
                    sock.sendall(str(client._lastRegisteredUser).encode() + b'\0')
                else:
                    # Si no hay cliente conectado, enviar el último conectado
                    sock.sendall(str(client._lastConnectedUser).encode() + b'\0')
            else:
                # Si hay un cliente conectado, enviar su userName
                sock.sendall(str(client._userName).encode() + b'\0')
            # Recibir el resultado de la operación
            res = client.recvRes(sock)

            # Tratar el resultado de la operación
            if res == "0":
                print("LIST_USERS OK")
                # Recibir el número de usuarios cuya información se va a enviar
                num_users = int(client.recvRes(sock))
                print(f"Número de usuarios conectados: {num_users}")
                # Recibir y mostrar la información de cada usuario
                for _ in range(num_users):
                    username = client.recvRes(sock)
                    ip = client.recvRes(sock)
                    port = client.recvRes(sock)
                    print(f"{username} {ip} {port}")
                    client._users[username] = (ip, int(port))
                return client.RC.OK
            elif res == "1":
                print("LIST_USERS FAIL, USER DOES NOT EXIST")
                return client.RC.ERROR
            elif res == "2":
                print("LIST_USERS FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR
            elif res == "3":
                print("LIST_USERS FAIL")
                return client.RC.USER_ERROR

        except Exception as e:
            print(f"Error durante la operación LIST_USERS: {e}")
            print("LIST_USERS FAIL")
            return client.RC.USER_ERROR
        finally:
            # Cerrar la conexión
            sock.close()
        return client.RC.ERROR

    @staticmethod
    def listcontent(user_name):
        """Método para conocer el contenido publicado por otro usuario. """
        # Conectarse al servidor
        sock = client.connectServer(client._server, client._port)
        if sock is None:
            print("LIST CONTENT FAIL")
            return client.RC.USER_ERROR

        try:
            # Enviar cadena con la operación
            sock.sendall("LIST_CONTENT".encode() + b'\0')
            # Enviar el dateTime
            sock.sendall(str(client.dateTimeService()).encode() + b'\0')
            # Enviar el nombre de usuario que realiza la operación
            if client._userName is None:
                # Arreglo para recibir el error USER NOT CONNECTED
                if client._lastConnectedUser is None:
                    # Si todavía nadie se ha conectado, enviar el último registrado
                    sock.sendall(str(client._lastRegisteredUser).encode() + b'\0')
                else:
                    # Si no hay cliente conectado, enviar el último conectado
                    sock.sendall(str(client._lastConnectedUser).encode() + b'\0')
            else:
                # Si hay un cliente conectado, enviar su userName
                sock.sendall(str(client._userName).encode() + b'\0')
            # Enviar el nombre de usuario cuyo contenido se quiere conocer
            sock.sendall(str(user_name).encode() + b'\0')
            # Recibir el resultado de la operación
            res = client.recvRes(sock)

            # Tratar el resultado de la operación
            if res == "0":
                print("LIST_CONTENT OK")
                # Recibir el número de nombres de ficheros publicados
                num_files = int(client.recvRes(sock))
                print(f"Número de ficheros publicados por {user_name}: {num_files}")
                # Recibir y mostrar la información de cada fichero
                for _ in range(num_files):
                    file_name = client.recvRes(sock)
                    file_description = client.recvRes(sock)
                    print(f"{file_name} {file_description}")
                return client.RC.OK
            elif res == "1":
                print("LIST_CONTENT FAIL, USER DOES NOT EXIST")
                return client.RC.ERROR
            elif res == "2":
                print("LIST_CONTENT FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR
            elif res == "3":
                print("LIST_CONTENT FAIL, REMOTE USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            elif res == "4":
                print("LIST_CONTENT FAIL")
                return client.RC.USER_ERROR

        except Exception as e:
            print(f"Error durante la operación LIST_CONTENT: {e}")
            print("LIST_CONTENT FAIL")
            return client.RC.USER_ERROR
        finally:
            # Cerrar la conexión
            sock.close()
        return client.RC.ERROR

    @staticmethod
    def getfile(user,  remote_FileName,  local_FileName):
        """Método para enviar mensajes a otros usuarios registrados para descargar el contenido de un fichero. """

        if user not in client._users:
            print("GET_FILE FAIL / USER NOT FOUND")
            return client.RC.USER_ERROR

        # Obtener la dirección IP y el puerto del usuario remoto
        # print(client._users)
        remote_ip, remote_port = client._users[user]
        # Utilizar un bloque with para asegurar que el socket se cierra correctamente
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            try:
                sock.connect((remote_ip, remote_port))
            except Exception as e:
                print(f"GET_FILE FAIL: No se pudo conectar con {user}: {e}")
                return client.RC.USER_ERROR

            try:
                print(os.path.exists(remote_FileName))
                # Enviar cadena con la operación
                sock.sendall("GET_FILE ".encode() + b'\0')
                # Enviar el nombre del fichero que se desea descargar
                sock.sendall(str(remote_FileName).encode() + b'\0')
                # Enviar el dateTime
                sock.sendall(str(client.dateTimeService()).encode() + b'\0')
                # Agregar timeout para evitar bloqueos indefinidos
                sock.settimeout(10)
                # Recibir el resultado de la operación de otro cliente
                res = client.recvRes(sock)

                # Tratar el resultado de la operación
                if res == "0":
                    # Recibir datos del archivo
                    file_data = []
                    while True:
                        try:
                            data = sock.recv(1024)
                            if not data:
                                break  # Terminar la recepción si no hay más datos
                            file_data.append(data.decode())
                        except socket.timeout:
                            print("Timeout durante la recepción de datos.")
                            return client.RC.USER_ERROR

                    # Guardar el archivo como 'local_file'
                    with open(local_FileName, 'w') as file:
                        file.writelines(file_data)
                    print("GET_FILE OK")
                    return client.RC.OK
                elif res == "1":
                    print("GET_FILE FAIL / FILE NOT EXIST")
                    return client.RC.ERROR
                elif res == "2":
                    print("GET_FILE FAIL")
                    return client.RC.USER_ERROR
            except socket.error as e:
                print(f"Socket error: {e}")
                return client.RC.USER_ERROR
            except Exception as e:
                print(f"Error durante la operación GET_FILE: {e}")
                print("GET_FILE FAIL")
                return client.RC.USER_ERROR
            finally:
                # Cerrar la conexión
                sock.close()
            return client.RC.ERROR
    # *
    # **
    # * @brief Command interpreter for the client. It calls the protocol functions.
    @staticmethod
    def shell():

        while True:
            try:
                command = input("c> ")
                line = command.split(" ")
                if (len(line) > 0):

                    line[0] = line[0].upper()

                    if (line[0]=="REGISTER"):
                        if (len(line) == 2):
                            client.register(line[1])
                        else:
                            print("Syntax error. Usage: REGISTER <userName>")

                    elif(line[0]=="UNREGISTER"):
                        if (len(line) == 2):
                            client.unregister(line[1])
                        else:
                            print("Syntax error. Usage: UNREGISTER <userName>")

                    elif(line[0]=="CONNECT"):
                        if (len(line) == 2):
                            client.connect(line[1])
                        else:
                            print("Syntax error. Usage: CONNECT <userName>")
                    
                    elif(line[0]=="PUBLISH"):
                        if (len(line) >= 3):
                            #  Remove first two words
                            description = ' '.join(line[2:])
                            client.publish(line[1], description)
                        else:
                            print("Syntax error. Usage: PUBLISH <fileName> <description>")

                    elif(line[0]=="DELETE"):
                        if (len(line) == 2):
                            client.delete(line[1])
                        else:
                            print("Syntax error. Usage: DELETE <fileName>")

                    elif(line[0]=="LIST_USERS"):
                        if (len(line) == 1):
                            client.listusers()
                        else:
                            print("Syntax error. Use: LIST_USERS")

                    elif(line[0]=="LIST_CONTENT"):
                        if (len(line) == 2):
                            client.listcontent(line[1])
                        else:
                            print("Syntax error. Usage: LIST_CONTENT <userName>")

                    elif(line[0]=="DISCONNECT"):
                        if (len(line) == 2):
                            client.disconnect(line[1])
                        else:
                            print("Syntax error. Usage: DISCONNECT <userName>")

                    elif(line[0]=="GET_FILE"):
                        if (len(line) == 4):
                            client.getfile(line[1], line[2], line[3])
                        else:
                            print("Syntax error. Usage: GET_FILE <userName> <remote_fileName> <local_fileName>")

                    elif(line[0]=="QUIT"):
                        if (len(line) == 1):
                            # Desconectar al cliente del sistema si está conectado
                            if client._userName is not None:
                                client.disconnect(client._userName)
                            break
                        else:
                            print("Syntax error. Use: QUIT")
                    else:
                        print("Error: command " + line[0] + " not valid.")
            except Exception as e:
                print("Exception: " + str(e))

    @staticmethod
    def dateTimeService():
        # URL del servicio web que devuelve la fecha y hora
        datetime_service_url = 'http://localhost:8000/?wsdl'
        datetime_client = ZeepClient(datetime_service_url)
        current_datetime = datetime_client.service.get_current_datetime()
        print(current_datetime)
        return current_datetime

    # *
    # * @brief Prints program usage'''

    @staticmethod
    def usage():
        print("Usage: python3 client.py -s <server> -p <port>")

    # *
    # * @brief Parses program execution arguments
    @staticmethod
    def parseArguments(argv):
        parser = argparse.ArgumentParser()
        parser.add_argument('-s', type=str, required=True, help='Server IP')
        parser.add_argument('-p', type=int, required=True, help='Server Port')
        args = parser.parse_args()

        if (args.s is None):
            parser.error("Usage: python3 client.py -s <server> -p <port>")
            return False

        if ((args.p < 1024) or (args.p > 65535)):
            parser.error("Error: Port must be in the range 1024 <= port <= 65535")
            return False
        
        client._server = args.s
        client._port = args.p

        return True

    # ******************** MAIN *********************
    @staticmethod
    def main(argv):
        if (not client.parseArguments(argv)) :
            client.usage()
            return

        #  Write code here
        client.shell()
        print("+++ FINISHED +++")

if __name__ == "__main__":
    client.main([])