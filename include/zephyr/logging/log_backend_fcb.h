/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_LOG_BACKEND_FCB_H_
#define ZEPHYR_LOG_BACKEND_FCB_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Transparent context storage */
typedef uint32_t log_backend_fcb_read_ctx_t[5];

/**
 * @brief Erase all log messages stored in FCB
 *
 * @return 0 on success, negative value on error
 *
 * @note This function erases the entire partition, regardless of the number
 * of sectors specified in CONFIG_LOG_BACKEND_FCB_MAX_SECTORS.
 */
int log_backend_fcb_erase(void);

/**
 * @brief Read log messages from FCB
 *
 * This function can be called multiple times to read chunks of log data in
 * case the provided buffer is not large enough to store the entire log. This
 * requires that the caller keeps a reference to the context between calls.
 *
 * The context is represented as an array of integers. On the first call, set
 * the first element of this array to zero to start reading from the beginning
 * of the log. When the function returns, check the first element of the
 * context; if it is zero the entire log has been returned. If it is non-zero,
 * call the function again with the returned context to read the next chunk of
 * log data.
 *
 * @param[out] buf Buffer to store the log messages
 * @param[in] buf_size Buffer size in bytes
 * @param[inout] ctx Context. On input: Set first element to zero to start
 * reading from beginning of log. Then use the context returned by the previous
 * call to continue reading from where the previous call left off.
 * On output: Updated context that can be passed to the next call. If the first
 * element is zero, the end of the log has been reached.
 *
 * @return Number of bytes read, or a negative value on error
 *
 * Example usage:
 *
 * uint8_t buf[256];
 * log_backend_fcb_read_ctx_t myctx = {0};
 * int bytes_read = 0;
 * do {
 *     bytes_read = log_backend_fcb_read(buf, sizeof(buf), myctx);
 *     if (bytes_read < 0) {
 *        // handle error
 *     else {
 *       // Process the log messages in buf
 *     }
 * } while ((bytes_read > 0) && (myctx[0] != 0));
 */
int log_backend_fcb_read(uint8_t *buf, size_t buf_size, log_backend_fcb_read_ctx_t ctx);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_LOG_BACKEND_FCB_H_ */
