#include <stdint.h>
#include <stdbool.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include "zephyr/logging/log_backend_fcb.h"

BUILD_ASSERT(!IS_ENABLED(CONFIG_LOG_MODE_IMMEDIATE),
	     "Immediate logging is not supported by LOG FCB backend.");

#define FCB_MAGIC_NUMBER   0x28090260
#define FCB_VERSION_NUMBER 0

static uint32_t log_format_current = CONFIG_LOG_BACKEND_FCB_OUTPUT_DEFAULT;
static struct flash_sector log_fcb_sectors[CONFIG_LOG_BACKEND_FCB_MAX_SECTORS];
static uint8_t write_buf[CONFIG_LOG_BACKEND_FCB_MAX_MESSAGE_SIZE];
static struct fcb fcb;
static bool init_ok = false;

/* The FCB backend API was written so that the users don't need to depend on
FCB header files, and the context is therefore represented as an array. That
array will be mapped into the following struct. */
struct read_context {
	struct fcb_entry loc; /* FCB context */
	uint32_t split_addr;  /* Used when an FCB entry must be split into multiple calls*/
};
BUILD_ASSERT(sizeof(log_backend_fcb_read_ctx_t) >= sizeof(struct read_context),
	     "log_backend_fcb_read_ctx_t must be at least as large as struct read_context");

static int write(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);
	int rc = 0;
	struct fcb_entry loc;
	
	if (!init_ok) {
		return -ENODEV;
	}
	printk("\n________IM WRITING_____\r\n");
	// TODO Verify length is not too long
	rc = fcb_append(&fcb, length, &loc);
	if (rc == -ENOSPC) {
		/* Rotate and retry */
		rc = fcb_rotate(&fcb);
		if (rc != 0) {
			init_ok = false;
			return -ENOMEM;
		}
		rc = fcb_append(&fcb, length, &loc);
		if (rc != 0) {
			init_ok = false;
			return -EIO;
		}
	} else if (rc != 0) {
		init_ok = false;
		return -EIO;
	}

	rc = flash_area_write(fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc), data, length);
	if (rc != 0) {
		(void)fcb_append_finish(&fcb, &loc);
		return -EIO;
	}

	rc = fcb_append_finish(&fcb, &loc);
	if (rc != 0) {
		return -EIO;
	}
	return length;
}

LOG_OUTPUT_DEFINE(log_output_fcb, write, write_buf, sizeof(write_buf));

/* Initialize FCB on the configured flash partition. If the partition has
already been initialized, the FCB will initialize based on the data already
present.
Note! If the partition has *not* been initialized, it is recommended to
erase the partition before initializing the FCB. Otherwise the initialization
may fail. See log_backend_fcb_erase(). */
static int init_fcb(void)
{
	int rc;
	uint32_t sector_count = ARRAY_SIZE(log_fcb_sectors);

	/* Get sector information from flash map API */
	rc = flash_area_get_sectors(FIXED_PARTITION_ID(log_partition), &sector_count,
				    log_fcb_sectors);
	if ((rc != 0) && (rc != -ENOMEM)) {
		/* -ENOMEM means that the partition contains more sectors than we need,
		which is ok. */
		return rc;
	}

	/* Initialize FCB */
	fcb.f_magic = FCB_MAGIC_NUMBER;
	fcb.f_version = FCB_VERSION_NUMBER;
	fcb.f_sector_cnt = sector_count;
	fcb.f_sectors = log_fcb_sectors;
	rc = fcb_init(FIXED_PARTITION_ID(log_partition), &fcb);
	if (rc != 0) {
		printk("fcb_init failed: %d\n", rc);
		return -2;
	}
	return 0;
}

static void init(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
	int rc = init_fcb();
	if (rc == 0) {
		/** All good, log backend has been successfully initialized */
		init_ok = true;
		printk("I'm in init()  -- PASS!!!\r\n");
	}
}

static int is_ready(struct log_backend const *const backend)
{
	ARG_UNUSED(backend);

	return init_ok ? 0 : -EBUSY;
}

static int format_set(const struct log_backend *const backend, uint32_t log_type)
{
	log_format_current = log_type;
	return 0;
}

static void panic(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
	log_backend_std_panic(&log_output_fcb);
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);

	log_backend_std_dropped(&log_output_fcb, cnt);
}

void process(const struct log_backend *const backend, union log_msg_generic *msg)
{
	ARG_UNUSED(backend);

	uint32_t flags = log_backend_std_get_flags();

	log_format_func_t log_output_func = log_format_func_t_get(log_format_current);

	log_output_func(&log_output_fcb, &msg->log, flags);
}

static void notify(const struct log_backend *const backend, enum log_backend_evt event,
		   union log_backend_evt_arg *arg)
{
	ARG_UNUSED(backend);
	ARG_UNUSED(event);
	ARG_UNUSED(arg);
}

static const struct log_backend_api log_backend_fcb_api = {
	.process = process,
	.dropped = dropped,
	.panic = panic,
	.init = init,
	.is_ready = is_ready,
	.format_set = format_set,
	.notify = notify,
};

LOG_BACKEND_DEFINE(log_backend_fcb, log_backend_fcb_api,
		   IS_ENABLED(CONFIG_LOG_BACKEND_FCB_AUTOSTART));

int log_backend_fcb_read(uint8_t *buf, size_t buf_size, log_backend_fcb_read_ctx_t ctx)
{
	__ASSERT_NO_MSG(buf != NULL);
	__ASSERT_NO_MSG(buf_size > 0);
	__ASSERT_NO_MSG(ctx != NULL);

	if (!init_ok) {
		printk("\n****** INIT NOT OK\n");
		return -ENODEV;
	}

	int total_bytes_read = 0;
	struct fcb_entry loc;
	struct read_context *ctxp = (struct read_context *)ctx;
	if (ctxp->loc.fe_sector == NULL) {
		memset(&loc, 0, sizeof(loc));
	} else {
		memcpy(&loc, &ctxp->loc, sizeof(loc));
	}

	if (ctxp->split_addr > 0) {
		/* Previous call to log_backend_fcb_read() did not return the
		whole entry. Continue reading where it left off (stored in
		ctxp->split_addr) */
		size_t remaining_len =
			FCB_ENTRY_FA_DATA_OFF(loc) + loc.fe_data_len - ctxp->split_addr;
		size_t read_len = (remaining_len > buf_size) ? buf_size : remaining_len;
		int rc = flash_area_read(fcb.fap, ctxp->split_addr, buf, read_len);
		if (rc != 0) {
			return rc;
		}
		total_bytes_read += read_len;
		if (remaining_len > buf_size) {
			/* We're gonna have to do another split read */
			ctxp->split_addr += read_len;
			return total_bytes_read;
		} else {
			/* Done reading this entry. Continue to next entry. */
			ctxp->split_addr = 0;
		}
	}

	while (0 == fcb_getnext(&fcb, &loc)) {
		size_t len = loc.fe_data_len;
		if (total_bytes_read + len > buf_size) {
			/* Given buffer can't fit the next entry */
			if (total_bytes_read > 0) {
				// If we already read some data, return it now
				return total_bytes_read;
			}
			/* A single log entry is larger than the provided buffer.
			Read up to size of buffer and prepare for split read. */
			len = buf_size;
			ctxp->split_addr = loc.fe_sector->fs_off + loc.fe_data_off + len;
		}

		int rc = flash_area_read(fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc),
					 buf + total_bytes_read, len);
		if (rc != 0) {
			return rc;
		}
		total_bytes_read += len;
		memcpy(&ctxp->loc, &loc, sizeof(ctxp->loc));
	}
	if (ctxp->split_addr == 0) {
		/* All log entries have been fully read. Clear context. */
		memset(&ctxp->loc, 0, sizeof(ctxp->loc));
	}
	return total_bytes_read;
}

/* Erase flash partition and re-initialize FCB */
int log_backend_fcb_erase(void)
{
	int rc;
	const struct flash_area *fap;
	rc = flash_area_open(FIXED_PARTITION_ID(log_partition), &fap);
	if (rc) {
		return rc;
	}
	rc = flash_area_erase(fap, 0, fap->fa_size);
	flash_area_close(fap);

	if (rc == 0) {
		rc = init_fcb();
	}
	return rc;
}