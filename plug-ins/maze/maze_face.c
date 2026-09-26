/* maze_face.c
 * User interface for plug-in-maze.
 * 
 * Implemented as a GIMP 0.99 Plugin by 
 * Kevin Turner <kevint@poboxes.com>
 * http://www.poboxes.com/kevint/gimp/maze.html
 */

/*
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "maze.h"
#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#define BORDER_TOLERANCE 1.00 /* maximum ratio of (max % divs) to width */
#define ENTRY_WIDTH 75

/* entscale stuff begin */
#define ENTSCALE_INT_SCALE_WIDTH 125
#define ENTSCALE_INT_ENTRY_WIDTH 40

typedef void (*EntscaleIntCallbackFunc) (gint value, gpointer data);

typedef struct {
  GtkAdjustment     *adjustment;
  GtkWidget     *entry;
  gint          constraint;
  EntscaleIntCallbackFunc	callback;
  gpointer	call_data;
} EntscaleIntData;
/* entscale stuff end */

gint maze_dialog (void);

static void maze_msg (gchar *msg);
static void maze_close_callback (GtkWidget *widget, gpointer data);
static void maze_ok_callback  (GtkWidget *widget, gpointer data);
static void maze_entry_callback  (GtkWidget *widget, gpointer data);
static void maze_help (GtkWidget *widget, gpointer foo);

/* Looking back, it would probably have been easier to completely
 * re-write the whole entry/scale thing to work with the divbox stuff.
 * It would undoubtably be cleaner code.  But since I already *had*
 * the entry/scale routines, I was under the (somewhat mistaken)
 * impression that it would be easier to work with them... */

/* Now, it goes like this:

   To update entscale (width) when div_entry changes:

    entscale_int_new has been slightly modified to return a pointer to
      its entry widget.

    This is fed to divbox_new as a "friend", which is in turn fed to
      the div_entry_callback routine.  And that's not really so bad,
      except...

    Oh, well, maybe it isn't so bad.  We can play with our friend's
      userdata to block his callbacks so we don't get feedback loops,
      that works nicely enough.

   To update div_entry when entscale (width) changes:

    The entry/scale setup graciously provides for callbacks.  However,
      this means we need to know about div_entry when we set up
      entry/scale, which we don't...  Chicken and egg problem.  So we
      set up a pointer to where div_entry will be, and pass this
      through to divbox_new when it happens.

    We need to block signal handlers for div_entry this time.  We
      happen to know that div_entry's callback data is our old
      "friend", so we pull our friend out from where we stuck him in
      the entry's userdata...  Hopefully that does it.  */

/* Questions:

     Gosh that was dumb.  Is there a way to
       signal_handler_block_by_name?
     That would make life so much nicer.

     Pointing to static variables "less" and "more" (for the buttons
     in divbox_new) is stupid.  Is there a way to store integer values
     in userdata or use intergers as parameters to callbacks?  The
     only alternative I could think of was seperate "button_less" and
     "button_more" callbacks, which did nothing but pass data on to
     what is now the div_button_callback function with an additional
     -1 or 1 parameter...  And that idea was at least as brain-damaged. 

*/

static void div_button_callback (GtkWidget *button, GtkWidget *entry);
static void div_entry_callback (GtkWidget *entry, GtkWidget *friend);
static void height_width_callback (gint width, GtkWidget **div_entry);
static void toggle_callback (GtkWidget *widget, gboolean *data);
static void alg_radio_callback (GtkWidget *widget, gpointer data);

static GtkWidget* divbox_new (guint *max,
			      GtkWidget *friend, 
			      GtkWidget **div_entry);

#if 0
static void div_buttonl_callback (GtkAdjustment *object);
static void div_buttonr_callback (GtkAdjustment *object);
#endif 

/* entscale stuff begin */
static GtkWidget*   entscale_int_new ( GtkWidget *table, gint x, gint y,
				       gchar *caption, gint *intvar, 
				       gint min, gint max, gboolean constraint,
				       EntscaleIntCallbackFunc callback,
				       gpointer data );

static void   entscale_int_destroy_callback (GtkWidget *widget,
					     gpointer data);
static void   entscale_int_scale_update (GtkAdjustment *adjustment,
					 gpointer      data);
static void   entscale_int_entry_update (GtkWidget *widget,
					 gpointer   data);
/* entscale stuff end */

extern MazeValues mvals;
extern guint sel_w, sel_h;

static gint maze_run=FALSE;
static GtkWidget *msg_label;

/* I only deal with setting up a few widgets at a time, so I could get
   by on a handful of generic GtkWidget variables.  But I've noticed
   that's not the way things are done around here...  I read it
   enhances optimization or some such thing.  Oh well.  Pointers are
   cheap, right? */
gint maze_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *msg_frame;
  GtkWidget *frame;
  GtkWidget *table;
  gint trow;
  GtkWidget *button;
  GtkWidget *label;
  GtkWidget *entry;
  GtkWidget *notebook;
  GtkWidget *tilecheck;


  GtkWidget *width_entry, *height_entry;
  GtkWidget *seed_hbox, *seed_entry, *time_button;
  GtkWidget *div_x_hbox, *div_y_hbox;
  GtkWidget *div_x_label, *div_y_label, *div_x_entry, *div_y_entry;

  GtkWidget *alg_box, *alg_button;

  gchar buffer[32];


  gtk_init ();

  dlg = gimp_dialog_new (MAZE_TITLE);
  g_signal_connect (dlg, "destroy",
		      G_CALLBACK (maze_close_callback),
		      NULL);

  /*  Action area  */
  button = gimp_dialog_add_button (dlg, "OK", NULL, NULL, TRUE);
  g_signal_connect (button, "clicked",
                      G_CALLBACK (maze_ok_callback),
                      dlg);

  button = gimp_dialog_add_button (dlg, "Cancel", NULL, NULL, FALSE);
  g_signal_connect_swapped (button, "clicked",
			     G_CALLBACK (gtk_window_destroy), dlg);

  gimp_dialog_add_button (dlg, "Help",
			  G_CALLBACK (maze_help), NULL, FALSE);


  /* Create notebook */
  notebook = gtk_notebook_new ();
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), notebook, FALSE, FALSE, 0);

  msg_frame = gtk_frame_new(MAZE_TITLE);
  gimp_container_set_border_width (msg_frame, 5);
  gimp_box_pack_start (gimp_dialog_get_vbox (dlg), msg_frame, FALSE, FALSE, 0);

  sprintf(buffer,"Selection is %dx%d",sel_w, sel_h);
  msg_label = gtk_label_new (buffer);
  gimp_container_add (msg_frame, msg_label);

#if 0
  g_print("label_width: %d, %d\n",
	  GTK_FRAME(msg_frame)->label_width,
	  gdk_string_measure (GTK_WIDGET(msg_frame)->style->font, 
			      GTK_FRAME(msg_frame)->label) + 7);
#endif

  /*  Set up Options page  */
  frame = gtk_frame_new ("Maze Options");
  gimp_container_set_border_width (frame, 5);
  table = gimp_table_new (5, 2, FALSE);
  gimp_container_set_border_width (table, 5);
  gimp_container_add (frame, table);

  trow = 0;

  /* Tileable checkbox */
  tilecheck = gtk_check_button_new_with_label ("Tileable?");
  gtk_check_button_set_active (GTK_CHECK_BUTTON (tilecheck), mvals.tile);
  g_signal_connect (tilecheck, "toggled",
		      G_CALLBACK (toggle_callback), &mvals.tile);
  gimp_table_attach (table, tilecheck, 0, 2, trow, trow+1, 
		    GIMP_FILL, 0, 5, 0 );

  trow++;

  /* entscale == Entry and Scale pair function found in pixelize.c */
  width_entry = entscale_int_new (table, 0, trow, "Width (pixels):", 
				  &mvals.width, 
				  1, sel_w/4, TRUE, 
				  (EntscaleIntCallbackFunc) height_width_callback,
				  &div_x_entry);


  /* Number of Divisions entry */
  trow++;

  div_x_label = gtk_label_new("Pieces:");

  gimp_table_attach (table, div_x_label, 0,1, trow, trow+1, 
		    0, 0, 5, 5);


  div_x_hbox = divbox_new(&sel_w, 
			  width_entry,
			  &div_x_entry);

  sprintf(buffer, "%d", (sel_w / mvals.width) );
  gtk_editable_set_text (GTK_EDITABLE (div_x_entry), buffer);

  gimp_table_attach (table, div_x_hbox, 1, 2, trow, trow+1, 
		    0, 0, 5, 5);


  trow++;


  height_entry = entscale_int_new (table, 0, trow, "Height (pixels):", 
				   &mvals.height, 
				   1, sel_h/4, TRUE, 
				   (EntscaleIntCallbackFunc) height_width_callback,
				   &div_y_entry);

  trow++;

  div_y_label = gtk_label_new("Pieces:");

  gimp_table_attach (table, div_y_label, 0, 1, trow, trow+1, 
		    0, 0, 5, 5);


  div_y_hbox = divbox_new(&sel_h,
			  height_entry,
			  &div_y_entry);

  sprintf(buffer, "%d", (sel_h / mvals.height) );
  gtk_editable_set_text (GTK_EDITABLE (div_y_entry), buffer);

  gimp_table_attach (table, div_y_hbox, 1, 2, trow, trow+1, 
		    0, 0, 5, 5);


  /* Add Options page to notebook */

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), frame, 
			    gtk_label_new ("Options"));

  /* Set up other page */
  frame = gtk_frame_new ("At Your Own Risk");
  gimp_container_set_border_width (frame, 10);
  table = gimp_table_new (4, 2, FALSE);
  gimp_container_set_border_width (table, 10);
  gimp_container_add (frame, table);

  /* Multiple input box */
  label = gtk_label_new ("Multiple (57)");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 0, 1, GIMP_FILL, 0, 5, 0 );
  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 1, 2, 0, 1, GIMP_FILL, GIMP_FILL, 0, 0 );
  gtk_widget_set_size_request( entry, ENTRY_WIDTH, -1 );
  sprintf( buffer, "%d", mvals.multiple );
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer );
  g_signal_connect (entry, "changed",
		      G_CALLBACK (maze_entry_callback),
		      &mvals.multiple);

  /* Offset input box */
  label = gtk_label_new ("Offset (1)");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 1, 2, GIMP_FILL, 0, 5, 0 );
  entry = gtk_entry_new ();
  gimp_table_attach (table, entry, 1, 2, 1, 2, GIMP_FILL, GIMP_FILL, 0, 0 );
  gtk_widget_set_size_request( entry, ENTRY_WIDTH, -1 );
  sprintf( buffer, "%d", mvals.offset );
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer );
  g_signal_connect (entry, "changed",
		      G_CALLBACK (maze_entry_callback),
		      &mvals.offset);

  /* Seed input box */
  label = gtk_label_new ("Seed");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 2, 3, 
		    GIMP_FILL, 0, 5, 5);

  seed_hbox = gimp_hbox_new(FALSE, 2);
  gimp_table_attach (table, seed_hbox, 1, 2, 2, 3, 
		    GIMP_FILL | GIMP_EXPAND, GIMP_FILL, 0, 5);

  seed_entry = gtk_entry_new ();
  gtk_widget_set_size_request( seed_entry, ENTRY_WIDTH, -1 );
  sprintf( buffer, "%d", mvals.seed );
  gtk_editable_set_text (GTK_EDITABLE (seed_entry), buffer );
  g_signal_connect (seed_entry, "changed",
		      G_CALLBACK (maze_entry_callback),
		      &mvals.seed);
  gimp_box_pack_start (seed_hbox, seed_entry, TRUE, TRUE, 0);

  time_button = gtk_toggle_button_new_with_label ("Time");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (time_button),mvals.timeseed);
  g_signal_connect (time_button, "clicked",
		      G_CALLBACK (toggle_callback),
		      &mvals.timeseed);
  gimp_box_pack_end (seed_hbox, time_button, FALSE, FALSE, 0);

  label = gtk_label_new("Algorithm");
  gimp_misc_set_alignment (label, 0.0, 0.5);
  gimp_table_attach (table, label, 0, 1, 3, 4, 
		    GIMP_FILL, 0, 5, 5);

  alg_box=gimp_vbox_new(FALSE, 5);
  gimp_table_attach (table, alg_box, 1, 2, 3, 4, 
		    GIMP_FILL, 0, 5, 5);

  alg_button=gimp_radio_button_new (NULL,"Depth First");
  g_signal_connect (alg_button, "toggled",
		     G_CALLBACK (alg_radio_callback), GINT_TO_POINTER (DEPTH_FIRST));
  if(mvals.algorithm==DEPTH_FIRST)
       gtk_check_button_set_active (GTK_CHECK_BUTTON (alg_button), TRUE);
  gimp_container_add (alg_box,alg_button);

  alg_button=gimp_radio_button_new (alg_button, "Prim's Algorithm");
  g_signal_connect (alg_button, "toggled",
		     G_CALLBACK (alg_radio_callback), GINT_TO_POINTER (PRIMS_ALGORITHM));
  if(mvals.algorithm==PRIMS_ALGORITHM)
       gtk_check_button_set_active (GTK_CHECK_BUTTON (alg_button), TRUE);

  gimp_container_add (alg_box,alg_button);

   /* Add Advanced page to notebook */
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), frame, 
			    gtk_label_new ("Advanced"));

  gtk_window_present (GTK_WINDOW (dlg));

  gimp_main_loop_run ();

  return maze_run;
}

static GtkWidget*
divbox_new (guint *max, GtkWidget *friend, GtkWidget **div_entry)
{
     GtkWidget *div_hbox;
     GtkWidget *buttonl, *buttonr;
     static gshort less= -1, more= 1;
#if DIVBOX_LOOKS_LIKE_SPINBUTTON
     GtkWidget *buttonbox;
#endif     


     div_hbox=gimp_hbox_new(FALSE, 0);

#if DIVBOX_LOOKS_LIKE_SPINBUTTON     
     buttonl=gtk_button_new_from_icon_name("pan-down-symbolic");
     buttonr=gtk_button_new_from_icon_name("pan-up-symbolic");
#else
     buttonl=gtk_button_new_from_icon_name("pan-start-symbolic");
     buttonr=gtk_button_new_from_icon_name("pan-end-symbolic");
#endif
     
     g_object_set_data (G_OBJECT (buttonl), "direction", &less);
     g_object_set_data (G_OBJECT (buttonr), "direction", &more);

     *div_entry= gtk_entry_new();

     g_object_set_data (G_OBJECT (*div_entry), "max", max);
     g_object_set_data (G_OBJECT (*div_entry), "friend", friend);

     gtk_widget_set_size_request( *div_entry, ENTRY_WIDTH, -1 );

#if DIVBOX_LOOKS_LIKE_SPINBUTTON
     buttonbox = gimp_vbox_new(FALSE, 0);

     gimp_box_pack_start (buttonbox, buttonr, FALSE, FALSE, 0);
     gimp_box_pack_start (buttonbox, buttonl, FALSE, FALSE, 0);

     gimp_box_pack_start (div_hbox, *div_entry, FALSE, FALSE, 2);
     gimp_box_pack_start (div_hbox, buttonbox, FALSE, FALSE, 0);
#else
     gimp_box_pack_start (div_hbox, buttonl, FALSE, FALSE, 0);
     gimp_box_pack_start (div_hbox, *div_entry,   FALSE, FALSE, 2);
     gimp_box_pack_start (div_hbox, buttonr, FALSE, FALSE, 0);
#endif     

     g_signal_connect (buttonl, "clicked",
			G_CALLBACK (div_button_callback), 
			*div_entry);

     g_signal_connect (buttonr, "clicked",
			G_CALLBACK (div_button_callback), 
			*div_entry);     

     g_signal_connect (*div_entry, "changed",
			G_CALLBACK (div_entry_callback),
			friend);

     return div_hbox;
}

static void
div_button_callback (GtkWidget *button, GtkWidget *entry)
{
     guint max, divs, even;
     const gchar *text;
     gchar *text2;
     gshort direction;

     direction = *((gshort*) g_object_get_data (G_OBJECT (button), "direction"));
     max = *((guint*) g_object_get_data (G_OBJECT (entry), "max"));

     /* Tileable mazes shall have only an even number of divisions.
        Other mazes have odd. */
     /* Logic games!

     If BIT1 is and "even" is then add:
	 FALSE        TRUE        0
         TRUE         TRUE        1
	 FALSE        FALSE       1
	 TRUE         FALSE       0

	 That's where the +((foo & 1) == even) stuff comes from. */

     /* Sanity check: */
     if (mvals.tile && (max & 1)) {
	  maze_msg("Selection size is not even.  \nTileable maze won't work perfectly.");
	  return;
     }

     even = mvals.tile ? 1 : 0;

     text = gtk_editable_get_text (GTK_EDITABLE (entry));

     divs=atoi(text);
     if (divs <= 3) {
	  divs= max - ((max & 1) == even);	  
     } else if (divs > max) {
	  divs= 5 + even;
     }
     
     /* Makes sure we're appropriately even or odd, adjusting in the
        proper direction. */
     divs += direction * ((divs & 1) == even);
	  
     if (mvals.tile) {	  
	  if (direction > 0) {
	       do {
		    divs += 2;
		    if (divs > max)
			 divs = 4;
	       } while (max % divs);
	  } else { /* direction < 0 */
	       do {
		    divs -= 2;
		    if (divs < 4)
			 divs = max - (max & 1);
	       } while (max % divs);
	  } /* endif direction < 0 */
     } else { /* If not tiling, having a non-zero remainder doesn't bother us much. */
	  if (direction > 0) {
	       do {
		    divs += 2;
	       } while ((max % divs > max / divs * BORDER_TOLERANCE ) && divs < max);
	  } else { /* direction < 0 */
	       do {
		    divs -= 2;
	       } while ((max % divs > max / divs * BORDER_TOLERANCE) && divs > 5);
	  } /* endif direction < 0 */
     } /* endif not tiling */

     if (divs <= 3) {
	  divs= max - ((max & 1) == even);	  
     } else if (divs > max) {
	  divs= 5 - even;
     } /* endif divs > max */

     text2 = g_new(gchar, 16);
     sprintf (text2,"%d",divs);

     gtk_editable_set_text (GTK_EDITABLE (entry),text2);

     return;
}

static void
div_entry_callback (GtkWidget *entry, GtkWidget *friend)
{
     guint divs, width, max;
     gchar *buffer;
     EntscaleIntData *userdata;
     EntscaleIntCallbackFunc friend_callback;

     divs = atoi(gtk_editable_get_text (GTK_EDITABLE (entry)));
     if (divs < 4) /* If this is under 4 (e.g. 0), something's weird. */
	  return;  /* But it'll probably be ok, so just return and ignore. */

     max = *((guint*) g_object_get_data (G_OBJECT (entry), "max"));     
     buffer = g_new(gchar, 16);

     /* I say "width" here, but it could be height.*/

     width = max/divs;
     sprintf (buffer,"%d", width );

     /* No tagbacks from our friend... */
     userdata = g_object_get_data (G_OBJECT (friend), "user_data");
     friend_callback = userdata->callback;
     userdata->callback = NULL;

     gtk_editable_set_text (GTK_EDITABLE (friend), buffer);

     userdata->callback = friend_callback;
}

static void
height_width_callback (gint width, GtkWidget **div_entry)
{
     guint divs, max;
     gpointer data;
     gchar *buffer;

     max = *((guint*) g_object_get_data (G_OBJECT (*div_entry), "max"));
     divs = max / width;

     buffer = g_new(gchar, 16);
     sprintf (buffer,"%d", divs );

     data = g_object_get_data (G_OBJECT (*div_entry), "friend");
     g_signal_handlers_block_matched (*div_entry, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data );
     
     gtk_editable_set_text (GTK_EDITABLE (*div_entry), buffer);

     g_signal_handlers_unblock_matched (*div_entry, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data );
     
}


static void 
maze_close_callback (GtkWidget *widget, 
		     gpointer data)
{
    gimp_main_loop_quit ();
}

static void
maze_help (GtkWidget *widget, gpointer foo)
{
     char *proc_blurb, *proc_help, *proc_author, *proc_copyright, *proc_date;
     int proc_type, nparams, nreturn_vals;
     GParamDef *params, *return_vals;
     gint baz;

     if (gimp_query_procedure("extension_web_browser",
                              &proc_blurb, &proc_help, 
			      &proc_author, &proc_copyright, &proc_date,
			      &proc_type, &nparams, &nreturn_vals,
			      &params, &return_vals)) {
          maze_msg("Opening " MAZE_URL);
          gimp_run_procedure("extension_web_browser", &baz,
                             PARAM_INT32, RUN_NONINTERACTIVE,
                             PARAM_STRING, MAZE_URL,
                             PARAM_INT32, HELP_OPENS_NEW_WINDOW,
                             PARAM_END);
     } else {
          maze_msg("See " MAZE_URL);
     }                                            
}

static void
maze_msg (gchar *msg)
{
     gtk_label_set_text (GTK_LABEL (msg_label), msg);
}

static void
maze_ok_callback (GtkWidget *widget,
		  gpointer data)
{
    maze_run = TRUE;
    gtk_window_destroy (GTK_WINDOW (data));
}

static void 
maze_entry_callback (GtkWidget *widget,
		       gpointer data)
{
    gint *text_val;

    text_val = (gint *) data;

    *text_val = atoi (gtk_editable_get_text (GTK_EDITABLE (widget)));
}

static void 
toggle_callback (GtkWidget *widget, gboolean *data)
{
    /* tilecheck is a check button, the "Time" button a toggle button */
    if (GTK_IS_CHECK_BUTTON (widget))
      *data = gtk_check_button_get_active (GTK_CHECK_BUTTON (widget));
    else
      *data = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
}

static void
alg_radio_callback (GtkWidget *widget, gpointer data)
{
     if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)))
       mvals.algorithm=(MazeAlgoType) GPOINTER_TO_INT (data);
}

/* ==================================================================== */
/* As found in pixelize.c, 
 * hacked to return a pointer to the entry widget. */

/*
  Entry and Scale pair 1.03

  TODO:
  - Do the proper thing when the user changes value in entry,
  so that callback should not be called when value is actually not changed.
  - Update delay
 */

/*
 *  entscale: create new entscale with label. (int)
 *  1 row and 2 cols of table are needed.
 *  Input:
 *    x, y:       starting row and col in table
 *    caption:    label string
 *    intvar:     pointer to variable
 *    min, max:   the boundary of scale
 *    constraint: (bool) true iff the value of *intvar should be constraint
 *                by min and max
 *    callback:	  called when the value is actually changed
 *    call_data:  data for callback func
 */
static GtkWidget*
entscale_int_new ( GtkWidget *table, gint x, gint y,
		   gchar *caption, gint *intvar,
		   gint min, gint max, gboolean constraint,
		   EntscaleIntCallbackFunc callback,
		   gpointer call_data)
{
  EntscaleIntData *userdata;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *entry;
  GtkWidget *scale;
  GtkAdjustment *adjustment;
  gchar    buffer[256];
  gint	    constraint_val;

  userdata = g_new ( EntscaleIntData, 1 );

  label = gtk_label_new (caption);
  gimp_misc_set_alignment (label, 0.0, 0.5);

  /*
    If the first arg of gtk_adjustment_new() isn't between min and
    max, it is automatically corrected by gtk later with
    "value_changed" signal. I don't like this, since I want to leave
    *intvar untouched when `constraint' is false.
    The lines below might look oppositely, but this is OK.
   */
  userdata->constraint = constraint;
  if( constraint )
    constraint_val = *intvar;
  else
    constraint_val = ( *intvar < min ? min : *intvar > max ? max : *intvar );

  userdata->adjustment = adjustment = 
    gtk_adjustment_new ( constraint_val, min, max, 1.0, 1.0, 0.0);
  scale = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, adjustment);
  gtk_widget_set_size_request (scale, ENTSCALE_INT_SCALE_WIDTH, -1);
  gtk_scale_set_draw_value (GTK_SCALE (scale), FALSE);

  userdata->entry = entry = gtk_entry_new ();
  gtk_widget_set_size_request (entry, ENTSCALE_INT_ENTRY_WIDTH, -1);
  sprintf( buffer, "%d", *intvar );
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer );

  userdata->callback = callback;
  userdata->call_data = call_data;

  /* userdata is done */
  g_object_set_data (G_OBJECT (adjustment), "user_data", userdata);
  g_object_set_data (G_OBJECT (entry), "user_data", userdata);

  /* now ready for signals */
  g_signal_connect (entry, "changed",
		      G_CALLBACK (entscale_int_entry_update),
		      intvar);
  g_signal_connect (adjustment, "value-changed",
		      G_CALLBACK (entscale_int_scale_update),
		      intvar);
  g_signal_connect (entry, "destroy",
		      G_CALLBACK (entscale_int_destroy_callback),
		      userdata );

  /* start packing */
  hbox = gimp_hbox_new (FALSE, 5);
  gimp_box_pack_start (hbox, scale, TRUE, TRUE, 0);
  gimp_box_pack_start (hbox, entry, FALSE, TRUE, 0);

  gimp_table_attach (table, label, x, x+1, y, y+1,
		    GIMP_FILL, GIMP_FILL, 0, 0);
  gimp_table_attach (table, hbox, x+1, x+2, y, y+1,
		    GIMP_EXPAND | GIMP_FILL, GIMP_FILL, 0, 0);


  return entry;
}  


/* when destroyed, userdata is destroyed too */
static void
entscale_int_destroy_callback (GtkWidget *widget,
			       gpointer data)
{
  EntscaleIntData *userdata;

  userdata = data;
  g_free ( userdata );
}

static void
entscale_int_scale_update (GtkAdjustment *adjustment,
			   gpointer      data)
{
  EntscaleIntData *userdata;
  GtkWidget	*entry;
  gchar		buffer[256];
  gint		*intvar = data;
  gint		new_val;

  userdata = g_object_get_data (G_OBJECT (adjustment), "user_data");

  new_val = (gint) gtk_adjustment_get_value (adjustment);

  *intvar = new_val;

  entry = userdata->entry;
  sprintf (buffer, "%d", (int) new_val );
  
  /* avoid infinite loop (scale, entry, scale, entry ...) */
  g_signal_handlers_block_matched (entry, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data );
  gtk_editable_set_text (GTK_EDITABLE (entry), buffer);
  g_signal_handlers_unblock_matched (entry, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data );

  if (userdata->callback)
    (*userdata->callback) (*intvar, userdata->call_data);
}

static void
entscale_int_entry_update (GtkWidget *widget,
			   gpointer   data)
{
  EntscaleIntData *userdata;
  GtkAdjustment	*adjustment;
  int		new_val, constraint_val;
  int		*intvar = data;

  userdata = g_object_get_data (G_OBJECT (widget), "user_data");
  adjustment = GTK_ADJUSTMENT( userdata->adjustment );

  new_val = atoi (gtk_editable_get_text (GTK_EDITABLE (widget)));
  constraint_val = new_val;
  if ( constraint_val < gtk_adjustment_get_lower (adjustment) )
    constraint_val = gtk_adjustment_get_lower (adjustment);
  if ( constraint_val > gtk_adjustment_get_upper (adjustment) )
    constraint_val = gtk_adjustment_get_upper (adjustment);

  if ( userdata->constraint )
    *intvar = constraint_val;
  else
    *intvar = new_val;

  g_signal_handlers_block_matched (adjustment, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data );
  gtk_adjustment_set_value (adjustment, constraint_val);
  g_signal_handlers_unblock_matched (adjustment, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data );
  
  if (userdata->callback)
    (*userdata->callback) (*intvar, userdata->call_data);
}
