/**
 * @file    dsp_calc.h
 * @brief   DSP calculation module using CMSIS-DSP (ARM Cortex-M4 FPU)
 *          RMS, FFT, THD, and protection threshold checking
 */
#ifndef DSP_CALC_H
#define DSP_CALC_H

#include "arm_math.h"
#include "eric888_spi_protocol.h"

/* FFT/RMS block size: 512 samples = 10 cycles @ 50Hz/25.6kHz */
#define DSP_BLOCK_SIZE  ERIC888_BATCH_SIZE  /* 512 */
#define DSP_NUM_CH      ERIC888_ADC_CHANNELS /* 8 */
#define DSP_SAMPLE_RATE 25600.0f

/* Protection thresholds (configurable at runtime) */
typedef struct {
  float32_t ov_threshold_mv;   /* Overvoltage trip (millivolts RMS) */
  float32_t uv_threshold_mv;   /* Undervoltage trip */
  float32_t oc_threshold_mv;   /* Overcurrent trip (ADC millivolts) */
  float32_t thd_threshold_pct; /* THD limit (percent) */
} DSP_Protection_Config;

/* Calculation results (updated every 512-sample block = ~20ms) */
typedef struct {
  float32_t rms[DSP_NUM_CH];       /* RMS per channel (millivolts) */
  float32_t thd[DSP_NUM_CH];       /* THD per channel (percent) */
  float32_t fundamental[DSP_NUM_CH]; /* 50Hz fundamental magnitude */
  float32_t peak[DSP_NUM_CH];      /* Peak value in block */
  uint8_t   fault_flags;           /* bit0~7: channel fault flags */
  uint8_t   trip_requested;        /* 1 = protection trip needed */
} DSP_Results;

/**
 * @brief  Initialize DSP module (RFFT instance, protection config)
 */
void DSP_Init(void);

/**
 * @brief  Set protection thresholds
 */
void DSP_SetProtection(const DSP_Protection_Config *cfg);

/**
 * @brief  Process a batch of 512 ADC samples
 *         Computes RMS, FFT, THD for each channel
 * @param  batch: pointer to 512-sample array
 * @retval pointer to results (valid until next call)
 */
const DSP_Results* DSP_ProcessBatch(const Eric888_ADC_Data *batch);

/**
 * @brief  Get latest DSP results
 */
const DSP_Results* DSP_GetResults(void);

#endif /* DSP_CALC_H */
