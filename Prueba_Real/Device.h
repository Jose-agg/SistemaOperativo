#ifndef DISPOSITIVO_H
#define DISPOSITIVO_H

enum DeviceStatus {FREE, BUSY, ABORTED}; // Examen V5 estado nuevo

typedef struct {
	int info;
	int IOEndTick;
} IODATA;

// Prototipos de las funciones
void Device_UpdateStatus();
void Device_PrintIOResult();
void Device_StartIO(int);
int Device_GetStatus();
void Device_Initialize(char *, int);
void Device_Reset(); // Examen V5

#endif
