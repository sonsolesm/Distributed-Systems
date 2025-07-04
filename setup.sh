#!/bin/bash

# Compilar los archivos C
echo "Compilando archivos C..."
make

# Instalar las dependencias de Python
echo "Instalando dependencias de Python..."
sudo apt-get install python3-pip
python3 -m pip install --upgrade pip setuptools
python3 -m pip install zeep
python3 -m pip install spyne

echo "Compilación e instalación completadas."
