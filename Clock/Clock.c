#include "Clock.h"
#include "Processor.h"

int tics=0;

void Processor_RaiseInterrupt(int);

void Clock_Update() {

	tics++;
	if(tics%INTERVALBETWEENINTERRUPS==0) Processor_RaiseInterrupt(CLOCKINT_BIT);
}


int Clock_GetTime() {

	return tics;
}
