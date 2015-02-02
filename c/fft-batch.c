// Batch export of FFT data as images.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>

#include <libhackrf/hackrf.h>
#include <fftw3.h>

#include "easypng.h"

const int FFT_SIZE = 2048;
const int FFT_HISTORY_SIZE = 200;
const int SAMPLES_SIZE = 131072;
const int FREQUENCY_START = 200.0e6;
const int FREQUENCY_END = 210.0e6;
const int FREQUENCY_STEP = 1e6;
const int SAMPLE_RATE = 10e6;
const int SAMPLE_BLOCKS_TO_SKIP = 10;

int frequency = FREQUENCY_START;

fftw_complex *fft_in;
fftw_complex *fft_out;
fftw_complex *fft_history;
int history_rows = 0;
fftw_plan fft_plan;
hackrf_device *device;
double freq_mhz = 88;
int skip = SAMPLE_BLOCKS_TO_SKIP;

// HackRF /////////////////////////////////////////////////////////////////////

static void hackrf_check_status(int status, const char *message, const char *file, int line) {
    if (status != 0) {
        fprintf(stderr, "NRF HackRF fatal error: %s\n", message);
        if (device != NULL) {
            hackrf_close(device);
        }
        hackrf_exit();
        exit(EXIT_FAILURE);
    }
}

#define HACKRF_CHECK_STATUS(status, message) hackrf_check_status(status, message, __FILE__, __LINE__)

int receive_sample_block(hackrf_transfer *transfer) {
    if (skip > 0) {
        skip--;
        return 0;
    }
    if (history_rows >= FFT_HISTORY_SIZE) return 0;
    int ii = 0;
    for (int i = 0; i < SAMPLES_SIZE; i += 2) {
        fft_in[ii][0] = powf(-1, ii) * transfer->buffer[i] / 255.0;
        fft_in[ii][1] = powf(-1, ii) * transfer->buffer[i + 1] / 255.0;
        ii++;
    }
    fftw_execute(fft_plan);

    // Move one line down.
    memcpy(fft_history + FFT_SIZE, fft_history, FFT_SIZE * (FFT_HISTORY_SIZE - 1) * sizeof(fftw_complex));
    // Set the first line.
    memcpy(fft_history, fft_out, FFT_SIZE * sizeof(fftw_complex));

    history_rows++;
    printf("Rows: %d\n", history_rows);
    if (history_rows >= FFT_HISTORY_SIZE) {
        // Write image.
        uint8_t *buffer = calloc(FFT_SIZE * FFT_HISTORY_SIZE, sizeof(uint8_t));
        for (int y = 0; y < FFT_HISTORY_SIZE; y++) {
            for (int x = 0; x < FFT_SIZE; x++) {
                double ci = fft_history[y * FFT_SIZE + x][0];
                double cq = fft_history[y * FFT_SIZE + x][1];
                uint8_t v = sqrt(ci * ci + cq * cq) * 2;
                buffer[y * FFT_SIZE + x] = v;
            }
        }
        char file_name[100];
        snprintf(file_name, 100, "fft-%.4f.png", frequency / 1.0e6);
        write_gray_png(file_name, FFT_SIZE, FFT_HISTORY_SIZE, buffer);
        free(buffer);
    }

    return 0;
}

static void setup_hackrf() {
    int status;

    status = hackrf_init();
    HACKRF_CHECK_STATUS(status, "hackrf_init");

    status = hackrf_open(&device);
    HACKRF_CHECK_STATUS(status, "hackrf_open");

    status = hackrf_set_freq(device, frequency);
    HACKRF_CHECK_STATUS(status, "hackrf_set_freq");

    status = hackrf_set_sample_rate(device, SAMPLE_RATE);
    HACKRF_CHECK_STATUS(status, "hackrf_set_sample_rate");

    status = hackrf_set_amp_enable(device, 0);
    HACKRF_CHECK_STATUS(status, "hackrf_set_amp_enable");

    status = hackrf_set_lna_gain(device, 32);
    HACKRF_CHECK_STATUS(status, "hackrf_set_lna_gain");

    status = hackrf_set_vga_gain(device, 30);
    HACKRF_CHECK_STATUS(status, "hackrf_set_lna_gain");

    status = hackrf_start_rx(device, receive_sample_block, NULL);
    HACKRF_CHECK_STATUS(status, "hackrf_start_rx");
}

static void teardown_hackrf() {
    hackrf_stop_rx(device);
    hackrf_close(device);
    hackrf_exit();
}

// static void set_frequency() {
//     freq_mhz = round(freq_mhz * 10.0) / 10.0;
//     printf("Seting freq to %f MHz.\n", freq_mhz);
//     int status = hackrf_set_freq(device, freq_mhz * 1e6);
//     HACKRF_CHECK_STATUS(status, "hackrf_set_freq");
// }

// FFTW /////////////////////////////////////////////////////////////////////

static void setup_fftw() {
    fft_in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * SAMPLES_SIZE);
    fft_out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * SAMPLES_SIZE);
    fft_history = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * FFT_SIZE * FFT_HISTORY_SIZE);
    fft_plan = fftw_plan_dft_1d(FFT_SIZE, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);
}

static void teardown_fftw() {
    fftw_destroy_plan(fft_plan);
    fftw_free(fft_in);
    fftw_free(fft_out);
    fftw_free(fft_history);
}

// Main /////////////////////////////////////////////////////////////////////

int main(int argc, char **argv) {
    setup_fftw();
    setup_hackrf();

    while (frequency <= FREQUENCY_END) {
        while (history_rows < FFT_HISTORY_SIZE) {
            sleep(1);
        }

        frequency = frequency + FREQUENCY_STEP;
        if (frequency > FREQUENCY_END) {
            exit(0);
        }

        int status = hackrf_set_freq(device, frequency);
        HACKRF_CHECK_STATUS(status, "hackrf_set_freq");

        skip = SAMPLE_BLOCKS_TO_SKIP;
        history_rows = 0;
        printf("Frequency: %.4f\n", frequency / 1.0e6);
    }

    teardown_hackrf();
    teardown_fftw();

    return 0;
}
