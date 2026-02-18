/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>
#include <stdlib.h>

#include "coerce.h"
#include "external/libcurl.h"
#include "functions/modules/curl.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/timer.h"
#include "util.h"

struct module_curl_request {
	const char *url;
	uint8_t *buf;
	uint64_t len;
	int32_t handle;
	struct mc_fetch_stats stats;
	struct timer duration;
	bool started, done, err;
};

struct module_curl_fetch_ctx {
	struct module_curl_request *requests;
	uint32_t len, prev_list_len;
};

struct module_curl_fetch_progress_decorate_elem {
	struct module_curl_request *request;
	float dur;
};

static int32_t
module_curl_fetch_progress_decorate_sort_compare(const void *_a, const void *_b)
{
	const struct module_curl_fetch_progress_decorate_elem *a = _a, *b = _b;
	return a->dur > b->dur ? -1 : 1;
}

static void
module_curl_fetch_progress_decorate(void *_ctx, uint32_t width)
{
	struct module_curl_fetch_ctx *ctx = _ctx;
	struct module_curl_fetch_progress_decorate_elem list[32] = { 0 };
	uint32_t list_len = 0;

	for (uint32_t i = 0; i < ctx->len; ++i) {
		struct module_curl_request *r = &ctx->requests[i];
		if (r->done || !r->started) {
			continue;
		}

		list[list_len].request = r;
		list[list_len].dur = timer_read(&r->duration);
		++list_len;
		if (list_len >= ARRAY_LEN(list)) {
			break;
		}
	}

	qsort(list,
		list_len,
		sizeof(struct module_curl_fetch_progress_decorate_elem),
		module_curl_fetch_progress_decorate_sort_compare);

	uint32_t i;
	for (i = 0; i < list_len; ++i) {
		char buf[512] = { 0 };
		uint32_t buf_i = 0;
		struct module_curl_request *r = list[i].request;

		const int64_t total = r->stats.total, dl = r->stats.downloaded;
		if (total && dl && dl <= total) {
			snprintf_append(buf, &buf_i, "%3.0f%% ", 100.0 * (double)dl / (double)total);
		} else {
		}

		snprintf_append(buf, &buf_i, "%6.2fs %s", list[i].dur, r->url);
		log_raw("%s\033[K\n", buf);
	}
	for (; i < ctx->prev_list_len; ++i) {
		log_raw("\033[K\n");
	}
	log_raw("\033[%dA", MAX(list_len, ctx->prev_list_len) + 1);
	ctx->prev_list_len = list_len;
}

FUNC_IMPL(module_curl,
	fetch,
	tc_string,
	func_impl_flag_impure,
	.desc = "Fetch url(s) using libcurl.  Only available if libcurl support is enabeld.")
{
	struct args_norm an[] = {
		{ make_complex_type(wk, complex_type_or, tc_string, complex_type_preset_get(wk, tc_cx_list_of_str)),
			.desc = "the url(s) to fetch" },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_progress,
		kw_batch_size,
	};
	struct args_kw akw[] = {
		[kw_progress] = { "progress", tc_required_kw, .desc = "Show progress output" },
		[kw_batch_size] = { "batch_size", tc_number, .desc = "Batch size" },
		0,
	};
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	bool is_list = false;
	struct module_curl_fetch_ctx ctx[1] = { 0 };
	{
		if (get_obj_type(wk, an[0].val) == obj_array) {
			is_list = true;
			*res = make_obj(wk, obj_array);
		} else {
			obj arr = make_obj(wk, obj_array);
			obj_array_push(wk, arr, an[0].val);
			an[0].val = arr;
		}
		const struct obj_array *list = get_obj_array(wk, an[0].val);

		if (!list->len) {
			return true;
		}
		ctx->len = list->len;
		ctx->requests = ar_maken(wk->a_scratch, struct module_curl_request, list->len);
		obj v;
		uint32_t i = 0;
		obj_array_for(wk, an[0].val, v) {
			struct module_curl_request *r = &ctx->requests[i];
			r->url = get_cstr(wk, v);
			++i;
		}
	}

	bool progress_bar;
	{
		enum requirement_type req = requirement_auto;
		if (akw[kw_progress].set) {
			if (!coerce_requirement(wk, &akw[kw_progress], &req)) {
				return false;
			}
		}

		if (req == requirement_auto) {
			progress_bar = is_list;
		} else if (req == requirement_required) {
			progress_bar = true;
		} else {
			progress_bar = false;
		}
	}

	int64_t job_count = 8;
	if (akw[kw_batch_size].set) {
		job_count = get_obj_number(wk, akw[kw_batch_size].val);
		if (job_count < 1) {
			vm_error_at(wk, akw[kw_batch_size].node, "invalid batch size: %" PRId64, job_count);
			return false;
		}
	}

	log_progress_push_state(wk);
	if (progress_bar) {
		log_progress_enable(wk);
		log_progress_push_level(0, ctx->len);
	}
	struct log_progress_style log_progress_style = {
		.show_count = true,
		.decorate = module_curl_fetch_progress_decorate,
		.usr_ctx = &ctx,
		.dont_disable_on_error = true,
	};
	log_progress_set_style(&log_progress_style);

	mc_init(wk->a_scratch);

	bool ok = true;
	uint32_t cnt_running = 0, cnt_complete = 0;
	while (cnt_complete < ctx->len) {
		cnt_running = 0;
		for (uint32_t i = 0; i < ctx->len; ++i) {
			struct module_curl_request *r = &ctx->requests[i];
			if (r->done) {
				continue;
			}

			++cnt_running;
			if (cnt_running > job_count) {
				break;
			}

			if (!r->started) {
				r->started = true;
				timer_start(&r->duration);
				if ((r->handle = mc_fetch_begin(r->url, &r->buf, &r->len, 0)) == -1) {
					ok = false;
					r->err = true;
					r->done = true;
					++cnt_complete;
				}
			} else {
				switch (mc_fetch_collect(r->handle, &r->stats)) {
				case mc_fetch_collect_result_pending: break;
				case mc_fetch_collect_result_done: {
					r->done = true;
					++cnt_complete;
					break;
				}
				case mc_fetch_collect_result_error: {
					ok = false;
					r->err = true;
					r->done = true;
					++cnt_complete;
					break;
				}
				}
			}
		}

		log_progress_subval(wk, cnt_complete, cnt_complete + cnt_running);

		mc_wait(1000);
	}

	if (ok) {
		if (is_list) {
			*res = make_obj(wk, obj_array);
			for (uint32_t i = 0; i < ctx->len; ++i) {
				struct module_curl_request *r = &ctx->requests[i];
				obj str = make_strn(wk, (char *)r->buf, r->len);
				obj_array_push(wk, *res, str);
			}
		} else {
			struct module_curl_request *r = &ctx->requests[0];
			*res = make_strn(wk, (char *)r->buf, r->len);
		}
	}

	mc_deinit();

	if (progress_bar) {
		log_progress_disable();
	}
	log_progress_pop_state(wk);

	return ok;
}

FUNC_REGISTER(module_curl)
{
	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(module_curl, fetch);
	}
}
