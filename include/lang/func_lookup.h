/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_FUNC_LOOKUP_H
#define MUON_LANG_FUNC_LOOKUP_H

#include "lang/workspace.h"

typedef bool (*func_native_impl)(struct workspace *wk, obj self, obj *res);
typedef obj (*func_impl_self_transform)(struct workspace *wk, obj self);

enum func_impl_flag {
	func_impl_flag_sandbox_disable = 1 << 0,
	func_impl_flag_extension = 1 << 1,
	func_impl_flag_throws_error = 1 << 2,
};

struct func_impl {
	const char *name;
	func_native_impl func;
	type_tag return_type;
	bool pure;
	enum func_impl_flag flags;
	func_impl_self_transform self_transform;
	const char *desc;
};

struct func_impl_group {
	const struct func_impl *impls;
	uint32_t off, len;
};

extern struct func_impl_group func_impl_groups[obj_type_count][language_mode_count];
extern struct func_impl native_funcs[];

void build_func_impl_tables(void);

bool func_lookup(struct workspace *wk, obj self, const char *name, uint32_t *idx, obj *func);
bool func_lookup_for_group(const struct func_impl_group impl_group[],
	enum language_mode mode,
	const char *name,
	uint32_t *idx);

void func_kwargs_lookup(struct workspace *wk, obj self, const char *name, struct arr *kwargs_arr);
void kwargs_arr_destroy(struct workspace *wk, struct arr *arr);
void kwargs_arr_push(struct workspace *wk, struct arr *arr, const struct args_kw *kw);
void kwargs_arr_del(struct workspace *wk, struct arr *arr, const char *name);
struct args_kw *kwargs_arr_get(struct workspace *wk, struct arr *arr, const char *name);

void dump_function_signatures(struct workspace *wk);
void dump_function_docs(struct workspace *wk);

obj dump_function_native(struct workspace *wk, enum obj_type t, const struct func_impl *impl);
obj dump_module_function_native(struct workspace *wk, enum module module, const struct func_impl *impl);
obj dump_module_function_capture(struct workspace *wk, const char *module, obj name, obj o);
#endif
