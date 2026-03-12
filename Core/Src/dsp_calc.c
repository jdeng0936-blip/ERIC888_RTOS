/**
 * @file    dsp_calc.c
 * @brief   DSP calculation module implementation (v2.0 — Hardened)
 *          Uses CMSIS-DSP for hardware-accelerated float math on Cortex-M4 FPU
 *
 *          Processing chain per 512-sample block (~20ms @ 25.6kHz):
 *          1. Convert int16 → float32 → millivolts
 *          2. arm_rms_f32() → RMS value
 *          3. arm_rfft_fast_f32() → frequency spectrum
 *          4. Fundamental extraction (50Hz bin) + THD calculation
 *          5. Protection threshold check → trip flag (with debounce)
 *
 *  v2.0 fixes:
 *    - FFT bin indexing: arm_rfft_fast_f32 output[0..1] = {DC, Nyquist},
 *      real bins start at output[2]. Fixed magnitude extraction.
 *    - Added UV (undervoltage) and OC (overcurrent) protection checks.
 *    - Added trip debounce: consecutive N blocks must exceed threshold.
 *    - Added safety comment: arm_rfft_fast_f32 destroys input buffer.
 */
#include "dsp_calc.h"
#include <string.h>

/* RFFT instance for 512-point FFT */
static arm_rfft_fast_instance_f32 s_rfft;

/* Working buffers (reused per channel) */
static float32_t s_input_f32[DSP_BLOCK_SIZE];
static float32_t s_fft_output[DSP_BLOCK_SIZE];   /* complex output */
static float32_t s_fft_mag[DSP_BLOCK_SIZE / 2];  /* magnitude spectrum */

/* Results and config */
static DSP_Results s_results;
static DSP_Protection_Config s_prot = {
  .ov_threshold_mv  = 11000.0f,  /* ±11V peak = overvoltage (±10V range) */
  .uv_threshold_mv  = 100.0f,    /* minimum expected voltage */
  .oc_threshold_mv  = 3000.0f,   /* overcurrent threshold */
  .thd_threshold_pct = 10.0f,    /* 10% THD limit */
  .trip_debounce     = 3,        /* 3 blocks ≈ 60ms sustained fault to trip */
};

/* AD7606 conversion: raw int16 → millivolts (±10V range, 16-bit) */
/* Scale: 10000mV / 32768 = 0.30518 mV/LSB */
#define AD7606_MV_PER_LSB  0.30518f

/*
 * FFT bin mapping for arm_rfft_fast_f32 output:
 *
 *   output[0] = DC component (real only)
 *   output[1] = Nyquist component (real only)
 *   output[2], output[3] = bin 1 (real, imag) → 50Hz
 *   output[4], output[5] = bin 2 (real, imag) → 100Hz
 *   ...
 *
 * So bin k (k >= 1) is at output[2*k] + j*output[2*k+1]
 * We compute magnitude starting from output[2] for bins 1..N/2-1
 *
 * bin_freq = k * Fs / N = k * 25600 / 512 = k * 50 Hz
 * bin 1 = 50Hz, bin 2 = 100Hz, ...
 */
#define FFT_BIN_50HZ  1

void DSP_Init(void)
{
  /* Initialize 512-point RFFT */
  arm_rfft_fast_init_f32(&s_rfft, DSP_BLOCK_SIZE);
  memset(&s_results, 0, sizeof(s_results));
}

void DSP_SetProtection(const DSP_Protection_Config *cfg)
{
  s_prot = *cfg;
}

const DSP_Results* DSP_ProcessBatch(const Eric888_ADC_Data *batch)
{
  s_results.fault_flags = 0;
  s_results.trip_requested = 0;

  for (int ch = 0; ch < DSP_NUM_CH; ch++) {
    /* Step 1: Extract channel data and convert to millivolts */
    for (int i = 0; i < DSP_BLOCK_SIZE; i++) {
      s_input_f32[i] = (float32_t)batch[i].ch[ch] * AD7606_MV_PER_LSB;
    }

    /* Step 2: RMS calculation (hardware FPU accelerated) */
    arm_rms_f32(s_input_f32, DSP_BLOCK_SIZE, &s_results.rms[ch]);

    /* Step 3: Peak detection */
    float32_t max_val, min_val;
    uint32_t max_idx, min_idx;
    arm_max_f32(s_input_f32, DSP_BLOCK_SIZE, &max_val, &max_idx);
    arm_min_f32(s_input_f32, DSP_BLOCK_SIZE, &min_val, &min_idx);
    s_results.peak[ch] = (max_val > -min_val) ? max_val : -min_val;

    /*
     * Step 4: FFT → magnitude spectrum
     *
     * WARNING: arm_rfft_fast_f32() DESTROYS the input buffer (s_input_f32).
     * RMS and Peak MUST be computed before this call.
     * Do NOT reorder Steps 2/3 to after Step 4.
     */
    arm_rfft_fast_f32(&s_rfft, s_input_f32, s_fft_output, 0);

    /*
     * Compute magnitude for bins 1..N/2-1
     * Skip output[0..1] (DC and Nyquist are packed as two reals, not a complex pair)
     * Process from output[2] onward as complex pairs
     */
    arm_cmplx_mag_f32(&s_fft_output[2], &s_fft_mag[1], DSP_BLOCK_SIZE / 2 - 1);

    /* DC magnitude is just |output[0]| */
    s_fft_mag[0] = (s_fft_output[0] >= 0.0f) ? s_fft_output[0] : -s_fft_output[0];

    /* Step 5: Extract 50Hz fundamental */
    s_results.fundamental[ch] = s_fft_mag[FFT_BIN_50HZ];

    /* Step 6: THD = sqrt(sum(harmonics^2)) / fundamental * 100% */
    /* Harmonics: 2nd(100Hz)=bin2, 3rd(150Hz)=bin3, ... up to 10th */
    if (s_results.fundamental[ch] > 1.0f) {
      float32_t harm_sum_sq = 0.0f;
      for (int h = 2; h <= 10; h++) {
        int bin = FFT_BIN_50HZ * h;
        if (bin < DSP_BLOCK_SIZE / 2) {
          harm_sum_sq += s_fft_mag[bin] * s_fft_mag[bin];
        }
      }
      float32_t harm_rms;
      arm_sqrt_f32(harm_sum_sq, &harm_rms);
      s_results.thd[ch] = (harm_rms / s_results.fundamental[ch]) * 100.0f;
    } else {
      s_results.thd[ch] = 0.0f;
    }

    /* Step 7: Protection threshold checks with debounce */
    uint8_t ch_fault = 0;

    /* 7a: Overvoltage (RMS exceeds OV threshold) */
    if (s_results.rms[ch] > s_prot.ov_threshold_mv) {
      ch_fault = 1;
    }

    /* 7b: Undervoltage (RMS below UV threshold — supply lost or sensor fault) */
    if (s_results.rms[ch] < s_prot.uv_threshold_mv && s_results.rms[ch] > 0.01f) {
      /* Only flag UV if signal is present but weak (> 0.01mV noise floor) */
      ch_fault = 1;
    }

    /* 7c: Overcurrent (channels 4-7 are current channels) */
    if (ch >= 4 && s_results.peak[ch] > s_prot.oc_threshold_mv) {
      ch_fault = 1;
    }

    /* 7d: THD excessive (voltage channels 0-3) */
    if (ch < 4 && s_results.thd[ch] > s_prot.thd_threshold_pct) {
      s_results.fault_flags |= (1 << ch);  /* THD = warning only, no trip */
    }

    /* Debounce logic: trip only after N consecutive fault blocks */
    if (ch_fault) {
      s_results.fault_flags |= (1 << ch);
      if (s_results.fault_count[ch] < 255) {
        s_results.fault_count[ch]++;
      }
      if (s_results.fault_count[ch] >= s_prot.trip_debounce) {
        s_results.trip_requested = 1;
      }
    } else {
      /* Reset debounce counter on healthy block */
      s_results.fault_count[ch] = 0;
    }
  }

  return &s_results;
}

const DSP_Results* DSP_GetResults(void)
{
  return &s_results;
}
