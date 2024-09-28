#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define N 205 //Block size
#define PI 3.14159265358979

float coeff;
int fix_coeff;
int Q1;
int Q2;
double sine;
double cosine;

int FIXED_POINT_16 = 16;
int ONE_16 = 1 << FIXED_POINT_16;

int FIXED_POINT_30 = 30;
int ONE_30 = 1 << FIXED_POINT_30;

int toFix(float val, int ONE) {
    return (int)(val * ONE);
}

float floatVal(int fix, int ONE) {
    return ((float)fix) / ONE;
}

int intVal(int fix, int FIXED_POINT) {
    return fix >> FIXED_POINT;
}

int mul(int a, int b, int FIXED_POINT_mixed) {
    return (int)((long long int)a * (long long int)b >> FIXED_POINT_mixed);
}

/* Call this routine before every "block" (size=N) of samples. */
void ResetGoertzel(void) {
    Q2 = 0;
    Q1 = 0;
}

/* Call this once, to precompute the constants. */
void InitGoertzel(int samplingRate, double targetFrequency) {
    int k;
    double floatN = (double)N;
    k = (int)(0.5 + ((floatN * targetFrequency) / samplingRate));
    double omega = (2.0 * PI * k) / floatN;
    sine = sin(omega);
    cosine = cos(omega);
    coeff = 2.0 * cosine;
    fix_coeff = toFix(coeff, ONE_30);
}

/* Process a single sample, dynamically handling 8-bit and 16-bit samples. */
void ProcessSample(unsigned char sample8, unsigned short sample16, int bits) {
    int Q0;
    int sample;

    if (bits == 8) {
        sample = toFix(sample8, ONE_16);
    } else if (bits == 16) {
        sample = toFix(sample16, ONE_16);
    } else {
        printf("Unsupported sample bit size: %d\n", bits);
        exit(1);
    }

    Q0 = mul(fix_coeff, Q1, 30) - Q2 + sample;
    Q2 = Q1;
    Q1 = Q0;
}

/* Optimized Goertzel */
float GetMagnitudeSquared(void) {
    float result;
    result = floatVal(Q1, ONE_16) * floatVal(Q1, ONE_16) + floatVal(Q2, ONE_16) * floatVal(Q2, ONE_16) - floatVal(Q1, ONE_16) * floatVal(Q2, ONE_16) * coeff;
    return result;
}

/* Synthesize some test data at a given frequency. */
void Generate(double frequency, int samplingRate, const char* outputFile, int duration, int bits) {
    int index, totalSamples = samplingRate * duration;
    double step = frequency * ((2.0 * PI) / samplingRate);
    FILE *file = fopen(outputFile, "wb");
    if (!file) {
        perror("Failed to open output file");
        exit(EXIT_FAILURE);
    }

    /* Generate the test data */
    for (index = 0; index < totalSamples; index++) {
        if (bits == 8) {
            unsigned char sample = (unsigned char)(100.0 * sin(index * step) + 100.0);
            fwrite(&sample, sizeof(unsigned char), 1, file);
        } else if (bits == 16) {
            unsigned short sample = (unsigned short)(10000.0 * sin(index * step) + 10000.0);
            fwrite(&sample, sizeof(unsigned short), 1, file);
        } else {
            printf("Unsupported bit size: %d\n", bits);
            fclose(file);
            exit(1);
        }
    }
    fclose(file);
}

/* Detect frequency from the input file */
void DetectFrequency(const char* inputFile, int samplingRate, int bits) {
    FILE *file = fopen(inputFile, "rb");
    if (!file) {
        perror("Failed to open input file");
        exit(EXIT_FAILURE);
    }

    ResetGoertzel();
    if (bits == 8) {
        unsigned char sample;
        while (fread(&sample, sizeof(unsigned char), 1, file)) {
            ProcessSample(sample, 0, 8);
        }
    } else if (bits == 16) {
        unsigned short sample;
        while (fread(&sample, sizeof(unsigned short), 1, file)) {
            ProcessSample(0, sample, 16);
        }
    } else {
        printf("Unsupported sample bit size: %d\n", bits);
        fclose(file);
        exit(1);
    }

    float magnitudeSquared = GetMagnitudeSquared();
    printf("Relative magnitude squared = %f\n", magnitudeSquared);
    fclose(file);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage:\n");
        printf("  ./goertzel output_file sampling_rate frequency bits duration # generate audio\n");
        printf("  ./goertzel input_file sampling_rate bits # detect frequency\n");
        return 1;
    }

     
    const char *file = argv[1];
    int samplingRate = atoi(argv[2]);
    double frequency = atof(argv[3]);
    int bits = atoi(argv[4]); 
    if (argc == 6) { // Generate audio
        int duration = atoi(argv[5]);
        InitGoertzel(samplingRate, frequency);
        Generate(frequency, samplingRate, file, duration, bits);
    } else { // Detect frequency
        InitGoertzel(samplingRate, frequency);
        DetectFrequency(file, samplingRate, bits);
    }

    return 0;
}

