// ===== Configuración por Bluetooth (BLE) con NimBLE =====
// Solo se activa en modo configuración. Alexa NO usa BLE: va por Wi-Fi + SinricPro.
#pragma once

void bleIniciar();    // crea el servicio GATT y anuncia "Raulcito-BLE"
void bleLoop();       // prueba el Wi-Fi tras escribir GUARDAR = 1
bool bleOcupado();    // hay un celular conectado o una prueba en curso
