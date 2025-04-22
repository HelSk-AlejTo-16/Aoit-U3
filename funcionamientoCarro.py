from machine import Pin, UART
import time

# Pines para controlar motores en pares
in1 = Pin(14, Pin.OUT)  # Motores izquierdos
in2 = Pin(27, Pin.OUT)
in3 = Pin(26, Pin.OUT)  # Motores derechos
in4 = Pin(25, Pin.OUT)

# UART2 para Bluetooth HC-06 (9600 baudios)
uart = UART(2, baudrate=9600, tx=17, rx=16)

def parar():
    in1.off()
    in2.off()
    in3.off()
    in4.off()

def adelante():
    in1.on()
    in2.off()
    in3.off()
    in4.on()

def atras():
    in1.off()
    in2.on()
    in3.on()
    in4.off()

def izquierda():
    in1.off()
    in2.on()
    in3.off()
    in4.on()

def derecha():
    in1.on()
    in2.off()
    in3.on()
    in4.off()

def adelante_izquierda():
    in1.on()   # Motor izquierdo retrocede
    in2.off()
    in3.off()
    in4.off()  # Motor derecho apagado

def adelante_derecha():
    in1.off()
    in2.off()  # Motor izquierdo apagado
    in3.off()
    in4.on()   # Motor derecho retrocede

def atras_izquierda():
    in1.off()
    in2.on()   # Motor izquierdo avanza
    in3.off()
    in4.off()  # Motor derecho apagado

def atras_derecha():
    in1.off()
    in2.off()  # Motor izquierdo apagado
    in3.on()
    in4.off()  # Motor derecho avanza

print("Esperando comandos por Bluetooth...")

while True:
    if uart.any():
        comando = uart.readline().decode('utf-8').strip().lower()
        print("Comando recibido:", comando)

        if comando == "f":
            adelante()
        elif comando == "b":
            atras()
        elif comando == "l":
            izquierda()
        elif comando == "r":
            derecha()
        elif comando == "s":
            parar()
        elif comando == "g":
            adelante_izquierda()
        elif comando == "i":
            adelante_derecha()
        elif comando == "h":
            atras_izquierda()
        elif comando == "j":
            atras_derecha()
        else:
            print("Comando no reconocido")
            parar()


