/* Hey EMACS -*- linux-c -*- */
/* $Id: calc_89t.c 3810 2007-09-25 19:14:30Z roms $ */

/*  libticalcs2 - hand-helds support library, a part of the TiLP project
 *  Copyright (C) 1999-2005  Romain Lievin
 *  Copyright (C) 2006  Kevin Kofler
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	NSPire support thru DirectUsb link.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ticonv.h>
#include "ticalcs.h"
#include "gettext.h"
#include "logging.h"
#include "error.h"
#include "pause.h"
#include "macros.h"

#include "nsp_vpkt.h"
#include "nsp_cmd.h"

static int		is_ready	(CalcHandle* handle)
{
	static int checked = 0;

	// Init once
	if(!checked)
	{
		TRYF(nsp_addr_request(handle));
		TRYF(nsp_addr_assign(handle, NSP_DEV_ADDR));
		checked++;
	}

	// Use ECHO packet for is_ready

	return 0;
}

static int		send_key	(CalcHandle* handle, uint16_t key)
{
	return 0;
}

static int		execute		(CalcHandle* handle, VarEntry *ve, const char *args)
{
	return 0;
}

static uint8_t* rle_uncompress(CalcScreenCoord* sc, const uint8_t *src, uint32_t size)
{
	uint8_t *dst = g_malloc(sc->width * sc->height / 2);
	uint8_t *q;
	int i;

	for(i = 0, q = dst; i < (int)size;)
	{
		int8_t rec = src[i++];

		if(rec >= 0)
		{
			uint8_t cnt = ((uint8_t )rec) + 1;
			uint8_t val = src[i++];

			memset(q, val, cnt);
			q += cnt;
		}
		else
		{
			uint8_t cnt = -(rec - 1);

			memcpy(q, src+i, cnt);
			q += cnt;
			i += cnt;
		}
	}	

	return dst;
}

static int		recv_screen	(CalcHandle* handle, CalcScreenCoord* sc, uint8_t** bitmap)
{
	uint32_t size;
	uint8_t cmd, *data;

	TRYF(nsp_session_open(handle, SID_SCREEN_RLE));

	TRYF(cmd_s_screen_rle(handle, 0));
	TRYF(cmd_r_screen_rle(handle, &cmd, &size, &data));
	sc->width = sc->clipped_width = (data[8] << 8) | data[9];
	sc->height = sc->clipped_height = (data[10] << 8) | data[11];
	TRYF(cmd_r_screen_rle(handle, &cmd, &size, &data));

	TRYF(nsp_session_close(handle));

	*bitmap = rle_uncompress(sc, data, size);
	g_free(data);

	if(*bitmap == NULL) 
		return ERR_MALLOC;	

	return 0;
}

static int		get_dirlist	(CalcHandle* handle, GNode** vars, GNode** apps)
{
	TreeInfo *ti;
	int err;
	GNode *root, *folder = NULL;
	char varname[40];
	uint32_t varsize;
	uint8_t vartype;
	int i;

	(*apps) = g_node_new(NULL);
	ti = (TreeInfo *)g_malloc(sizeof(TreeInfo));
	ti->model = handle->model;
	ti->type = APP_NODE_NAME;
	(*apps)->data = ti;

    (*vars) = g_node_new(NULL);
	ti = (TreeInfo *)g_malloc(sizeof(TreeInfo));
	ti->model = handle->model;
	ti->type = VAR_NODE_NAME;
	(*vars)->data = ti;

	root = g_node_new(NULL);
	g_node_append(*apps, root);

	TRYF(nsp_session_open(handle, SID_FILE_MGMT));

	TRYF(cmd_s_dir_enum_init(handle, "/"));
	TRYF(cmd_r_dir_enum_init(handle));

	for(;;)
	{
		VarEntry *fe = tifiles_ve_create();
		GNode *node;

		TRYF(cmd_s_dir_enum_next(handle));
		err = cmd_r_dir_enum_next(handle, varname, &varsize, &vartype);

		if (err == ERR_EOT)
			break;
		else if (err != 0)
			return err;
		
		strcpy(fe->folder, varname);
		strcpy(fe->name, varname);
		fe->size = varsize;
		fe->type = vartype;
		fe->attr = ATTRB_NONE;

		node = g_node_new(fe);
		folder = g_node_append(*vars, node);

		ticalcs_info(_("Name: %s | Type: %i | Attr: %i  | Size: %08X"), 
			fe->name, 
			fe->type,
			fe->attr,
			fe->size);
	}

	TRYF(cmd_s_dir_enum_done(handle));
	TRYF(cmd_r_dir_enum_done(handle));

	for(i = 0; i < (int)g_node_n_children(*vars); i++) 
	{
		GNode *folder = g_node_nth_child(*vars, i);
		char *folder_name = ((VarEntry *) (folder->data))->name;
		char *u1, *u2;

		ticalcs_info(_("Directory listing in <%s>..."), folder_name);

		TRYF(cmd_s_dir_enum_init(handle, folder_name));
		TRYF(cmd_r_dir_enum_init(handle));

		for(;;)
		{
			VarEntry *ve = tifiles_ve_create();
			GNode *node;
			char *ext;

			TRYF(cmd_s_dir_enum_next(handle));
			err = cmd_r_dir_enum_next(handle, varname, &varsize, &vartype);

			if (err == ERR_EOT)
				break;
			else if (err != 0)
				return err;
		
			ext = tifiles_fext_get(varname);
			strcpy(ve->folder, folder_name);
			ve->size = varsize;			
			ve->type = tifiles_fext2vartype(handle->model, ext);
			ve->attr = ATTRB_NONE;
			if(ext) *(ext-1) = '\0';
			strcpy(ve->name, varname);

			node = g_node_new(ve);
			g_node_append(folder, node);

			ticalcs_info(_("Name: %8s | Type: %8s | Attr: %i  | Size: %08X"), 
				ve->name, 
				tifiles_vartype2string(handle->model, ve->type),
				ve->attr,
				ve->size);

			u1 = ticonv_varname_to_utf8(handle->model, ((VarEntry *) (folder->data))->name, -1);
			u2 = ticonv_varname_to_utf8(handle->model, ve->name, ve->type);
			g_snprintf(update_->text, sizeof(update_->text), _("Parsing %s/%s"), u1, u2);
			g_free(u1); g_free(u2);
			update_label();
		}

		TRYF(cmd_s_dir_enum_done(handle));
		TRYF(cmd_r_dir_enum_done(handle));
	}	

	TRYF(nsp_session_close(handle));

	return 0;
}

static int		get_memfree	(CalcHandle* handle, uint32_t* ram, uint32_t* flash)
{
	return 0;
}

static int		send_var	(CalcHandle* handle, CalcMode mode, FileContent* content)
{


	return 0;	
}

static int		recv_var	(CalcHandle* handle, CalcMode mode, FileContent* content, VarRequest* vr)
{
	char *path;
	uint8_t *data;
	VarEntry *ve;
	char *utf8;

	utf8 = ticonv_varname_to_utf8(handle->model, vr->name, vr->type);
	g_snprintf(update_->text, sizeof(update_->text), "%s", utf8);
	g_free(utf8);
	update_label();

	TRYF(nsp_session_open(handle, SID_FILE_MGMT));

	path = g_strconcat("/", vr->folder, "/", vr->name, ".", 
		tifiles_vartype2fext(handle->model, vr->type), NULL);
	TRYF(cmd_s_get_file(handle, path));
	TRYF(cmd_r_get_file(handle, &(vr->size)));

	TRYF(cmd_s_file_ok(handle));
	TRYF(cmd_r_file_contents(handle, NULL, &data));
	TRYF(cmd_s_status(handle, ERR_OK));

	content->model = handle->model;
	strcpy(content->comment, tifiles_comment_set_single());
    content->num_entries = 1;

    content->entries = tifiles_ve_create_array(1);	
    ve = content->entries[0] = tifiles_ve_create();
    memcpy(ve, vr, sizeof(VarEntry));

	ve->data = tifiles_ve_alloc_data(ve->size);
	memcpy(ve->data, data, ve->size);
	g_free(data);

	TRYF(nsp_session_close(handle));
	g_free(path);

	return 0;
}

static int		send_backup	(CalcHandle* handle, BackupContent* content)
{
	return 0;
}

static int		send_var_ns	(CalcHandle* handle, CalcMode mode, FileContent* content)
{
	return 0;
}

static int		recv_var_ns	(CalcHandle* handle, CalcMode mode, FileContent* content, VarEntry** ve)
{
	return 0;
}

static int		send_flash	(CalcHandle* handle, FlashContent* content)
{
	return 0;
}

static int		recv_flash	(CalcHandle* handle, FlashContent* content, VarRequest* vr)
{
	return 0;
}

static int		send_os    (CalcHandle* handle, FlashContent* content)
{

	return 0;
}

static int		recv_idlist	(CalcHandle* handle, uint8_t* id)
{
	uint32_t size;
	uint8_t cmd, *data;

	TRYF(nsp_session_open(handle, SID_DEV_INFOS));

	TRYF(cmd_s_dev_infos(handle, CMD_DI_VERSION));
	TRYF(cmd_r_dev_infos(handle, &cmd, &size, &data));

	strncpy(id, (char*)(data + 84), 28);

	g_free(data);
	TRYF(nsp_session_close(handle));

	return 0;
}

static int		dump_rom_1	(CalcHandle* handle)
{

	return 0;
}

static int		dump_rom_2	(CalcHandle* handle, CalcDumpSize size, const char *filename)
{

	return 0;
}

static int		set_clock	(CalcHandle* handle, CalcClock* clock)
{
	return 0;
}

static int		get_clock	(CalcHandle* handle, CalcClock* clock)
{


	return 0;
}

static int		del_var		(CalcHandle* handle, VarRequest* vr)
{

	return 0;
}

static int		new_folder  (CalcHandle* handle, VarRequest* vr)
{



	return 0;
}

static int		get_version	(CalcHandle* handle, CalcInfos* infos)
{
	uint32_t size;
	uint8_t cmd, *data;
	int i;

	TRYF(nsp_session_open(handle, SID_DEV_INFOS));

	TRYF(cmd_s_dev_infos(handle, CMD_DI_MODEL));
	TRYF(cmd_r_dev_infos(handle, &cmd, &size, &data));

	strncpy(infos->product_name, (char*)data, 10);
	infos->mask |= INFOS_MAIN_CALC_ID;

	TRYF(cmd_s_dev_infos(handle, CMD_DI_VERSION));
	TRYF(cmd_r_dev_infos(handle, &cmd, &size, &data));

	i = 4;
	infos->ram_free = GINT32_FROM_BE(*((uint64_t *)(data + i)));
	infos->mask |= INFOS_RAM_FREE;

	i = 36;
	g_snprintf(infos->os_version, sizeof(infos->boot_version), "%1i.%1i.%04i", 
		data[i+0], data[i+1], (data[i+2] << 8) | data[i+3]);
	infos->mask |= INFOS_OS_VERSION;

	i = 40;
	g_snprintf(infos->boot_version, sizeof(infos->boot_version), "%1i.%1i.%04i", 
		data[i+0], data[i+1], (data[i+2] << 8) | data[i+3]);
	infos->mask |= INFOS_BOOT_VERSION;

	i = 44;
	g_snprintf(infos->boot2_version, sizeof(infos->boot_version), "%1i.%1i.%04i", 
		data[i+0], data[i+1], (data[i+2] << 8) | data[i+3]);
	infos->mask |= INFOS_BOOT2_VERSION;

	i = 82;
	strncpy(infos->main_calc_id, (char*)(data + 84), 28);
	infos->mask |= INFOS_MAIN_CALC_ID;

	g_free(data);
	TRYF(nsp_session_close(handle));
	
	return 0;
}

static int		send_cert	(CalcHandle* handle, FlashContent* content)
{
	return 0;
}

static int		recv_cert	(CalcHandle* handle, FlashContent* content)
{
	return 0;
}

extern int tixx_recv_backup(CalcHandle* handle, BackupContent* content);

const CalcFncts calc_nsp = 
{
	CALC_NSPIRE,
	"NSPire",
	"NSpire handheld",
	N_("NSPire thru DirectLink"),
	OPS_ISREADY | OPS_VERSION | OPS_SCREEN | OPS_IDLIST | OPS_DIRLIST | OPS_VARS,
	{"", "", "1P", "1L", "", "2P1L", "2P1L", "2P1L", "1P1L", "2P1L", "1P1L", "2P1L", "2P1L",
		"2P", "1L", "2P", "", "", "1L", "1L", "", "1L", "1L" },
	&is_ready,
	&send_key,
	&execute,
	&recv_screen,
	&get_dirlist,
	&get_memfree,
	&send_backup,
	&tixx_recv_backup,
	&send_var,
	&recv_var,
	&send_var_ns,
	&recv_var_ns,
	&send_flash,
	&recv_flash,
	&send_os,
	&recv_idlist,
	&dump_rom_1,
	&dump_rom_2,
	&set_clock,
	&get_clock,
	&del_var,
	&new_folder,
	&get_version,
	&send_cert,
	&recv_cert,
};
