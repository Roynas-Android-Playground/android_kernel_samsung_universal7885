/*
 * Copyright (c) 2013-2016 TRUSTONIC LIMITED
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/string.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/kthread.h>

#include "mobicore_driver_api.h"
#include "tui_ioctl.h"
#include "tlcTui.h"
#include "dciTui.h"
#include "tui-hal.h"

#if defined(CONFIG_SECURE_OS_BOOSTER_API)
#include <soc/samsung/secos_booster.h>
#endif

/* ------------------------------------------------------------- */
/* Globals */
struct tui_dci_msg_t *dci;
static DECLARE_COMPLETION(dci_comp);
static DECLARE_COMPLETION(io_comp);

/* ------------------------------------------------------------- */
/* Static */
static const uint32_t DEVICE_ID = MC_DEVICE_ID_DEFAULT;
static struct task_struct *thread_id;
static struct tlc_tui_command_t g_user_cmd = {.id = TLC_TUI_CMD_NONE};
static struct mc_session_handle dr_session_handle = {0, 0};
struct tlc_tui_response_t g_user_rsp = {.id = TLC_TUI_CMD_NONE,
				.return_code = TLC_TUI_ERR_UNKNOWN_CMD};
bool g_dci_version_checked;

/* Functions */

/* ------------------------------------------------------------- */
static bool tlc_open_driver(void)
{
	bool ret = false;
	enum mc_result mc_ret;
	struct mc_uuid_t dr_uuid = DR_TUI_UUID;

	/* Allocate WSM buffer for the DCI */
	mc_ret = mc_malloc_wsm(DEVICE_ID, 0, sizeof(struct tui_dci_msg_t),
			       (uint8_t **)&dci, 0);
	if (MC_DRV_OK != mc_ret) {
		pr_info("ERROR %s:%d Allocation of DCI WSM failed: %d\n",
			 __func__, __LINE__, mc_ret);
		return false;
	}

	/* Clear the session handle */
	memset(&dr_session_handle, 0, sizeof(dr_session_handle));
	/* The device ID (default device is used */
	dr_session_handle.device_id = DEVICE_ID;
	/* Open session with the Driver */
	mc_ret = mc_open_session(&dr_session_handle, &dr_uuid, (uint8_t *)dci,
				 (uint32_t)sizeof(struct tui_dci_msg_t));
	if (MC_DRV_OK != mc_ret) {
		pr_info("ERROR %s:%d Open driver session failed: %d\n",
			 __func__, __LINE__, mc_ret);
		ret = false;
	} else {
		ret = true;
	}

	return ret;
}

/* ------------------------------------------------------------- */
static bool tlc_open(void)
{
	bool ret = false;
	enum mc_result mc_ret;

	/* Open the tbase device */
	pr_info("%s: Opening tbase device\n", __func__);
	mc_ret = mc_open_device(DEVICE_ID);

	/* In case the device is already open, mc_open_device will return an
	 * error (MC_DRV_ERR_INVALID_OPERATION).  But in this case, we can
	 * continue, even though mc_open_device returned an error.  Stop in all
	 * other case of error
	 */
	if (MC_DRV_OK != mc_ret && MC_DRV_ERR_INVALID_OPERATION != mc_ret) {
		pr_info("ERROR %s:%d Error %d opening device\n", __func__,
			 __LINE__, mc_ret);
		return false;
	}

	pr_info("%s: Opening driver session\n", __func__);
	ret = tlc_open_driver();

	return ret;
}

/* ------------------------------------------------------------- */
static void tlc_wait_cmd_from_driver(void)
{
	uint32_t ret = TUI_DCI_ERR_INTERNAL_ERROR;

	/* Wait for a command from secure driver */
	ret = mc_wait_notification(&dr_session_handle, -1);
	if (MC_DRV_OK == ret)
		pr_debug("tlc_wait_cmd_from_driver: Got a command\n");
	else
		pr_debug("ERROR %s:%d mc_wait_notification() failed: %d\n",
			 __func__, __LINE__, ret);
}

struct mc_session_handle *get_session_handle(void)
{
	return &dr_session_handle;
}

uint32_t send_cmd_to_user(uint32_t command_id, uint32_t data0, uint32_t data1)
{
	uint32_t ret = TUI_DCI_ERR_NO_RESPONSE;
	int retry = 10;

	/* Init shared variables */
	g_user_cmd.id = command_id;
	g_user_cmd.data[0] = data0;
	g_user_cmd.data[1] = data1;
	/* Erase the rsp struct */
	memset(&g_user_rsp, 0, sizeof(g_user_rsp));
	g_user_rsp.id = TLC_TUI_CMD_NONE;
	g_user_rsp.return_code = TLC_TUI_ERR_UNKNOWN_CMD;

	while (!atomic_read(&fileopened) && retry--) {
		msleep(100);
		pr_debug("sleep for atomic_read(&fileopened) with retry = %d\n",
			 retry);
	}

	/* Check that the client (TuiService) is still present before to return
	 * the command. */
	if (atomic_read(&fileopened)) {
		/* S.LSI : Clean up previous response. */
		complete(&io_comp);
		reinit_completion(&io_comp);

		/* Unlock the ioctl thread (IOCTL_WAIT) in order to let the
		 * client know that there is a command to process. */
		pr_info("%s: give way to ioctl thread\n", __func__);
		complete(&dci_comp);
		pr_info("TUI TLC is running, waiting for the userland response\n");
		/* Wait for the client acknowledge (IOCTL_ACK). */
		unsigned long completed = wait_for_completion_interruptible_timeout(&io_comp,HZ*5);
		if (!completed) {
			pr_debug("%s:%d No acknowledge from client, timeout!\n",
				 __func__, __LINE__);
		}
	} else {
		/* There is no client, do nothing except reporting an error to
		 * SWd. */
		pr_info("TUI TLC seems dead. Not waiting for userland answer\n");
		ret = TUI_DCI_ERR_INTERNAL_ERROR;
		goto end;
	}

	pr_debug("send_cmd_to_user: Got an answer from ioctl thread.\n");
	reinit_completion(&io_comp);

	/* Check id of the cmd processed by ioctl thread (paranoia) */
	if (g_user_rsp.id != command_id) {
				pr_info("ERROR %s:%d Wrong response id %d iso %d\n",
		//		pr_info("ERROR %s:%d Wrong response id \n",
					 __func__, __LINE__ , (unsigned int)g_user_rsp.id, (unsigned int)RSP_ID(command_id));
		ret = TUI_DCI_ERR_INTERNAL_ERROR;
	} else {
		/* retrieve return code */
		switch (g_user_rsp.return_code) {
		case TLC_TUI_OK:
			ret = TUI_DCI_OK;
			break;
		case TLC_TUI_ERROR:
			ret = TUI_DCI_ERR_INTERNAL_ERROR;
			break;
		case TLC_TUI_ERR_UNKNOWN_CMD:
			ret = TUI_DCI_ERR_UNKNOWN_CMD;
			break;
		}
	}

end:
	/* In any case, reset the value of the command, to ensure that commands
	 * sent due to inturrupted wait_for_completion are TLC_TUI_CMD_NONE. */
	reset_global_command_id();
	return ret;
}

/* ------------------------------------------------------------- */
static void tlc_process_cmd(void)
{
	uint32_t ret = TUI_DCI_ERR_INTERNAL_ERROR;
	uint32_t command_id = CMD_TUI_SW_NONE;
#if defined(CONFIG_SECURE_OS_BOOSTER_API)
	int ret_val = 0;
	u8 retry_cnt = 0;
	uint32_t TUI_BOOSTER = 0xFFFF0000; /* boosting Frequency = MAX, Boosting time =  0xFFFF (65536 msec)*/
#endif

	if  (NULL == dci) {
		pr_debug("ERROR %s:%d DCI has not been set up properly - exiting\n",
			 __func__, __LINE__);
		return;
	}

	command_id = dci->cmd_nwd.id;

	if (dci->hal_rsp)
		hal_tui_notif();

	/* Warn if previous response was not acknowledged */
	if (CMD_TUI_SW_NONE == command_id) {
		pr_debug("ERROR %s:%d Notified without command\n", __func__,
			 __LINE__);
		return;
	}

	if (dci->nwd_rsp.id != CMD_TUI_SW_NONE)
		pr_debug("%s: Warning, previous response not ack\n",
			 __func__);

	/* Handle command */
	switch (command_id) {
	case CMD_TUI_SW_OPEN_SESSION:
		pr_debug("%s: CMD_TUI_SW_OPEN_SESSION.\n", __func__);

		if (!g_dci_version_checked) {
			pr_info("ERROR %s:%d DrTui version is not compatible!\n",
				__func__, __LINE__);
			ret = TUI_DCI_ERR_INTERNAL_ERROR;
			break;
		}

#if defined(CONFIG_SECURE_OS_BOOSTER_API)
		pr_info("%s TUI_CPU_SPEEDUP ON retry: %d\n",
			__func__, retry_cnt);
		do {
			ret_val = secos_booster_start(TUI_BOOSTER);
			retry_cnt++;
			if (ret_val) {
				pr_err("%s: booster start failed. (%d) retry: %d\n"
					, __func__, ret_val, retry_cnt);
				if (retry_cnt < 7)
					usleep_range(500, 510);
				}
			} while (ret_val && retry_cnt < 7);
#endif

		/* Start android TUI activity */
		ret = send_cmd_to_user(TLC_TUI_CMD_START_ACTIVITY,
				       dci->cmd_nwd.payload.alloc_data.
				       num_of_buff,
				       dci->cmd_nwd.payload.alloc_data.
				       alloc_size);
		if (TUI_DCI_OK != ret)
			break;

/*****************************************************************************/

		/* Alloc work buffer separately and send it as last buffer */
		ret = hal_tui_alloc(dci->nwd_rsp.alloc_buffer,
				    dci->cmd_nwd.payload.alloc_data.alloc_size,
				   dci->cmd_nwd.payload.alloc_data.num_of_buff);
		if (TUI_DCI_OK != ret) {
			pr_debug("%s: hal_tui_alloc() failed (0x%08X)",
				 __func__, ret);
			send_cmd_to_user(TLC_TUI_CMD_STOP_ACTIVITY, 0, 0);
			break;
		}

		/* Deactivate linux UI drivers */
		ret = hal_tui_deactivate();

		if (TUI_DCI_OK != ret) {
			hal_tui_free();
			send_cmd_to_user(TLC_TUI_CMD_STOP_ACTIVITY, 0, 0);
			break;
		}

		break;

	case CMD_TUI_SW_GET_VERSION: {
		pr_debug("%s: CMD_TUI_SW_GET_VERSION.\n", __func__);
		uint32_t drtui_dci_version = dci->version;
		uint32_t tlctui_dci_version =
			TUI_DCI_VERSION(TUI_DCI_VERSION_MAJOR,
					TUI_DCI_VERSION_MINOR);
		pr_info("%s: TlcTui DCI Version (%u.%u)\n",  __func__,
			TUI_DCI_VERSION_GET_MAJOR(tlctui_dci_version),
			TUI_DCI_VERSION_GET_MINOR(tlctui_dci_version));
		pr_info("%s: DrTui DCI Version (%u.%u)\n",  __func__,
			TUI_DCI_VERSION_GET_MAJOR(drtui_dci_version),
			TUI_DCI_VERSION_GET_MINOR(drtui_dci_version));
		/* Write the TlcTui DCI version in the response for the SWd */
		dci->version = tlctui_dci_version;
		g_dci_version_checked = true;
		ret = TUI_DCI_OK;
		break;
	}

	case CMD_TUI_SW_HAL:
		/* TODO Always answer, even if there is a cancel!! */
		ret = hal_tui_process_cmd(&dci->cmd_nwd.payload.hal,
					  &dci->nwd_rsp.hal_rsp);
		break;

	case CMD_TUI_SW_CLOSE_SESSION:
		pr_debug("%s: CMD_TUI_SW_CLOSE_SESSION.\n", __func__);

		/* QC: close ion client before activating linux UI */
		hal_tui_free();

		/* Activate linux UI drivers */
		ret = hal_tui_activate();

#if defined(CONFIG_SECURE_OS_BOOSTER_API)
		ret_val = secos_booster_stop();
		if (ret_val)
			pr_err("%s: booster stop failed. (%d)\n"
				, __func__, ret_val);
#endif

		/* Stop android TUI activity */
		/* Ignore return code, because an error means the TLC has been
		 * killed, which imply that the activity is stopped already.
		 */
		send_cmd_to_user(TLC_TUI_CMD_STOP_ACTIVITY, 0, 0);
		ret = TUI_DCI_OK;

		break;

	default:
		pr_debug("ERROR %s:%d Unknown command %d\n",
			 __func__, __LINE__, command_id);
		ret = TUI_DCI_ERR_UNKNOWN_CMD;
		break;
	}

	/* Fill in response to SWd, fill ID LAST */
	pr_debug("%s: return 0x%08x to cmd 0x%08x\n",
		 __func__, ret, command_id);
	/* TODO: fill data fields of pDci->nwdRsp */
	dci->nwd_rsp.return_code = ret;
	dci->nwd_rsp.id = RSP_ID(command_id);

	/* Acknowledge command */
	dci->cmd_nwd.id = CMD_TUI_SW_NONE;

	/* Notify SWd */
	pr_debug("DCI RSP NOTIFY CORE\n");
	ret = mc_notify(&dr_session_handle);
	if (MC_DRV_OK != ret)
		pr_debug("ERROR %s:%d Notify failed: %d\n", __func__, __LINE__,
			 ret);
}

/* ------------------------------------------------------------- */
static void tlc_close_driver(void)
{
	enum mc_result ret;

	/* Close session with the Driver */
	ret = mc_close_session(&dr_session_handle);
	if (MC_DRV_OK != ret) {
		pr_debug("ERROR %s:%d Closing driver session failed: %d\n",
			 __func__, __LINE__, ret);
	}
}

/* ------------------------------------------------------------- */
static void tlc_close(void)
{
	enum mc_result ret;

	pr_debug("%s: Closing driver session\n", __func__);
	tlc_close_driver();

	pr_debug("%s: Closing tbase\n", __func__);
	/* Close the tbase device */
	ret = mc_close_device(DEVICE_ID);
	if (MC_DRV_OK != ret) {
		pr_debug("ERROR %s:%d Closing tbase device failed: %d\n",
			 __func__, __LINE__, ret);
	}
}

void reset_global_command_id(void)
{
	g_user_cmd.id = TLC_TUI_CMD_NONE;
}

/* ------------------------------------------------------------- */

bool tlc_notify_event(uint32_t event_type)
{
	bool ret = false;
	enum mc_result result;

	if (NULL == dci) {
		pr_debug("WARNING %s:%d tlc_notify_event: DCI has not been set up properly - exiting\n",
			 __func__, __LINE__);
		return false;
	}

	/* Prepare notification message in DCI */
	pr_debug("tlc_notify_event: event_type = %d\n", event_type);
	dci->nwd_notif = event_type;

	/* Signal the Driver */
	pr_debug("DCI EVENT NOTIFY CORE\n");
	result = mc_notify(&dr_session_handle);
	if (MC_DRV_OK != result) {
		pr_debug("ERROR %s:%d tlc_notify_event: mc_notify failed: %d\n",
			 __func__, __LINE__, result);
		ret = false;
	} else {
		ret = true;
	}

	return ret;
}

/* ------------------------------------------------------------- */
/**
 */
static int main_thread(void *uarg)
{
	pr_debug("main_thread: TlcTui start!\n");

	/* Open session on the driver */
	if (!tlc_open()) {
		pr_debug("ERROR %s:%d main_thread: open driver failed!\n",
			 __func__, __LINE__);
		return 1;
	}

	/* TlcTui main thread loop */
	for (;;) {
		/* Wait for a command from the DrTui on DCI */
		tlc_wait_cmd_from_driver();
		/* Something has been received, process it. */
		tlc_process_cmd();
	}

	/* Close tlc. Note that this frees the DCI pointer.
	 * Do not use this pointer after tlc_close().*/
	tlc_close();

	return 0;
}

static int start_thread_if_needed(void)
{
	/* Create the TlcTui Main thread and start secure driver (only
	   1st time) */
	if (!dr_session_handle.session_id) {
		thread_id = kthread_run_perf_critical(cpu_lp_mask, main_thread, NULL, "tee_tui");
		if (!thread_id) {
			pr_debug("Unable to start Trusted UI main thread\n");
			return -EFAULT;
		}
	}
	return 0;
}

int tlc_wait_cmd(struct tlc_tui_command_t *cmd_id)
{
	int ret = start_thread_if_needed();

	if (ret)
		return ret;

	/* Wait for signal from DCI handler */
	/* In case of an interrupted sys call, return with -EINTR */
	if (wait_for_completion_interruptible(&dci_comp)) {
		pr_debug("interrupted by system\n");
		return -ERESTARTSYS;
	}
	reinit_completion(&dci_comp);

	*cmd_id = g_user_cmd;
	return 0;
}

int tlc_init_driver(void)
{
	return start_thread_if_needed();
}

int tlc_ack_cmd(struct tlc_tui_response_t *rsp)
{
	g_user_rsp = *rsp;

	if (TLC_TUI_CMD_ALLOC_FB == g_user_rsp.id)
		hal_tui_post_start(&g_user_rsp);

	/* Send signal to DCI */
	complete(&io_comp);

	return 0;
}

/** @} */
