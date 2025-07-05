#ifndef _MATH
#define _MATH

double frexp(double arg, int* exp);
double sqrt(double arg);
double fabs(double arg);
double floor(double arg);
double ceil(double arg);
double fmod(double x, double y);
double pow(double base, double exponent);
double acos(double arg);
double cos(double arg);
double round(double arg);

static inline float roundf(float arg)
{
	return (float)round((double)arg);
}

#endif _MATH
