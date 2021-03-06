/*Tests for the cosmology module, ported from N-GenIC.*/

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <gsl/gsl_sf_hyperg.h>
#include "physconst.h"
#include "cosmology.h"
#include "stub.h"

void setup_cosmology(Cosmology * CP, double Omega0, double OmegaBaryon, double H0)
{
    CP->CMBTemperature = 2.7255;
    CP->Omega0 = Omega0;
    CP->OmegaLambda = 1- CP->Omega0;
    CP->OmegaBaryon = OmegaBaryon;
    CP->HubbleParam = H0;
    CP->MasslessNeutrinosOn = 1;
    CP->RadiationOn = 1;

    /*Default value for L=kpc v=km/s*/
    double UnitTime_in_s = 3.08568e+16;
    /*Should be 0.1*/
    CP->Hubble = HUBBLE * UnitTime_in_s;
    /*Do the main cosmology initialisation*/
    init_cosmology(CP);
}

static inline double radgrow(double aa, double omegar) {
    return omegar + 1.5 * 1. * aa; 
}

//Omega_L + Omega_M = 1 => D+ ~ Gauss hypergeometric function
static inline double growth(double aa, double omegam) {
    double omegal = 1-omegam;
    return aa * gsl_sf_hyperg_2F1(1./3, 1, 11./6, -omegal/omegam*pow(aa,3));
}

static void test_cosmology(void ** state)
{
    Cosmology CP;
    //Check that we get the right scalings for total matter domination.
    //Cosmology(double HubbleParam, double Omega, double OmegaLambda, double MNu, int Hierarchy, bool NoRadiation)
    setup_cosmology(&CP, 1., 0.0455, 0.7);
    /*Check some of the setup worked*/
    assert_true(CP.OmegaK < 1e-5);
    assert_true(fabs(CP.OmegaG/5.045e-5 - 1) < 2e-3);
    /*Check the hubble function is sane*/
    CP.RadiationOn = 0;
    assert_true(fabs(hubble_function(1) - CP.Hubble) < 1e-5);
    CP.RadiationOn = 1;
    assert_true(fabs(hubble_function(1) - CP.Hubble* sqrt(1+CP.OmegaNu0+CP.OmegaG)) < 1e-7);

    assert_true(fabs(CP.Hubble - 0.1) < 1e-6);
    assert_true((hubble_function(1) - CP.Hubble) < 1e-5);
    assert_true(fabs(hubble_function(0.1) - hubble_function(1)/pow(0.1,3/2.)) < 1e-2);
    assert_true(fabs(GrowthFactor(0.5,1.)/0.5 -1) < 2e-4);
    //Check that massless neutrinos work
    assert_true(fabs(CP.OmegaNu0 - CP.OmegaG*7./8.*pow(pow(4/11.,1/3.)*1.00328,4)*3) < 2e-5);
    //Check that the velocity correction d ln D1/d lna is constant
    assert_true(fabs(1.0 - F_Omega(1.5)) < 1e-1);
    assert_true(fabs(1.0 - F_Omega(2)) < 1e-2);
    //Check radiation against exact solution from gr-qc/0504089
    double omegar = CP.OmegaG + CP.OmegaNu0;
    assert_true(fabs(1/GrowthFactor(0.05,1.) - radgrow(1., omegar)/radgrow(0.05, omegar))< 1e-3);
    assert_true(fabs(GrowthFactor(0.01,0.001) - radgrow(0.01, omegar)/radgrow(0.001, omegar))< 1e-3);

    //Check against exact solutions from gr-qc/0504089: No radiation!
    //Note that the GSL hyperg needs the last argument to be < 1
    double omegam = 0.5;
    setup_cosmology(&CP, omegam, 0.0455, 0.7);
    CP.RadiationOn = 0;
    //Check growth factor during matter domination
    assert_true(fabs(1/GrowthFactor(0.5, 1.) - growth(1., omegam)/growth(0.5, omegam)) < 1e-3);
    assert_true(fabs(GrowthFactor(0.3, 0.15) - growth(0.3, omegam)/growth(0.15, omegam)) < 1e-3);
    assert_true(fabs(1/GrowthFactor(0.01, 1.) - growth(1, omegam)/growth(0.01, omegam)) < 1e-3);
    assert_true(fabs(0.01*log(GrowthFactor(0.01+1e-5,0.01-1e-5))/2e-5 -  F_Omega(0.01)) < 1e-3);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_cosmology),
    };
    return cmocka_run_group_tests_mpi(tests, NULL, NULL);
}
