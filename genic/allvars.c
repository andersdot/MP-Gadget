#include <stdio.h>
#include <stdint.h>
#include "genic/allvars.h"

int Nmesh, Ngrid;

double Box;
int ProduceGas;
int Seed;
int UnitaryAmplitude;
int UsePeculiarVelocity;
int NumPart;
int InvertPhase;
double MaxMemSizePerNode;

struct part_data *P;

double InitTime;

char * OutputDir, * FileBase;
int NumWriters;
int  NumFiles;

int ThisTask, NTask;

double UnitTime_in_s, UnitLength_in_cm, UnitMass_in_g, UnitVelocity_in_cm_per_s;
double G;
Cosmology CP;
struct power_params PowerP;
