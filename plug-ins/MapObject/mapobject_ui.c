/**************************************************************/
/* Dialog creation and updaters, callbacks and event-handlers */
/**************************************************************/

#include "mapobject_ui.h"
#include "mapobject_pixmaps.h"

extern MapObjectValues mapvals;

GckApplicationWindow *appwin            = NULL;
GtkNotebook          *options_note_book = NULL;

/* GTK 4 has no GtkTooltips object to switch off, so the widgets */
/* with tooltips are kept here and "Enable tooltips" turns each  */
/* of them on or off.                                            */

GSList *tooltip_widgets = NULL;

GtkWidget *previewarea = NULL,*pointlightwid,*dirlightwid;
GtkWidget *xentry,*yentry,*zentry;

GckRGB old_light_color;



guint left_button_pressed = FALSE, light_hit = FALSE;

GckScaleValues angle_scale_vals =  { 180, 0.0, -180.0, 180.0, 0.1, 1.0, 1.0, TRUE };
GckScaleValues sample_scale_vals = { 128, 3.0,    1.0,   6.0, 1.0, 1.0, 1.0, TRUE };

gchar *light_labels[] =
  {
    "Point light",
    "Directional light",
    "No light",
     NULL
  };

gchar *map_labels[] =
  {
    "Plane",
    "Sphere",
     NULL
  };

/**********/
/* Protos */
/**********/

void create_main_dialog   (void);
void create_main_notebook (GtkWidget *);

/**************************/
/* Callbacks and updaters */
/**************************/

void preview_button_press   (GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer data);
void preview_button_motion  (GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data);
void preview_button_release (GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data);

void update_slider            (void);
void update_angle_sliders     (void);
void update_light_pos_entries (void);

void xyzval_update      (GtkWidget *widget, GtkEntry *entry);
void entry_update       (GtkWidget *widget, GtkEntry *entry);
void angle_update       (GtkWidget *widget, GtkAdjustment *adjustment);
void scale_update       (GtkWidget *widget, GtkAdjustment *adjustment);
void toggle_update      (GtkWidget *widget, GtkCheckButton *button);
void togglegrid_update  (GtkWidget *widget, GtkCheckButton *button);
void toggletile_update  (GtkWidget *widget, GtkCheckButton *button);
void toggleanti_update  (GtkWidget *widget, GtkCheckButton *button);
void toggletips_update  (GtkWidget *widget, GtkCheckButton *button);
void toggletrans_update (GtkWidget *widget, GtkCheckButton *button);

void lightmenu_callback    (GtkWidget *widget, gpointer client_data);
void mapmenu_callback      (GtkWidget *widget, gpointer client_data);
void preview_callback      (GtkWidget *widget, gpointer client_data);
void zoomout_callback      (GtkWidget *widget, gpointer client_data);
void zoomin_callback       (GtkWidget *widget, gpointer client_data);
void apply_callback        (GtkWidget *widget, gpointer client_data);
void exit_callback         (GtkWidget *widget, gpointer client_data);
gboolean close_callback    (GtkWidget *widget, gpointer client_data);
void light_color_callback  (GtkWidget *widget, gpointer client_data);
void color_chosen_callback (const guchar *rgb, gpointer client_data);

GtkWidget *create_options_page     (void);
GtkWidget *create_light_page       (void);
GtkWidget *create_material_page    (void);
GtkWidget *create_orientation_page (void);

/******************/
/* Implementation */
/******************/

/*******************************************************/
/* Give widget a tooltip that "Enable tooltips" covers */
/*******************************************************/

static void set_tooltip(GtkWidget *widget, const gchar *tip)
{
  gtk_widget_set_tooltip_text(widget,tip);
  gtk_widget_set_has_tooltip(widget,mapvals.tooltips_enabled);
  tooltip_widgets=g_slist_prepend(tooltip_widgets,widget);
}

/***************************************************/
/* Show the frame for the current light type only  */
/***************************************************/

static void update_light_widgets(void)
{
  gtk_widget_set_visible(pointlightwid,mapvals.lightsource.type==POINT_LIGHT);
  gtk_widget_set_visible(dirlightwid,mapvals.lightsource.type==DIRECTIONAL_LIGHT);
}

/**********************************************************/
/* Update entry fields that affect the preview parameters */
/**********************************************************/

void xyzval_update(GtkWidget *widget, GtkEntry *entry)
{
  gdouble *valueptr;
  gdouble value;

  valueptr=(gdouble *)g_object_get_data(G_OBJECT(widget),"ValuePtr");
  value = atof(gtk_editable_get_text(GTK_EDITABLE(widget)));

  *valueptr=value;

  if (mapvals.showgrid==TRUE)
    draw_preview_wireframe();
}

/*********************/
/* Std. entry update */
/*********************/

void entry_update(GtkWidget *widget, GtkEntry *entry)
{
  gdouble *valueptr;
  gdouble value;

  valueptr=(gdouble *)g_object_get_data(G_OBJECT(widget),"ValuePtr");
  value = atof(gtk_editable_get_text(GTK_EDITABLE(widget)));

  *valueptr=value;
}

/***************************************************/
/* Update angle sliders (redraw grid if necessary) */
/***************************************************/

void angle_update(GtkWidget *widget, GtkAdjustment *adjustment)
{
  gdouble *valueptr;

  valueptr=(gdouble *)g_object_get_data(G_OBJECT(widget),"ValuePtr");

  *valueptr=gtk_range_get_value(GTK_RANGE(widget));

  if (mapvals.showgrid==TRUE)
    draw_preview_wireframe();
}

void update_light_pos_entries(void)
{
  gchar entrytext[64];

  g_snprintf(entrytext,sizeof(entrytext),"%f",mapvals.lightsource.position.x);
  gtk_editable_set_text(GTK_EDITABLE(xentry),entrytext);
  g_snprintf(entrytext,sizeof(entrytext),"%f",mapvals.lightsource.position.y);
  gtk_editable_set_text(GTK_EDITABLE(yentry),entrytext);
  g_snprintf(entrytext,sizeof(entrytext),"%f",mapvals.lightsource.position.z);
  gtk_editable_set_text(GTK_EDITABLE(zentry),entrytext);
}

void update_slider(void)
{
}

void update_angle_sliders(void)
{
}

/*********************/
/* Std. scale update */
/*********************/

void scale_update(GtkWidget *widget,GtkAdjustment *adjustment)
{
  gdouble *valueptr;

  valueptr=(gdouble *)g_object_get_data(G_OBJECT(widget),"ValuePtr");

  *valueptr=gtk_range_get_value(GTK_RANGE(widget));
}

/**********************/
/* Std. toggle update */
/**********************/

void toggle_update(GtkWidget *widget, GtkCheckButton *button)
{
  gint *value;

  value=(gint *)g_object_get_data(G_OBJECT(widget),"ValuePtr");
  *value=!(*value);
}

/***************************/
/* Show grid toggle update */
/***************************/

void togglegrid_update(GtkWidget *widget, GtkCheckButton *button)
{
  gint *value;

  value=(gint *)g_object_get_data(G_OBJECT(widget),"ValuePtr");
  *value=!(*value);

  if (mapvals.showgrid==TRUE && linetab[0].x1==-1)
    draw_preview_wireframe();
  else if (mapvals.showgrid==FALSE && linetab[0].x1!=-1)
    {
      linetab[0].x1=-1;
      clear_wireframe();
    }
}

/****************************/
/* Tile image toggle update */
/****************************/

void toggletile_update(GtkWidget *widget, GtkCheckButton *button)
{
  gint *value;

  value=(gint *)g_object_get_data(G_OBJECT(widget),"ValuePtr");
  *value=!(*value);

  draw_preview_image(TRUE);
  linetab[0].x1=-1;
}

/******************************/
/* Antialiasing toggle update */
/******************************/

void toggleanti_update(GtkWidget *widget, GtkCheckButton *button)
{
  gint *value;

  value=(gint *)g_object_get_data(G_OBJECT(widget),"ValuePtr");
  *value=!(*value);
}

/**************************/
/* Tooltips toggle update */
/**************************/

void toggletips_update(GtkWidget *widget, GtkCheckButton *button)
{
  gint *value;
  GSList *list;

  value=(gint *)g_object_get_data(G_OBJECT(widget),"ValuePtr");
  *value=!(*value);

  for (list=tooltip_widgets;list!=NULL;list=list->next)
    gtk_widget_set_has_tooltip(GTK_WIDGET(list->data),mapvals.tooltips_enabled);
}

/****************************************/
/* Transparent background toggle update */
/****************************************/

void toggletrans_update(GtkWidget *widget, GtkCheckButton *button)
{
  gint *value;

  value=(gint *)g_object_get_data(G_OBJECT(widget),"ValuePtr");
  *value=!(*value);

  draw_preview_image(TRUE);
  linetab[0].x1=-1;
}

/*****************************************/
/* Main window light type menu callback. */
/*****************************************/

void lightmenu_callback(GtkWidget *widget, gpointer client_data)
{
  mapvals.lightsource.type=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"_GckOptionMenuItemID"));

  update_light_widgets();
}

/***************************************/
/* Main window map type menu callback. */
/***************************************/

void mapmenu_callback(GtkWidget *widget, gpointer client_data)
{
  mapvals.maptype=(MapType)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"_GckOptionMenuItemID"));

  draw_preview_image(TRUE);

  if (mapvals.showgrid==TRUE && linetab[0].x1==-1)
    draw_preview_wireframe();
  else if (mapvals.showgrid==FALSE && linetab[0].x1!=-1)
    {
      linetab[0].x1=-1;
      clear_wireframe();
    }
}

/******************************************/
/* Main window "Preview!" button callback */
/******************************************/

void preview_callback(GtkWidget *widget, gpointer client_data)
{
  draw_preview_image(TRUE);
  linetab[0].x1=-1;
}

/*********************************************/
/* Main window "-" (zoom in) button callback */
/*********************************************/

void zoomout_callback(GtkWidget *widget, gpointer client_data)
{
  if (mapvals.preview_zoom_factor<2)
    {
      mapvals.preview_zoom_factor++;
      if (linetab[0].x1!=-1)
        clear_wireframe();
      draw_preview_image(TRUE);
    }
}

/*********************************************/
/* Main window "+" (zoom out) button callback */
/*********************************************/

void zoomin_callback(GtkWidget *widget, gpointer client_data)
{
  if (mapvals.preview_zoom_factor>0)
    {
      mapvals.preview_zoom_factor--;
      if (linetab[0].x1!=-1)
        clear_wireframe();
      draw_preview_image(TRUE);
    }
}

/*******************************************************/
/* Free the preview and close the window, leaving the  */
/* main loop running until the caller quits it.        */
/*******************************************************/

static void close_main_dialog(void)
{
  if (preview_rgb_data!=NULL)
    {
      free(preview_rgb_data);
      preview_rgb_data=NULL;
    }

  if (image!=NULL)
    {
      cairo_surface_destroy(image);
      image=NULL;
    }

  g_slist_free(tooltip_widgets);
  tooltip_widgets=NULL;

  previewarea=NULL;
  gck_application_window_destroy(appwin);
  appwin=NULL;
}

/**********************************************/
/* Main window "Apply" button callback.       */
/* Render to GIMP image, close down and exit. */
/**********************************************/

void apply_callback(GtkWidget *widget, gpointer client_data)
{
  close_main_dialog();

  compute_image();

  gimp_main_loop_quit();
}

/*************************************************************/
/* Main window "Cancel" button callback. Shut down and exit. */
/*************************************************************/

void exit_callback(GtkWidget *widget, gpointer client_data)
{
  close_main_dialog();

  gimp_main_loop_quit();
}

/***********************************************/
/* The window manager's close button = Cancel. */
/***********************************************/

gboolean close_callback(GtkWidget *widget, gpointer client_data)
{
  exit_callback(widget,client_data);

  return TRUE;
}

/***************************************************/
/* Color dialog result: the new light source color */
/***************************************************/

void color_chosen_callback(const guchar *rgb, gpointer client_data)
{
  mapvals.lightsource.color.r=rgb[0]/255.0;
  mapvals.lightsource.color.g=rgb[1]/255.0;
  mapvals.lightsource.color.b=rgb[2]/255.0;
}

void light_color_callback(GtkWidget *widget, gpointer client_data)
{
  guchar rgb[3];

  if (mapvals.lightsource.type!=NO_LIGHT)
    {
      rgb[0]=(guchar)(mapvals.lightsource.color.r*255.0+0.5);
      rgb[1]=(guchar)(mapvals.lightsource.color.g*255.0+0.5);
      rgb[2]=(guchar)(mapvals.lightsource.color.b*255.0+0.5);

      gimp_color_dialog_run(GTK_WINDOW(appwin->widget),"Select lightsource color",
        rgb,color_chosen_callback,NULL);
    }
}

/*********************************/
/* Preview area mouse handlers   */
/*********************************/

void preview_button_press(GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer data)
{
  HVect pos;

  light_hit=check_light_hit((gint)x,(gint)y);
  if (light_hit==FALSE)
    {
      pos.x=-(2.0*x/(gdouble)PREVIEW_WIDTH-1.0);
      pos.y=2.0*y/(gdouble)PREVIEW_HEIGHT-1.0;
      /*ArcBall_Mouse(pos);
      ArcBall_BeginDrag(); */
      (void)pos;
    }
  left_button_pressed=TRUE;
}

void preview_button_release(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data)
{
  gdouble x,y;
  HVect pos;

  gtk_gesture_drag_get_start_point(gesture,&x,&y);
  x+=dx;
  y+=dy;

  if (light_hit==TRUE)
    draw_preview_image(TRUE);
  else
    {
      pos.x=-(2.0*x/(gdouble)PREVIEW_WIDTH-1.0);
      pos.y=2.0*y/(gdouble)PREVIEW_HEIGHT-1.0;
      /*ArcBall_Mouse(pos);
      ArcBall_EndDrag(); */
      (void)pos;
    }
  left_button_pressed=FALSE;
}

void preview_button_motion(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data)
{
  gdouble x,y;
  HVect pos;

  gtk_gesture_drag_get_start_point(gesture,&x,&y);
  x+=dx;
  y+=dy;

  if (left_button_pressed==TRUE)
    {
      if (light_hit==TRUE)
        {
          update_light((gint)x,(gint)y);
          update_light_pos_entries();
        }
      else
        {
          pos.x=-(2.0*x/(gdouble)PREVIEW_WIDTH-1.0);
          pos.y=2.0*y/(gdouble)PREVIEW_HEIGHT-1.0;
          (void)pos;
/*          ArcBall_Mouse(pos);
          ArcBall_Update();
          ArcBall_Values(&a,&b,&c);
          Alpha+=RadToDeg(-a);
          Beta+RadToDeg(-b);
          Gamma+=RadToDeg(-c);
          if (Alpha>180) Alpha-=360;
          if (Alpha<-180) Alpha+=360;
          if (Beta>180) Beta-=360;
          if (Beta<-180) Beta+=360;
          if (Gamma>180) Gamma-=360;
          if (Gamma<-180) Gamma+=360;
      	  UpdateAngleSliders(); */
        }
    }
}

/*******************************/
/* Create general options page */
/*******************************/

GtkWidget *create_options_page(void)
{
  GtkWidget *page,*frame,*vbox,*hbox;
  GtkWidget *toggletile,*toggleanti;
  GtkWidget *toggletrans,*toggleimage,*toggletips;
  GtkWidget *widget1,*widget2;

  page=gck_vbox_new(NULL,FALSE,FALSE,FALSE,0,0,0);

  frame=gck_frame_new("General options",page,GCK_SHADOW_ETCHED_IN,FALSE,FALSE,0,2);
  vbox=gck_vbox_new(frame,FALSE,FALSE,FALSE,0,2,2);

  widget1=gck_option_menu_new("Map to:",vbox,TRUE,TRUE,0,map_labels,
    G_CALLBACK(mapmenu_callback), NULL);

  gck_option_menu_set_history(widget1,mapvals.maptype);
  set_tooltip(widget1,"Type of object to map to");

  vbox=gck_vbox_new(vbox,FALSE,FALSE,FALSE,0,0,0);
  toggletrans=gck_checkbutton_new("Transparent background",vbox,mapvals.transparent_background,
    G_CALLBACK(toggletrans_update));
  toggletile=gck_checkbutton_new("Tile source image",vbox,mapvals.tiled,
    G_CALLBACK(toggletile_update));
  toggleimage=gck_checkbutton_new("Create new image",vbox,mapvals.create_new_image,
    G_CALLBACK(toggle_update));
  toggletips=gck_checkbutton_new("Enable tooltips",vbox,mapvals.tooltips_enabled,
    G_CALLBACK(toggletips_update));

  set_tooltip(toggletrans,"Make image transparent outside object");
  set_tooltip(toggletile,"Tile source image: useful for infinite planes");
  set_tooltip(toggleimage,"Create a new image when applying filter");
  set_tooltip(toggletips,"Enable/disable tooltip messages");

  g_object_set_data(G_OBJECT(toggletrans),"ValuePtr",(gpointer)&mapvals.transparent_background);
  g_object_set_data(G_OBJECT(toggletile),"ValuePtr",(gpointer)&mapvals.tiled);
  g_object_set_data(G_OBJECT(toggleimage),"ValuePtr",(gpointer)&mapvals.create_new_image);
  g_object_set_data(G_OBJECT(toggletips),"ValuePtr", (gpointer)&mapvals.tooltips_enabled);

  frame=gck_frame_new("Antialiasing options",page,GCK_SHADOW_ETCHED_IN,FALSE,FALSE,0,2);
  vbox=gck_vbox_new(frame,FALSE,FALSE,FALSE,0,2,2);

  toggleanti=gck_checkbutton_new("Enable antialiasing",vbox,mapvals.antialiasing,
    G_CALLBACK(toggleanti_update));
  g_object_set_data(G_OBJECT(toggleanti),"ValuePtr",(gpointer)&mapvals.antialiasing);
  set_tooltip(toggleanti,"Enable/disable jagged edges removal (antialiasing)");

  hbox=gck_hbox_new(vbox,FALSE,TRUE,TRUE,0,0,0);

  vbox=gck_vbox_new(hbox,TRUE,FALSE,TRUE,0,0,0);

  frame=gck_frame_new(NULL,vbox,GCK_SHADOW_NONE,TRUE,TRUE,0,0);
  gck_label_aligned_new("Depth:",frame,GCK_ALIGN_RIGHT,GCK_ALIGN_BOTTOM);

  frame=gck_frame_new(NULL,vbox,GCK_SHADOW_NONE,TRUE,TRUE,0,0);
  gck_label_aligned_new("Treshold:",frame,GCK_ALIGN_RIGHT,GCK_ALIGN_CENTERED);

  vbox=gck_vbox_new(hbox,TRUE,FALSE,FALSE,5,0,0);

  widget1=gck_hscale_new(NULL,vbox,&sample_scale_vals,G_CALLBACK(scale_update));
  widget2=gck_entryfield_new(NULL,vbox,mapvals.pixeltreshold,G_CALLBACK(entry_update));
  g_object_set_data(G_OBJECT(widget1),"ValuePtr",(gpointer)&mapvals.maxdepth);
  g_object_set_data(G_OBJECT(widget2),"ValuePtr",(gpointer)&mapvals.pixeltreshold);

  set_tooltip(widget1,"Antialiasing quality. Higher is better, but slower");
  set_tooltip(widget2,"Stop when pixel differences are smaller than this value");

  return page;
}

/******************************/
/* Create light settings page */
/******************************/

GtkWidget *create_light_page(void)
{
  GtkWidget *page,*frame,*vbox;
  GtkWidget *widget1,*widget2,*widget3;

  page=gimp_vbox_new(FALSE,0);

  frame=gck_frame_new("Light settings",page,GCK_SHADOW_ETCHED_IN,FALSE,FALSE,0,5);
  vbox=gck_vbox_new(frame,FALSE,TRUE,TRUE,5,0,5);

  widget1=gck_option_menu_new("Lightsource type:",vbox,TRUE,TRUE,0,
    light_labels,G_CALLBACK(lightmenu_callback), NULL);
  gck_option_menu_set_history(widget1,mapvals.lightsource.type);

  widget2=gck_pushbutton_new("Lightsource color",vbox,TRUE,FALSE,0,
    G_CALLBACK(light_color_callback));

  set_tooltip(widget1,"Type of light source to apply");
  set_tooltip(widget2,"Set light source color (white is default)");

  pointlightwid=gck_frame_new("Position",page,GCK_SHADOW_ETCHED_IN,FALSE,FALSE,0,5);
  vbox=gck_vbox_new(pointlightwid,FALSE,FALSE,FALSE,5,0,5);

  xentry=gck_entryfield_new("X:",vbox,mapvals.lightsource.position.x,G_CALLBACK(entry_update));
  yentry=gck_entryfield_new("Y:",vbox,mapvals.lightsource.position.y,G_CALLBACK(entry_update));
  zentry=gck_entryfield_new("Z:",vbox,mapvals.lightsource.position.z,G_CALLBACK(entry_update));

  g_object_set_data(G_OBJECT(xentry),"ValuePtr",(gpointer)&mapvals.lightsource.position.x);
  g_object_set_data(G_OBJECT(yentry),"ValuePtr",(gpointer)&mapvals.lightsource.position.y);
  g_object_set_data(G_OBJECT(zentry),"ValuePtr",(gpointer)&mapvals.lightsource.position.z);

  set_tooltip(xentry,"Light source X position in XYZ space");
  set_tooltip(yentry,"Light source Y position in XYZ space");
  set_tooltip(zentry,"Light source Z position in XYZ space");

  dirlightwid=gck_frame_new("Direction vector",page,GCK_SHADOW_ETCHED_IN,FALSE,FALSE,0,5);
  vbox=gck_vbox_new(dirlightwid,FALSE,FALSE,FALSE,5,0,5);

  widget1=gck_entryfield_new("X:",vbox,mapvals.lightsource.direction.x,G_CALLBACK(entry_update));
  widget2=gck_entryfield_new("Y:",vbox,mapvals.lightsource.direction.y,G_CALLBACK(entry_update));
  widget3=gck_entryfield_new("Z:",vbox,mapvals.lightsource.direction.z,G_CALLBACK(entry_update));

  set_tooltip(widget1,"Light source X direction in XYZ space");
  set_tooltip(widget2,"Light source Y direction in XYZ space");
  set_tooltip(widget3,"Light source Z direction in XYZ space");

  g_object_set_data(G_OBJECT(widget1),"ValuePtr",(gpointer)&mapvals.lightsource.direction.x);
  g_object_set_data(G_OBJECT(widget2),"ValuePtr",(gpointer)&mapvals.lightsource.direction.y);
  g_object_set_data(G_OBJECT(widget3),"ValuePtr",(gpointer)&mapvals.lightsource.direction.z);

  update_light_widgets();

  return page;
}

/*********************************************/
/* Put an XPM picture into a material table  */
/*********************************************/

static void attach_pixmap(GtkWidget *table, char **xpm_data,
                          gint left, gint top)
{
  GtkWidget *pixmap;

  pixmap=gck_pixmap_new(xpm_data,NULL);
  gimp_table_attach(table,pixmap,left,left+1,top,top+1,
    GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL,0,0);
}

/*********************************/
/* Create material settings page */
/*********************************/

GtkWidget *create_material_page(void)
{
  GtkWidget *page,*frame,*table;
  GtkWidget *label1,*label2,*label3;
  GtkWidget *widget1,*widget2,*widget3;

  page=gck_vbox_new(NULL,FALSE,FALSE,FALSE,0,0,0);

  frame=gck_frame_new("Intensity levels",page,GCK_SHADOW_ETCHED_IN,FALSE,FALSE,0,5);

  table=gimp_table_new(2,4,FALSE);
  gtk_frame_set_child(GTK_FRAME(frame),table);

  label1=gck_label_aligned_new("Ambient:",NULL,GCK_ALIGN_RIGHT,GCK_ALIGN_CENTERED);
  label2=gck_label_aligned_new("Diffuse:",NULL,GCK_ALIGN_RIGHT,GCK_ALIGN_CENTERED);

  gimp_table_attach(table,label1,0,1,0,1, 0,0,0,0);
  gimp_table_attach(table,label2,0,1,1,2, 0,0,0,0);

  widget1=gck_entryfield_new(NULL,NULL,mapvals.material.ambient_int,G_CALLBACK(entry_update));
  widget2=gck_entryfield_new(NULL,NULL,mapvals.material.diffuse_int,G_CALLBACK(entry_update));

  gimp_table_attach(table,widget1,2,3,0,1, GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL, 0,0);
  gimp_table_attach(table,widget2,2,3,1,2, GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL, 0,0);

  attach_pixmap(table,amb1_xpm,1,0);
  attach_pixmap(table,amb2_xpm,3,0);
  attach_pixmap(table,diffint1_xpm,1,1);
  attach_pixmap(table,diffint2_xpm,3,1);

  set_tooltip(widget1,"Amount of original color to show where no direct light falls");
  set_tooltip(widget2,"Intensity of original color when lit by a light source");

  g_object_set_data(G_OBJECT(widget1),"ValuePtr",(gpointer)&mapvals.material.ambient_int);
  g_object_set_data(G_OBJECT(widget2),"ValuePtr",(gpointer)&mapvals.material.diffuse_int);

  frame=gck_frame_new("Reflectivity",page,GCK_SHADOW_ETCHED_IN,FALSE,FALSE,0,5);

  table=gimp_table_new(3,4,FALSE);
  gtk_frame_set_child(GTK_FRAME(frame),table);

  label1=gck_label_aligned_new("Diffuse:",NULL,GCK_ALIGN_RIGHT,GCK_ALIGN_CENTERED);
  label2=gck_label_aligned_new("Specular:",NULL,GCK_ALIGN_RIGHT,GCK_ALIGN_CENTERED);
  label3=gck_label_aligned_new("Hightlight:",NULL,GCK_ALIGN_RIGHT,GCK_ALIGN_CENTERED);

  gimp_table_attach(table,label1,0,1,0,1, 0,0,0,0);
  gimp_table_attach(table,label2,0,1,1,2, 0,0,0,0);
  gimp_table_attach(table,label3,0,1,2,3, 0,0,0,0);

  widget1=gck_entryfield_new(NULL,NULL,mapvals.material.diffuse_ref,G_CALLBACK(entry_update));
  widget2=gck_entryfield_new(NULL,NULL,mapvals.material.specular_ref,G_CALLBACK(entry_update));
  widget3=gck_entryfield_new(NULL,NULL,mapvals.material.highlight,G_CALLBACK(entry_update));

  gimp_table_attach(table,widget1,2,3,0,1, GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL, 0,0);
  gimp_table_attach(table,widget2,2,3,1,2, GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL, 0,0);
  gimp_table_attach(table,widget3,2,3,2,3, GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL, 0,0);

  set_tooltip(widget1,"Higher values makes the object reflect more light (appear lighter)");
  set_tooltip(widget2,"Controls how intense the highlights will be");
  set_tooltip(widget3,"Higher values makes the highlights more focused");

  g_object_set_data(G_OBJECT(widget1),"ValuePtr",(gpointer)&mapvals.material.diffuse_ref);
  g_object_set_data(G_OBJECT(widget2),"ValuePtr",(gpointer)&mapvals.material.specular_ref);
  g_object_set_data(G_OBJECT(widget3),"ValuePtr",(gpointer)&mapvals.material.highlight);

  attach_pixmap(table,diffref1_xpm,1,0);
  attach_pixmap(table,diffref2_xpm,3,0);
  attach_pixmap(table,specref1_xpm,1,1);
  attach_pixmap(table,specref2_xpm,3,1);
  attach_pixmap(table,high1_xpm,1,2);
  attach_pixmap(table,high2_xpm,3,2);

  return page;
}

/****************************************/
/* Create orientation and position page */
/****************************************/

GtkWidget *create_orientation_page(void)
{
  GtkWidget *page,*frame,*vbox,*label,*table;
  GtkWidget *widget1,*widget2,*widget3;

  page=gck_vbox_new(NULL,FALSE,FALSE,FALSE,0,0,0);

  frame=gck_frame_new("Position and orientation",page,GCK_SHADOW_ETCHED_IN,TRUE,TRUE,0,5);
  vbox=gck_vbox_new(frame,FALSE,FALSE,FALSE,0,0,5);

  widget1=gck_entryfield_new("X pos.:",vbox,mapvals.position.x,G_CALLBACK(xyzval_update));
  widget2=gck_entryfield_new("Y pos.:",vbox,mapvals.position.y,G_CALLBACK(xyzval_update));
  widget3=gck_entryfield_new("Z pos.:",vbox,mapvals.position.z,G_CALLBACK(xyzval_update));

  set_tooltip(widget1,"Object X position in XYZ space (0.5 is center)");
  set_tooltip(widget2,"Object Y position in XYZ space (0.5 is center)");
  set_tooltip(widget3,"Object Z position in XYZ space (0.5 is center)");

  g_object_set_data(G_OBJECT(widget1),"ValuePtr",(gpointer)&mapvals.position.x);
  g_object_set_data(G_OBJECT(widget2),"ValuePtr",(gpointer)&mapvals.position.y);
  g_object_set_data(G_OBJECT(widget3),"ValuePtr",(gpointer)&mapvals.position.z);

  table = gimp_table_new(3,2,FALSE);
  gimp_box_pack_start(vbox,table,TRUE,TRUE,5);

  label=gck_label_aligned_new("XY:",NULL,GCK_ALIGN_RIGHT,0.7);
  gimp_table_attach(table,label,0,1,0,1, GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL, 0,0);

  label=gck_label_aligned_new("YZ:",NULL,GCK_ALIGN_RIGHT,0.7);
  gimp_table_attach(table,label,0,1,1,2, GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL, 0,0);

  label=gck_label_aligned_new("XZ:",NULL,GCK_ALIGN_RIGHT,0.7);
  gimp_table_attach(table,label,0,1,2,3, GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL, 0,0);

  angle_scale_vals.value = mapvals.alpha;
  widget1=gck_hscale_new(NULL,NULL,&angle_scale_vals,G_CALLBACK(angle_update));
  angle_scale_vals.value = mapvals.beta;
  widget2=gck_hscale_new(NULL,NULL,&angle_scale_vals,G_CALLBACK(angle_update));
  angle_scale_vals.value = mapvals.gamma;
  widget3=gck_hscale_new(NULL,NULL,&angle_scale_vals,G_CALLBACK(angle_update));

  gimp_table_attach(table,widget1,1,2,0,1, GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL, 0,0);
  gimp_table_attach(table,widget2,1,2,1,2, GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL, 0,0);
  gimp_table_attach(table,widget3,1,2,2,3, GIMP_EXPAND|GIMP_FILL,GIMP_EXPAND|GIMP_FILL, 0,0);

  set_tooltip(widget1,"XY axis rotation angle");
  set_tooltip(widget2,"YZ axis rotation angle");
  set_tooltip(widget3,"XZ axis rotation angle");

  g_object_set_data(G_OBJECT(widget1),"ValuePtr",(gpointer)&mapvals.alpha);
  g_object_set_data(G_OBJECT(widget2),"ValuePtr",(gpointer)&mapvals.beta);
  g_object_set_data(G_OBJECT(widget3),"ValuePtr",(gpointer)&mapvals.gamma);

  return page;
}

/****************************/
/* Create notbook and pages */
/****************************/

void create_main_notebook(GtkWidget *container)
{
  GtkWidget *page;

  options_note_book=GTK_NOTEBOOK(gtk_notebook_new());
  gimp_container_add(container,GTK_WIDGET(options_note_book));

  page = create_options_page();
  gtk_notebook_append_page(options_note_book,page,gtk_label_new("Options"));

  page = create_light_page();
  gtk_notebook_append_page(options_note_book,page,gtk_label_new("Light"));

  page = create_material_page();
  gtk_notebook_append_page(options_note_book,page,gtk_label_new("Material"));

  page = create_orientation_page();
  gtk_notebook_append_page(options_note_book,page,gtk_label_new("Orientation"));
}

/*****************************************************/
/* Create and show main dialog. Uses the plugin_ui.c */
/* routines when possible, gtk itself when not.      */
/*****************************************************/

void create_main_dialog(void)
{
  GtkWidget *main_vbox,*main_workbox,*actionbox,*workbox1,*workbox1b,*workbox2,*vbox;
  GtkWidget *frame,*applybutton,*cancelbutton,*helpbutton,*hbox,*gridtoggle;
  GtkWidget *wid;
  GtkGesture *drag;

  appwin = gck_application_window_new("Map to object");
  g_signal_connect(appwin->widget,"close-request",
    G_CALLBACK(close_callback),NULL);

  /* Main manager widget */
  /* =================== */

  main_vbox=gck_vbox_new(appwin->widget,FALSE,FALSE,FALSE,8,0,0);

  /* Work area manager widget */
  /* ======================== */

  main_workbox=gck_hbox_new(main_vbox,FALSE,FALSE,FALSE,5,0,5);

  /* Action area manager widget */
  /* ========================== */

  gck_hseparator_new(main_vbox);
  actionbox=gck_hbox_new(main_vbox,TRUE,TRUE,TRUE,5,0,5);

  /* Add Ok, Cancel and Help buttons to the action area */
  /* ================================================== */

  applybutton=gck_pushbutton_new("Apply",actionbox,FALSE,TRUE,5,G_CALLBACK(apply_callback));
  cancelbutton=gck_pushbutton_new("Cancel",actionbox,FALSE,TRUE,5,G_CALLBACK(exit_callback));
  helpbutton=gck_pushbutton_new("Help",actionbox,FALSE,TRUE,5,NULL);

  gtk_window_set_default_widget(GTK_WINDOW(appwin->widget),applybutton);
  gtk_widget_set_sensitive(helpbutton,FALSE);

  set_tooltip(applybutton,"Apply filter with current settings");
  set_tooltip(cancelbutton,"Close filter without doing anything");

  /* Split the workarea in two */
  /* ========================= */

  frame=gck_frame_new(NULL,main_workbox,GCK_SHADOW_ETCHED_IN,TRUE,TRUE,0,0);
  workbox1=gck_vbox_new(frame,FALSE,TRUE,TRUE,5,0,5);
  workbox2=gck_vbox_new(main_workbox,FALSE,FALSE,FALSE,0,0,0);

  /* Add preview widget and various buttons to the first part */
  /* ======================================================== */

  frame=gck_frame_new(NULL,workbox1,GCK_SHADOW_IN,FALSE,FALSE,0,0);
  previewarea = gck_drawing_area_new(frame, PREVIEW_WIDTH, PREVIEW_HEIGHT,
    preview_draw, NULL);

  drag=gtk_gesture_drag_new();
  g_signal_connect(drag,"drag-begin",G_CALLBACK(preview_button_press),NULL);
  g_signal_connect(drag,"drag-update",G_CALLBACK(preview_button_motion),NULL);
  g_signal_connect(drag,"drag-end",G_CALLBACK(preview_button_release),NULL);
  gtk_widget_add_controller(previewarea,GTK_EVENT_CONTROLLER(drag));

  workbox1b=gck_vbox_new(workbox1,TRUE,TRUE,TRUE,0,0,0);
  hbox=gck_hbox_new(workbox1b,FALSE,TRUE,TRUE,5,0,0);
  wid=gck_pushbutton_new("Preview!",hbox,TRUE,TRUE,0,G_CALLBACK(preview_callback));
  set_tooltip(wid,"Recompute preview image");

  hbox=gck_hbox_new(hbox,FALSE,TRUE,TRUE,0,0,0);
  wid=gck_pushbutton_new("+",hbox,TRUE,TRUE,0,G_CALLBACK(zoomin_callback));
  set_tooltip(wid,"Zoom in (make image bigger)");
  wid=gck_pushbutton_new("-",hbox,TRUE,TRUE,0,G_CALLBACK(zoomout_callback));
  set_tooltip(wid,"Zoom out (make image smaller)");

  vbox = gck_vbox_new(workbox1b, FALSE, FALSE, FALSE, 0, 0, 5);
  gridtoggle=gck_checkbutton_new("Show preview wireframe",vbox,mapvals.showgrid,
    G_CALLBACK(togglegrid_update));
  g_object_set_data(G_OBJECT(gridtoggle),"ValuePtr",&mapvals.showgrid);
  set_tooltip(gridtoggle,"Show/hide preview wireframe");

  create_main_notebook(workbox2);

  /* Endmarkers for line table */
  /* ========================= */

  linetab[0].x1=-1;

  /* Phew :) Now lets check out the result of this mess */
  /* ================================================== */

  gtk_window_present(GTK_WINDOW(appwin->widget));

  gck_cursor_set(previewarea,"pointer");
}
