/*
 * Written 1997 Jens Ch. Restemeier <jchrr@hrz.uni-bielefeld.de>
 * This program is based on an algorithm / article by
 * J�rn Loviscach.
 *
 * It appeared in c't 10/95, page 326 and is called 
 * "Ausgew�rfelt - Moderne Kunst algorithmisch erzeugen".
 * (~modern art created with algorithms)
 * 
 * It generates one main formula (the middle button) and 8 variations of it.
 * If you select a variation it becomes the new main formula. If you
 * press "OK" the main formula will be applied to the image.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

/*
 * History:
 * 1.0 first release
 * 1.2 now handles RGB*
 * 1.5 fixed a small bug
 * 1.6 fixed a bug that was added by v1.5 :-(
 * 1.7 added patch from Art Haas to make it compile with HP-UX, a small clean-up
 * 1.8 Dscho added transform file load/save, bug-fixes 
 * 1.9 rewrote renderloop.
 * 1.9a fixed a bug.
 * 1.9b fixed MAIN()
 * 1.10 added optimizer
 */
                 
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

/** qbist renderer ***********************************************************/

#define MAX_TRANSFORMS	36
#define NUM_TRANSFORMS	9
#define NUM_REGISTERS	6

#define PLUG_IN_NAME "plug_in_qbist"
#define PLUG_IN_VERSION "March 1998, 1.10"
#define PREVIEW_SIZE 64

/** types *******************************************************************/

typedef gfloat vreg[3];

typedef struct _info {
	int transformSequence	[MAX_TRANSFORMS];
	int source		[MAX_TRANSFORMS];
	int control		[MAX_TRANSFORMS];
	int dest		[MAX_TRANSFORMS];
} s_info;

#define PROJECTION	0
#define SHIFT		1
#define SHIFTBACK	2
#define ROTATE		3
#define ROTATE2		4
#define MULTIPLY	5
#define SINE		6
#define CONDITIONAL	7
#define COMPLEMENT	8

/** prototypes **************************************************************/

void query(void);
void run(char *name, int nparams, GParam *param, int *nreturn_vals, GParam **return_vals);

void dialog_cancel(GtkWidget *widget, gpointer data);
void dialog_new_variations(GtkWidget *widget, gpointer data);
void dialog_update_previews(GtkWidget *widget, gpointer data);
void dialog_select_preview (GtkWidget *widget, s_info *n_info);
int dialog_create(void);

s_info qbist_info;

/** qbist functions *********************************************************/

void create_info(s_info *info)
{
	int k;
	for (k=0; k<MAX_TRANSFORMS; k++) {
		info->transformSequence[k]=rand() % NUM_TRANSFORMS;
		info->source[k]=rand() % NUM_REGISTERS;
		info->control[k]=rand() % NUM_REGISTERS;
		info->dest[k]=rand() % NUM_REGISTERS;
	}
	info->dest[rand() % MAX_TRANSFORMS]=0;
}

void modify_info(s_info *o_info, s_info *n_info)
{
	int k, n; 
	memcpy(n_info, o_info, sizeof(s_info)); 
	n=rand() % MAX_TRANSFORMS;
	for (k=0;k<n; k++) {
		switch (rand() % 4) {
			case 0: n_info->transformSequence[rand() % MAX_TRANSFORMS] = rand() % NUM_TRANSFORMS; break;
			case 1: n_info->source[rand() % MAX_TRANSFORMS]            = rand() % NUM_REGISTERS; break;
			case 2: n_info->control[rand() % MAX_TRANSFORMS]           = rand() % NUM_REGISTERS; break;
			case 3: n_info->dest[rand() % MAX_TRANSFORMS]              = rand() % NUM_REGISTERS; break;
		}
	}
}

/*
 * Optimizer
 */
int used_trans_flag[MAX_TRANSFORMS];
int used_reg_flag[NUM_REGISTERS];

void check_last_modified(s_info info, int p, int n)
{
	p--;
	while ((p>=0) && (info.dest[p]!=n)) p--;
	if (p<0) 
		used_reg_flag[n]=1;
	else {
		used_trans_flag[p]=1;
		check_last_modified(info, p, info.source[p]);
		check_last_modified(info, p, info.control[p]);
	}
}

void optimize(s_info info)
{
	int i;
	/* double-arg fix: */
	for (i=0; i<MAX_TRANSFORMS; i++) {
		used_trans_flag[i]=0;
		if (i<NUM_REGISTERS)
			used_reg_flag[i]=0;
		/* double-arg fix: */
		switch (info.transformSequence[i]) {
			case ROTATE: 
			case ROTATE2: 
			case COMPLEMENT: 
				info.control[i]=info.dest[i];
				break;
		}
	}
	/* check for last modified item */
	check_last_modified(info, MAX_TRANSFORMS, 0);
}

void qbist(s_info info, gchar *buffer, int xp, int yp, int num, int width, int height, int bpp)
{
	gushort gx;
	vreg reg [NUM_REGISTERS];
	int i;
	gushort sr, cr, dr;

	if (num<=0) return;

	for(gx=0; gx<num; gx ++) {
		for(i=0; i<NUM_REGISTERS; i++) {
			if (used_reg_flag[i]) {
				reg[i][0] = ((float)gx+xp) / ((float)(width));
				reg[i][1] = ((float)yp) / ((float)(height));
				reg[i][2] = ((float)i) / ((float)NUM_REGISTERS);
			}
		}
		for(i=0;i<MAX_TRANSFORMS; i++) {
			sr=info.source[i];cr=info.control[i];dr=info.dest[i];
			
			if (used_trans_flag[i]) switch (info.transformSequence[i]) {
				case PROJECTION: {
					gfloat scalarProd;
					scalarProd = (reg[sr][0]*reg[cr][0])+(reg[sr][1]*reg[cr][1])+(reg[sr][2]*reg[cr][2]);
					reg[dr][0] = scalarProd*reg[sr][0];
					reg[dr][1] = scalarProd*reg[sr][1];
					reg[dr][2] = scalarProd*reg[sr][2];
					break;
				}
				case SHIFT: 
					reg[dr][0] = reg[sr][0]+reg[cr][0];
					if (reg[dr][0] >= 1.0) reg[dr][0] -= 1.0;
					reg[dr][1] = reg[sr][1]+reg[cr][1];
					if (reg[dr][1] >= 1.0) reg[dr][1] -= 1.0;
					reg[dr][2] = reg[sr][2]+reg[cr][2];
					if (reg[dr][2] >= 1.0) reg[dr][2] -= 1.0;
					break;
				case SHIFTBACK: 
					reg[dr][0] = reg[sr][0]-reg[cr][0];
					if (reg[dr][0] <= 0.0) reg[dr][0] += 1.0;
					reg[dr][1] = reg[sr][1]-reg[cr][1];
					if (reg[dr][1] <= 0.0) reg[dr][1] += 1.0;
					reg[dr][2] = reg[sr][2]-reg[cr][2];
					if (reg[dr][2] <= 0.0) reg[dr][2] += 1.0;
					break;
				case ROTATE: 
					reg[dr][0] = reg[sr][1];
					reg[dr][1] = reg[sr][2];
					reg[dr][2] = reg[sr][0];
					break;
				case ROTATE2: 
					reg[dr][0] = reg[sr][2];
					reg[dr][1] = reg[sr][0];
					reg[dr][2] = reg[sr][1];
					break;
				case MULTIPLY: 
					reg[dr][0] = reg[sr][0]*reg[cr][0];
					reg[dr][1] = reg[sr][1]*reg[cr][1];
					reg[dr][2] = reg[sr][2]*reg[cr][2];
					break;
				case SINE: 
					reg[dr][0] = 0.5+(0.5*sin(20.0*reg[sr][0]*reg[cr][0]));
					reg[dr][1] = 0.5+(0.5*sin(20.0*reg[sr][1]*reg[cr][1]));
					reg[dr][2] = 0.5+(0.5*sin(20.0*reg[sr][2]*reg[cr][2]));
					break;
				case CONDITIONAL: 
					if ((reg[cr][0]+reg[cr][1]+reg[cr][2]) > 0.5)	{
						reg[dr][0] = reg[sr][0];
						reg[dr][1] = reg[sr][1];
						reg[dr][2] = reg[sr][2];
					} else {
						reg[dr][0] = reg[cr][0];
						reg[dr][1] = reg[cr][1];
						reg[dr][2] = reg[cr][2];
					}
					break;
				case COMPLEMENT: 
					reg[dr][0] = 1.0-reg[sr][0];
					reg[dr][1] = 1.0-reg[sr][1];
					reg[dr][2] = 1.0-reg[sr][2];
					break;
			}
		}
		for (i=0; i<bpp; i++) {
			if (i<3) { 
				int a;
				a=255.0 * reg[0][i];
				buffer[i]=(a<0) ? 0 : ((a>255) ? 255 : a); 
			} else {
				buffer[i]=255;
			}
		}
		buffer+=bpp;
	}
}

/** Plugin interface *********************************************************/

GPlugInInfo PLUG_IN_INFO = {
	NULL,	/* init_proc */
	NULL,	/* quit_proc */
	query,	/* query_proc */
	run	/* run_proc */
};

/* Definition of parameters */
GParamDef args[] = {
	{ PARAM_INT32, "run_mode", "Interactive, non-interactive" },
	{ PARAM_IMAGE, "image", "Input image (unused)" },
	{ PARAM_DRAWABLE, "drawable", "Input drawable" }
};

GParamDef *return_vals  = NULL;
int        nargs        = sizeof(args) / sizeof(args[0]);
int        nreturn_vals = 0;

MAIN()

void query(void)
{
        gimp_install_procedure(PLUG_IN_NAME, 
                               "Create images based on a random genetic formula", 
                               "This Plug-in is based on an article by "
                               "J�rn Loviscach (appeared in c't 10/95, page 326). It generates modern art "
                               "pictures from a random genetic formula.", 
                               "J�rn Loviscach, Jens Ch. Restemeier", 
                               "J�rn Loviscach, Jens Ch. Restemeier", 
                               PLUG_IN_VERSION, 
                               "<Image>/Filters/Render/Qbist", 
                               "RGB*", 
                               PROC_PLUG_IN, 
                               nargs, 
                               nreturn_vals, 
                               args, 
                               return_vals);
}

/* Return values */
GParam values[1];

void run(char *name, int nparams, GParam *param, int *nreturn_vals, GParam **return_vals)
{
	gint sel_x1, sel_y1, sel_x2, sel_y2;
	gint img_height, img_width, img_bpp, img_has_alpha;

	GDrawable 	*drawable;
        GRunModeType	run_mode;
        GStatusType	status;

        *nreturn_vals = 1;
        *return_vals  = values;

        status = STATUS_SUCCESS;
	
	if (param[0].type!=PARAM_INT32)
		status=STATUS_CALLING_ERROR;
        run_mode = param[0].data.d_int32;

        if (param[2].type!=PARAM_DRAWABLE)
		status=STATUS_CALLING_ERROR;
        drawable = gimp_drawable_get(param[2].data.d_drawable);

        img_width     = gimp_drawable_width(drawable->id);
        img_height    = gimp_drawable_height(drawable->id);
        img_bpp       = gimp_drawable_bpp(drawable->id);
        img_has_alpha = gimp_drawable_has_alpha(drawable->id);
        gimp_drawable_mask_bounds(drawable->id, &sel_x1, &sel_y1, &sel_x2, &sel_y2);

	if (!gimp_drawable_color(drawable->id))
		status=STATUS_CALLING_ERROR;
		
	if (status==STATUS_SUCCESS) {
		create_info(&qbist_info);
        	switch (run_mode) {
        	        case RUN_INTERACTIVE:
        	                /* Possibly retrieve data */
        	                gimp_get_data(PLUG_IN_NAME, &qbist_info);
        	
        	                /* Get information from the dialog */
        	                if (dialog_create()) {
        	                	status=STATUS_SUCCESS;
		                        gimp_set_data(PLUG_IN_NAME, &qbist_info, sizeof(s_info));
				} else
					status=STATUS_EXECUTION_ERROR;
        	                break;

        	        case RUN_NONINTERACTIVE:
				status=STATUS_CALLING_ERROR;
        	                break;
	
	                case RUN_WITH_LAST_VALS:
	                        /* Possibly retrieve data */
	                        gimp_get_data(PLUG_IN_NAME, &qbist_info);
	                        status=STATUS_SUCCESS;
	                        break;
	                default:
				status=STATUS_CALLING_ERROR;
	                        break;
	        } 
        	if (status == STATUS_SUCCESS) {
        		GPixelRgn imagePR;
			guchar *row_data;
			gint row;

	                gimp_tile_cache_ntiles((drawable->width + gimp_tile_width() - 1) / gimp_tile_width());
			gimp_pixel_rgn_init (&imagePR, drawable, 0,0, img_width, img_height, TRUE, TRUE);
			row_data=(guchar *)malloc((sel_x2-sel_x1)*img_bpp);

			optimize(qbist_info);

			gimp_progress_init ("Qbist ...");
			for (row=sel_y1; row<sel_y2; row++) {
				qbist(qbist_info, (gchar *)row_data, 0, row, sel_x2-sel_x1, sel_x2-sel_x1, sel_y2-sel_y1, img_bpp);
				gimp_pixel_rgn_set_row(&imagePR, row_data, sel_x1, row, (sel_x2-sel_x1));
				if ((row % 5) == 0) 
					gimp_progress_update((gfloat)(row-sel_y1)/(gfloat)(sel_y2-sel_y1));
			}

			free(row_data);
			gimp_drawable_flush (drawable);
			gimp_drawable_merge_shadow (drawable->id, TRUE);
			gimp_drawable_update (drawable->id, sel_x1, sel_y1, (sel_x2 - sel_x1), (sel_y2 - sel_y1));
			            				
                        gimp_displays_flush();
	        } 
	}
	
	values[0].type = PARAM_STATUS;
        values[0].data.d_status = status;
        gimp_drawable_detach(drawable);
}

/** User interface ***********************************************************/

GtkWidget *preview[9];
s_info info[9];
gint result;

void dialog_close(GtkWidget *widget, gpointer data)
{
	gimp_main_loop_quit();
}

void dialog_cancel(GtkWidget *widget, gpointer data)
{
	gtk_window_destroy(GTK_WINDOW(data));
}

void dialog_ok(GtkWidget *widget, gpointer data)
{
	result=TRUE;
	gtk_window_destroy(GTK_WINDOW(data));
}

void dialog_new_variations(GtkWidget *widget, gpointer data)
{
	int i;
	for (i=1; i<9; i++)
		modify_info(&(info[0]), &(info[i]));
}

void dialog_update_previews(GtkWidget *widget, gpointer data)
{
	int i, j;
	guchar buf[PREVIEW_SIZE * 3];
	for (j=0;j<9;j++) {
		optimize(info[(j+5) % 9]);
		for (i = 0; i < PREVIEW_SIZE; i++) {
			qbist(info[(j+5) % 9], (gchar *)buf, 0, i, PREVIEW_SIZE, PREVIEW_SIZE, PREVIEW_SIZE, 3);
			gimp_preview_draw_row (GIMP_PREVIEW (preview[j]), buf, 0, i, PREVIEW_SIZE);
		}
	}
}

void dialog_select_preview (GtkWidget *widget, s_info *n_info)
{
	memcpy(&(info[0]), n_info, sizeof(s_info));
	dialog_new_variations(widget, NULL);
	dialog_update_previews(widget, NULL);
}

/* File I/O stuff */

#define LOBITE(x) ((x)&0xff)
#define HIBITE(x) ((x)>>8)
#define PUTMACUSHORT(u, f) fprintf(f, "%c%c", HIBITE(u), LOBITE(u));

/* Big-endian 16 bit value.  The two reads have to be sequenced: the
 * order of evaluation of the operands of + is unspecified.
 */
static int getmacushort(FILE *f)
{
	int hi, lo;

	hi=fgetc(f);
	lo=fgetc(f);
	return (hi<<8)+lo;
}

int load_data(const char *name)
{
	int i;
	FILE *f;
	f=g_fopen(name, "rb");
	if(f==NULL) return(0);
	for(i=0;i<MAX_TRANSFORMS;i++) info[0].transformSequence[i]=getmacushort(f);
	for(i=0;i<MAX_TRANSFORMS;i++) info[0].source[i]=getmacushort(f);
	for(i=0;i<MAX_TRANSFORMS;i++) info[0].control[i]=getmacushort(f);
	for(i=0;i<MAX_TRANSFORMS;i++) info[0].dest[i]=getmacushort(f);
	fclose(f);
	return(1);
}

void save_data(const char *name)
{
	int i=0;
	FILE *f;

	f=g_fopen(name, "wb");
	if(f==NULL) return;
	for(i=0;i<MAX_TRANSFORMS;i++) PUTMACUSHORT(info[0].transformSequence[i], f);
	for(i=0;i<MAX_TRANSFORMS;i++) PUTMACUSHORT(info[0].source[i], f);
	for(i=0;i<MAX_TRANSFORMS;i++) PUTMACUSHORT(info[0].control[i], f);
	for(i=0;i<MAX_TRANSFORMS;i++) PUTMACUSHORT(info[0].dest[i], f);
	fclose(f);
}

static void file_selection_save(const gchar *filename, gpointer data)
{
	if (filename)
		save_data(filename);
}

static void file_selection_load(const gchar *filename, gpointer data)
{
	if (filename == NULL)
		return;
	load_data(filename);
	dialog_new_variations(NULL, NULL);
	dialog_update_previews(NULL, NULL);
}

void dialog_load(GtkWidget *widget, gpointer d)
{
	gimp_file_dialog_open(GTK_WINDOW(d), "Load QBE file...", NULL,
			      file_selection_load, NULL);
}

void dialog_save(GtkWidget *widget, gpointer d)
{
	gimp_file_dialog_save(GTK_WINDOW(d), "Save (middle transform) as QBE file...",
			      NULL, file_selection_save, NULL);
}


int dialog_create(void)
{
	GtkWidget *dialog;
	GtkWidget *button;
	GtkWidget *table;

	int i;

	srand(time(NULL));

	gtk_init ();

	dialog=gimp_dialog_new ("G-Qbist 1.10");
	g_signal_connect (dialog, "destroy",
		G_CALLBACK (dialog_close),
		NULL);

	table=gimp_table_new (3, 3, FALSE);
	gtk_grid_set_row_spacing(GTK_GRID(table), 5);
	gtk_grid_set_column_spacing(GTK_GRID(table), 5);
	gimp_container_set_border_width (table, 5);
	gimp_box_pack_start(gimp_dialog_get_vbox(dialog), table, TRUE, TRUE, 0);

	memcpy((char *)&(info[0]), (char *)&qbist_info, sizeof(s_info));
	dialog_new_variations(NULL, NULL);

	for (i=0; i<9; i++) {

		button=gtk_button_new();
		g_signal_connect (button, "clicked",
			G_CALLBACK (dialog_select_preview), (gpointer) &(info[(i+5)%9]));
		gimp_table_attach(table, button, i%3, (i%3)+1, i/3, (i/3)+1,
			GIMP_EXPAND | GIMP_FILL, GIMP_EXPAND | GIMP_FILL, 0, 0);

		preview[i] = gimp_preview_new(GIMP_PREVIEW_COLOR);
		gimp_preview_size(GIMP_PREVIEW(preview[i]), PREVIEW_SIZE, PREVIEW_SIZE);
		gtk_button_set_child(GTK_BUTTON(button), preview[i]);
	}

	dialog_update_previews(NULL, NULL);

	gimp_dialog_add_button (dialog, "OK", G_CALLBACK (dialog_ok),
				dialog, FALSE);
	gimp_dialog_add_button (dialog, "Load", G_CALLBACK (dialog_load),
				dialog, FALSE);
	gimp_dialog_add_button (dialog, "Save", G_CALLBACK (dialog_save),
				dialog, FALSE);
	gimp_dialog_add_button (dialog, "Cancel", G_CALLBACK (dialog_cancel),
				dialog, TRUE);

	gtk_window_present(GTK_WINDOW(dialog));

	result=FALSE;

	gimp_main_loop_run();

	if (result)
		memcpy((char *)&qbist_info, (char *)&(info[0]), sizeof(s_info));
	return result;
}

