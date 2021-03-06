/*-
 * Copyright 2016 Vsevolod Stakhov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "config.h"
#include "rspamadm.h"
#include "cfg_file.h"
#include "cfg_rcl.h"
#include "rspamd.h"
#include "lua/lua_common.h"

static gboolean quiet = FALSE;
static gchar *config = NULL;
static gboolean strict = FALSE;
extern struct rspamd_main *rspamd_main;
/* Defined in modules.c */
extern module_t *modules[];
extern worker_t *workers[];

static void rspamadm_configtest (gint argc, gchar **argv,
								 const struct rspamadm_command *cmd);
static const char *rspamadm_configtest_help (gboolean full_help,
											 const struct rspamadm_command *cmd);

struct rspamadm_command configtest_command = {
		.name = "configtest",
		.flags = 0,
		.help = rspamadm_configtest_help,
		.run = rspamadm_configtest,
		.lua_subrs = NULL,
};

static GOptionEntry entries[] = {
		{"quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet,
				"Suppress output", NULL},
		{"config", 'c', 0, G_OPTION_ARG_STRING, &config,
				"Config file to test",     NULL},
		{"strict", 's', 0, G_OPTION_ARG_NONE, &strict,
				"Stop on any error in config", NULL},
		{NULL,  0,   0, G_OPTION_ARG_NONE, NULL, NULL, NULL}
};

static const char *
rspamadm_configtest_help (gboolean full_help, const struct rspamadm_command *cmd)
{
	const char *help_str;

	if (full_help) {
		help_str = "Perform configuration file test\n\n"
				"Usage: rspamadm configtest [-q -c <config_name>]\n"
				"Where options are:\n\n"
				"-q: quiet output\n"
				"-c: config file to test\n"
				"--help: shows available options and commands";
	}
	else {
		help_str = "Perform configuration file test";
	}

	return help_str;
}

static void
config_logger (rspamd_mempool_t *pool, gpointer ud)
{
	struct rspamd_main *rm = ud;
	GQuark configtest_quark = g_quark_from_static_string ("configtest");

	rm->cfg->log_type = RSPAMD_LOG_CONSOLE;

	if (quiet) {
		rm->cfg->log_level = G_LOG_LEVEL_CRITICAL;
	}
	else {
		rm->cfg->log_level = G_LOG_LEVEL_WARNING;
	}

	rspamd_set_logger (rm->cfg, configtest_quark, &rm->logger,
			rm->server_pool);
	if (rspamd_log_open_priv (rm->logger, rm->workers_uid, rm->workers_gid) ==
			-1) {
		fprintf (stderr, "Fatal error, cannot open logfile, exiting\n");
		exit (EXIT_FAILURE);
	}
}

static void
rspamadm_configtest (gint argc, gchar **argv, const struct rspamadm_command *cmd)
{
	GOptionContext *context;
	GError *error = NULL;
	const gchar *confdir;
	struct rspamd_config *cfg = rspamd_main->cfg;
	gboolean ret = TRUE;
	worker_t **pworker;
	const guint64 *log_cnt;

	context = g_option_context_new (
			"configtest - perform configuration file test");
	g_option_context_set_summary (context,
			"Summary:\n  Rspamd administration utility version "
					RVERSION
					"\n  Release id: "
					RID);
	g_option_context_add_main_entries (context, entries, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		fprintf (stderr, "option parsing failed: %s\n", error->message);
		g_error_free (error);
		exit (1);
	}

	if (config == NULL) {
		if ((confdir = g_hash_table_lookup (ucl_vars, "CONFDIR")) == NULL) {
			confdir = RSPAMD_CONFDIR;
		}

		config = g_strdup_printf ("%s%c%s", confdir, G_DIR_SEPARATOR,
				"rspamd.conf");
	}

	pworker = &workers[0];
	while (*pworker) {
		/* Init string quarks */
		(void) g_quark_from_static_string ((*pworker)->name);
		pworker++;
	}
	cfg->cache = rspamd_symcache_new (cfg);
	cfg->compiled_modules = modules;
	cfg->compiled_workers = workers;
	cfg->cfg_name = config;

	if (!rspamd_config_read (cfg, cfg->cfg_name, config_logger, rspamd_main, ucl_vars)) {
		ret = FALSE;
	}
	else {
		/* Do post-load actions */
		rspamd_lua_post_load_config (cfg);

		if (!rspamd_init_filters (rspamd_main->cfg, FALSE)) {
			ret = FALSE;
		}

		if (ret) {
			ret = rspamd_config_post_load (cfg, RSPAMD_CONFIG_INIT_SYMCACHE);
		}

		if (!rspamd_symcache_validate (rspamd_main->cfg->cache,
				rspamd_main->cfg,
				FALSE)) {
			ret = FALSE;
		}
	}

	if (strict && ret) {
		log_cnt = rspamd_log_counters (rspamd_main->logger);

		if (log_cnt && log_cnt[0] > 0) {
			if (!quiet) {
				rspamd_printf ("%L errors found\n", log_cnt[0]);
			}
			ret = FALSE;
		}
	}

	if (!quiet) {
		rspamd_printf ("syntax %s\n", ret ? "OK" : "BAD");
	}

	if (!ret) {
		exit (EXIT_FAILURE);
	}

	exit (EXIT_SUCCESS);
}
