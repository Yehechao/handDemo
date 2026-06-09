// Copyright (c) 2026 Matrix 墨现科技. All rights reserved.

#ifndef MATRIX_HAND_SDK_H
#define MATRIX_HAND_SDK_H

#include <stdint.h>

#define MATRIX_HAND_CHANNEL_COUNT 19
#define MATRIX_HAND_MAX_INVALID_CHANNEL_COUNT 19

#if defined(_WIN32) && defined(MATRIX_HAND_SDK_EXPORTS)
#define MATRIX_HAND_API __declspec(dllexport)
#elif defined(_WIN32)
#define MATRIX_HAND_API __declspec(dllimport)
#else
#define MATRIX_HAND_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* MatrixHandHandle;

typedef enum MatrixHandStatus {
    MATRIX_HAND_OK = 0,
    MATRIX_HAND_ERROR_NULL_HANDLE = 1,
    MATRIX_HAND_ERROR_NULL_POINTER = 2,
    MATRIX_HAND_ERROR_INVALID_CONFIG = 3,
    MATRIX_HAND_ERROR_BAD_STATE = 4,
    MATRIX_HAND_ERROR_NOT_READY = 5,
    MATRIX_HAND_ERROR_CALIBRATION_NOT_ACTIVE = 6,
    MATRIX_HAND_ERROR_CALIBRATION_EMPTY = 7,
    MATRIX_HAND_ERROR_CALIBRATION_BAD_STAGE = 8,
    MATRIX_HAND_ERROR_XTALK_FIT_FAILED = 9
} MatrixHandStatus;

typedef enum MatrixHandCalibrationStage {
    MATRIX_HAND_CALIBRATION_CLOSED = 0,
    MATRIX_HAND_CALIBRATION_FIST = 1,
    MATRIX_HAND_CALIBRATION_SPREAD = 2,
    MATRIX_HAND_CALIBRATION_CROSSTALK = 3
} MatrixHandCalibrationStage;

typedef struct MatrixHandRuntimeConfig {
    uint32_t mean_filter_window_frame_count;
    uint32_t thumb_gate_filter_window_size;
    int32_t thumb_inward_gate_channel;
    double thumb_gate_deadband_ratio;
    double spread_deadband_ratio;
    int32_t crosstalk_fit_intercept;
    double crosstalk_max_abs_intercept;
} MatrixHandRuntimeConfig;

typedef struct MatrixHandAngleOutput {
    float little_finger[4];
    float ring_finger[4];
    float middle_finger[3];
    float index_finger[4];
    float thumb[3];
} MatrixHandAngleOutput;

typedef struct MatrixHandCalibrationResult {
    int32_t stage;
    int32_t frame_count;
    int32_t valid_channel_count;
    int32_t invalid_channel_count;
    int32_t invalid_channels[MATRIX_HAND_MAX_INVALID_CHANNEL_COUNT];
    int32_t xtalk_unstable_count;
    int32_t xtalk_unstable_channels[MATRIX_HAND_MAX_INVALID_CHANNEL_COUNT];
} MatrixHandCalibrationResult;

MATRIX_HAND_API MatrixHandHandle matrix_hand_create(void);
MATRIX_HAND_API void matrix_hand_destroy(MatrixHandHandle handle);

MATRIX_HAND_API int matrix_hand_reset(MatrixHandHandle handle);
MATRIX_HAND_API int matrix_hand_set_config(MatrixHandHandle handle, const MatrixHandRuntimeConfig* config);

MATRIX_HAND_API int matrix_hand_begin_calibration(MatrixHandHandle handle, MatrixHandCalibrationStage stage);
MATRIX_HAND_API int matrix_hand_push_calibration_frame(MatrixHandHandle handle, const int16_t ad[MATRIX_HAND_CHANNEL_COUNT]);
MATRIX_HAND_API int matrix_hand_finish_calibration(MatrixHandHandle handle, MatrixHandCalibrationResult* result);

MATRIX_HAND_API int matrix_hand_is_ready(MatrixHandHandle handle, int32_t* ready);
MATRIX_HAND_API int matrix_hand_process_frame(
    MatrixHandHandle handle,
    const int16_t ad[MATRIX_HAND_CHANNEL_COUNT],
    MatrixHandAngleOutput* output);

MATRIX_HAND_API int matrix_hand_get_current_ad(MatrixHandHandle handle, int32_t filtered, double ad[MATRIX_HAND_CHANNEL_COUNT]);
MATRIX_HAND_API const char* matrix_hand_status_text(int status);

#ifdef __cplusplus
}
#endif

#endif  // MATRIX_HAND_SDK_H
