/* Copyright (c) 2006-2009 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "array.h"
#include "istream.h"
#include "mail-namespace.h"
#include "mailbox-list-private.h"
#include "acl-api-private.h"
#include "acl-lookup-dict.h"
#include "acl-plugin.h"

#include <stdlib.h>

struct acl_storage_module acl_storage_module =
	MODULE_CONTEXT_INIT(&mail_storage_module_register);
struct acl_user_module acl_user_module =
	MODULE_CONTEXT_INIT(&mail_user_module_register);

static int
acl_mailbox_create(struct mail_storage *storage, struct mailbox_list *list,
		   const char *name, bool directory)
{
	union mail_storage_module_context *astorage = ACL_CONTEXT(storage);
	int ret;

	if ((list->ns->flags & NAMESPACE_FLAG_NOACL) != 0)
		ret = 1;
	else T_BEGIN {
		ret = acl_mailbox_list_have_right(list, name, TRUE,
						  ACL_STORAGE_RIGHT_CREATE,
						  NULL);
	} T_END;

	if (ret <= 0) {
		if (ret == 0) {
			/* Note that if the mailbox didn't have LOOKUP
			   permission, this not reveals to user the mailbox's
			   existence. Can't help it. */
			mail_storage_set_error(storage, MAIL_ERROR_PERM,
					       MAIL_ERRSTR_NO_PERMISSION);
		} else {
			mail_storage_set_internal_error(storage);
		}
		return -1;
	}

	return astorage->super.mailbox_create(storage, list, name, directory);
}

void acl_mail_storage_created(struct mail_storage *storage)
{
	struct acl_user *auser = ACL_USER_CONTEXT(storage->user);
	union mail_storage_module_context *astorage;

	if (auser == NULL) {
		/* ACLs disabled for this user */
	} else {
		astorage = p_new(storage->pool,
				 union mail_storage_module_context, 1);
		astorage->super = storage->v;
		storage->v.mailbox_alloc = acl_mailbox_alloc;
		storage->v.mailbox_create = acl_mailbox_create;

		MODULE_CONTEXT_SET_SELF(storage, acl_storage_module, astorage);
	}

	if (acl_next_hook_mail_storage_created != NULL)
		acl_next_hook_mail_storage_created(storage);
}

static void acl_user_deinit(struct mail_user *user)
{
	struct acl_user *auser = ACL_USER_CONTEXT(user);

	acl_lookup_dict_deinit(&auser->acl_lookup_dict);
	auser->module_ctx.super.deinit(user);
}

static void acl_mail_user_create(struct mail_user *user, const char *env)
{
	struct acl_user *auser;

	auser = p_new(user->pool, struct acl_user, 1);
	auser->module_ctx.super = user->v;
	user->v.deinit = acl_user_deinit;
	auser->acl_lookup_dict = acl_lookup_dict_init(user);

	auser->acl_env = env;
	auser->master_user = mail_user_plugin_getenv(user, "master_user");

	env = mail_user_plugin_getenv(user, "acl_groups");
	if (env != NULL) {
		auser->groups =
			(const char *const *)p_strsplit(user->pool, env, ",");
	}

	MODULE_CONTEXT_SET(user, acl_user_module, auser);
}

void acl_mail_user_created(struct mail_user *user)
{
	const char *env;

	env = mail_user_plugin_getenv(user, "acl");
	if (env != NULL)
		acl_mail_user_create(user, env);
	else {
		if (user->mail_debug)
			i_info("acl: No acl setting - ACLs are disabled");
	}

	if (acl_next_hook_mail_user_created != NULL)
		acl_next_hook_mail_user_created(user);
}

