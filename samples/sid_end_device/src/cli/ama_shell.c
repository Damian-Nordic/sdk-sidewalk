/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ace/cli.h>
#include <sid_qa.h>
#include <sid_config_cli.h>
#include <app_ble_config.h>
#include <app_subGHz_config.h>
#include <sid_asd_cli.h>
#include <sid_api.h>
#include <sid_error.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>

#define AMA_SHELL_MAX_COMMAND_LEN 768

static struct k_thread ama_qa_thread;
K_THREAD_STACK_DEFINE(ama_qa_thread_stack, CONFIG_SIDEWALK_THREAD_STACK_SIZE);

K_SEM_DEFINE(ama_qa_event_sem, 0, 1);

static struct sid_config ama_sid_config;

static void pwr_meas_mode_enter(void)
{
}

static void pwr_meas_mode_exit(void)
{
}

static void pwr_meas_is_blocked(void)
{
	k_sem_take(&ama_qa_event_sem, K_FOREVER);
}

static void pwr_meas_is_unblocked(void)
{
	k_sem_give(&ama_qa_event_sem);
}

static void reboot_func(void)
{
	sys_reboot(SYS_REBOOT_WARM);
}

static void set_sub_ghz_cfg(struct sid_sub_ghz_links_config *sub_ghz_cfg)
{
#ifdef CONFIG_SIDEWALK_SUBGHZ_SUPPORT
	if (sub_ghz_cfg != NULL) {
		memcpy(app_get_sub_ghz_config(), sub_ghz_cfg, sizeof(*sub_ghz_cfg));
	}
#else
	ARG_UNUSED(sub_ghz_cfg);
#endif /* CONFIG_SIDEWALK_SUBGHZ_SUPPORT */
}

static void ama_qa_thread_entry(void *context, void *unused, void *unused2)
{
	ARG_UNUSED(context);
	ARG_UNUSED(unused);
	ARG_UNUSED(unused2);

	while (true) {
		sid_qa_process(QA_PROC_WAIT);
	}
}

static int ama_shell_init(void)
{
	sid_error_t err = sid_cli_init();

	if (err != SID_ERROR_NONE) {
		return err;
	}

	struct sid_end_device_characteristics dev_ch = {
		.type = SID_END_DEVICE_TYPE_STATIC,
		.power_type = SID_END_DEVICE_POWERED_BY_BATTERY_AND_LINE_POWER,
		.qualification_id = 0x0004,
	};

	ama_sid_config = (struct sid_config) {
		.dev_ch = dev_ch,
		.link_config = app_get_ble_config(),
		.sub_ghz_link_config = app_get_sub_ghz_config(),
	};

	static struct sid_qa_pwr_meas_if pwr_meas_if = {
		.enter_func = pwr_meas_mode_enter,
		.exit_func = pwr_meas_mode_exit,
		.blocked_func = pwr_meas_is_blocked,
		.unblocked_func = pwr_meas_is_unblocked,
	};

	sid_qa_set_config(&ama_sid_config, &pwr_meas_if);

	static struct sid_qa_callbacks qa_callbacks = {
		.reboot_cmd = reboot_func,
		.set_sub_ghz_cfg = set_sub_ghz_cfg,
	};

	err = sid_qa_init(&qa_callbacks);
	if (err != SID_ERROR_NONE) {
		return err;
	}

	err = sid_config_cli_init();
	if (err != SID_ERROR_NONE) {
		return err;
	}

	(void)k_thread_create(&ama_qa_thread, ama_qa_thread_stack,
			      K_THREAD_STACK_SIZEOF(ama_qa_thread_stack), ama_qa_thread_entry, NULL,
			      NULL, NULL, CONFIG_SIDEWALK_THREAD_PRIORITY, 0, K_NO_WAIT);
	(void)k_thread_name_set(&ama_qa_thread, "ama_qa");

	return 0;
}

SYS_INIT(ama_shell_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static int cmd_ama(const struct shell *shell, int32_t argc, const char **argv)
{
	char command[AMA_SHELL_MAX_COMMAND_LEN];
	size_t offset = 0;

	for (int32_t arg = 1; arg < argc; arg++) {
		size_t separator_len = (arg > 1) ? 1 : 0;
		size_t arg_len = strlen(argv[arg]);

		if ((offset + separator_len + arg_len) >= sizeof(command)) {
			shell_error(shell, "AMA command length exceeds %d bytes",
				    AMA_SHELL_MAX_COMMAND_LEN);
			return -ENOBUFS;
		}

		if (separator_len != 0) {
			command[offset++] = ' ';
		}

		memcpy(&command[offset], argv[arg], arg_len);
		offset += arg_len;
	}

	command[offset] = '\0';

	return (int)aceCli_processCmd(command);
}

SHELL_CMD_ARG_REGISTER(ama, NULL, "Forward command to Amazon SDK CLI", cmd_ama, 1,
		       CONFIG_SHELL_ARGC_MAX - 1);
