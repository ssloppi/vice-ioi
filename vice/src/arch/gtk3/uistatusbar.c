/** \file   uistatusbar.c
 *  \brief  Native GTK3 UI statusbar stuff
 *
 *  The status bar widget is part of every machine window. This widget
 *  reacts to UI notifications from the emulation core and otherwise
 *  does not interact with the rest of the main menu.
 *
 *  Functions described as "Statusbar API functions" are called by the
 *  rest of the UI or the emulation system itself to report that the
 *  status displays must be updated to reflect possibly new
 *  information. It is not necessary for the data to be truly new; the
 *  statusbar implementation will treat repeated reports of the same
 *  state as no-ops when necessary for performance.
 *
 *  \author Marco van den Heuvel <blackystardust68@yahoo.com>
 *  \author Michael C. Martin <mcmartin@gmail.com>
 *  \author Bas Wassink <b.wassink@ziggo.nl>
 *  \author David Hogan <david.q.hogan@gmail.com>
 */

/*
 * Resources controlled by this widget. We probably need to expand this list.
 * (Do not add resources controlled by widgets #include'd by this widget, only
 *  add resources actually controlled from this widget)
 *
 * $VICERES SoundVolume all
 */

/*
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"
#include <stdio.h>
#include <gtk/gtk.h>

#include "archdep_defs.h"
#include "attach.h"
#include "autostart.h"
#include "contentpreviewwidget.h"
#include "datasette.h"
#include "dirmenupopup.h"
#include "diskcontents.h"
#include "diskimage.h"
#include "diskimage/fsimage.h"
#include "drive-check.h"
#include "drive.h"
#include "drive.h"
#include "drivetypes.h"
#include "fliplist.h"
#include "joyport.h"
#include "joystickmenupopup.h"
#include "kbddebugwidget.h"
#include "lib.h"
#include "machine.h"
#include "mainlock.h"
#include "resources.h"
#include "statusbarrecordingwidget.h"
#include "statusbarspeedwidget.h"
#include "tapecontents.h"
#include "tapeport.h"
#include "types.h"
#include "ui.h"
#include "uiapi.h"
#include "uidatasette.h"
#include "uidiskattach.h"
#include "uifliplist.h"
#include "uisettings.h"
#include "userport/userport_joystick.h"
#include "vice_gtk3.h"

#include "uistatusbar.h"


/** \brief The maximum number of status bars we will permit to exist at once. */
#define MAX_STATUS_BARS 3

/** \brief  Timeout for statusbar messages in seconds */
#define MESSAGE_TIMEOUT 5

/** \brief  Maximum length for drive unit status string */
#define DRIVE_UNIT_STR_MAX_LEN 8

/** \brief  Maximum length for drive track status string */
#define DRIVE_TRACK_STR_MAX_LEN 16

/** \brief  Fold unit \a U and drive \a D into int and cast to pointer
 *
 * \param[in]   U   unit number
 * \param[in]   D   drive number
 */
#define UNIT_DRIVE_TO_PTR(U, D) GINT_TO_POINTER(((U) << 8) | ((D) & 0xff))

/** \brief  CSS for the drive widgets
 */
#define DRIVE_WIDGET_CSS \
    "label {\n" \
    "    font-family: monospace;\n" \
    "    font-size:100%;\n" \
    "}\n"


/** \brief  Status bar column indexes
 *
 * These values assume a proper emulator statusbar (ie not VSID).
 *
 * Rows 0 and 1
 */
enum {
    SB_COL_SPEED = 0,   /**< cpu/fps widget */
    SB_COL_SEP_SPEED ,  /**< separator between speed/fps and recording widget */
    SB_COL_CRT,         /**< crt widget */
    SB_COL_MIXER = SB_COL_CRT,   /**< mixer widget */

    SB_COL_SEP_CRT,     /**< separator between crt/mixer and tape widgets */
    SB_COL_TAPE,        /**< tape and joysticks widget */
    SB_COL_SEP_TAPE,    /**< separator between tape and joysticks widgets */
    SB_COL_DRIVE,       /**< drives widgets */
    SB_COL_SEP_DRIVE,   /**< separator between drives and volume widgets */
    SB_COL_VOLUME,      /**< volume widget */
    SB_COL_COUNT        /**< number of columns on the statusbar widget */
};


/* Row 2 */
enum {
    SB_COL_MSG = 0,
    SB_COL_MSG_SEP = 5,
    SB_COL_RECORD = 6
};

/* TODO: add more symbolic constants */
#define SB_ROW_SPEED        0
#define SB_ROW_SEP_SPEED    0
#define SB_ROW_CRT          0   /* CRT checkbox */
#define SB_ROW_SEP_CRT      0
#define SB_ROW_SEP_TAPE     0
#define SB_ROW_MIXER        1   /* Mixer checkbox */
#define SB_ROW_KBD_CHECK    2   /* Kbd debug checkbox */

#define SB_ROW_MSG_ROW_SEP  3

#define SB_ROW_MSG          4
#define SB_ROW_MSG_SEP      4
#define SB_ROW_RECORD       4
#define SB_ROW_SEP_RECORD   4

#define SB_ROW_KBD_ROW_SEP  5

#define SB_ROW_KBD          6


/** \brief Global data that custom status bar widgets base their rendering
 *         on.
 *
 *  This data is usually set by calls from the emulation core or from
 *  other parts of the UI in response to user commands or I/O events.
 *
 *  \todo The PET can have two tape drives.
 *
 *  \todo Two-unit drive units are not covered by this structure.
 */
typedef struct ui_sb_state_s {
    /** \brief Identifier for the currently displayed status bar
     * message.
     *
     * Used to correlate timeout events so that a new message
     * isn't erased by some older message timing out. */
    intptr_t statustext_msgid;

    /** \brief Current tape state (play, rewind, etc) */
    int tape_control[2];

    /** \brief Nonzero if the tape motor is powered. */
    int tape_motor_status[2];

    /** \brief Location on the tape of datasette #1 */
    int tape_counter[2];

    /** \brief Which drives are to be displayed in the status bar.
     *
     *  This is a bitmask, with bits 0-3 representing drives 8-11,
     *  respectively.
     */
    int drives_enabled;

    /** \brief  Drive type for each unit
     *
     * Used to determine if the layout needs changing due to changes in drive
     * type that change a unit's dual-drive status.
     */
    int drives_type[NUM_DISK_UNITS];

    /** \brief  Drive type changes involving dual-drive status
     *
     * Each bit represents a unit, with bit 0-3 -> representing units 8-11.
     * If a bit is set it means the dual-drive state of that unit has changed
     * and the UI needs updating of the widgets for that unit.
     */
    unsigned int drives_dual;

    /** \brief Nonzero if True Drive Emulation is active and drive
     *         LEDs should be drawn. */
    int drives_tde_enabled;

    /** \brief true if drive ui layout is needed */
    bool drives_layout_needed;

    /** \brief Color descriptors for the drive LED colors.
     *
     *  This value is a bitmask, with bit 0 and 1 set if the
     *  corresponding LED is green. Otherwise it is red. Drives that
     *  only have one LED will have their 'second' LED permanently at
     *  intensity zero so the value is irrelevant in that case. */
    int drive_led_types[NUM_DISK_UNITS];

    /** \brief Current intensity of each drive LED, 0=off,
     *         1000=max. */
    unsigned int current_drive_leds[NUM_DISK_UNITS][2][2];

    /** \brief true if a drive led has been changed */
    bool current_drive_leds_updated[NUM_DISK_UNITS][2][2];

    /** \brief unit:drive label for each unit and its drives */
    char current_drive_unit_str[NUM_DISK_UNITS][2][DRIVE_UNIT_STR_MAX_LEN];

    /** \brief true if a drive unit string has been changed */
    bool current_drive_unit_str_updated[NUM_DISK_UNITS][2];

    /** \brief device:track.halftrack label for each unit and its drives */
    char current_drive_track_str[NUM_DISK_UNITS][2][DRIVE_TRACK_STR_MAX_LEN];

    /** \brief true if a drive track string has been changed */
    bool current_drive_track_str_updated[NUM_DISK_UNITS][2];

    /** \brief Current state for each of the joyports.
     *
     *  This is an 7-bit bitmask, representing, from least to most
     *  significant bits: up, down, left, right, fire button,
     *  secondary fire button, tertiary fire button. */
    int current_joyports[JOYPORT_MAX_PORTS];

    /** \brief Which joystick ports are actually available.
     *
     *  This is a bitmask representing notional ports 0-4, which are
     *  themselves defined in joyport/joyport.h. Cases like a SIDcard
     *  control port on a Plus/4 without other userport control ports
     *  mean that the set of active joyports may be discontinuous. */
    int joyports_enabled;
} ui_sb_state_t;

/** \brief Used to safely access sb_state between threads. */
static pthread_mutex_t sb_state_lock = PTHREAD_MUTEX_INITIALIZER;

/** \brief The current state of the status bars across the UI.
 *
 * Don't use directly! Use lock_sb_state / unlock_sb_state instead.
 * I thought this pattern would make it more obvious to future
 * developers that they shouldn't just use sb_state without locking.
 */
static ui_sb_state_t sb_state_do_not_use_directly;

/** \brief The full structure representing a status bar widget.
 *
 *  This includes the top-level widget and then every subwidget that
 *  needs to be individually addressed or manipulated by the
 *  status-report API. */
typedef struct ui_statusbar_s {
    /** \brief The status bar widget proper.
     *
     *  This is the widget the rest of the UI code will store and pack
     *  into windows. */
    GtkWidget *bar;

    /** \brief  Widget displaying CPU speed and FPS
     *
     * Also used to set refresh rate, CPU speed, pause, warp and adv-frame
     */
    GtkWidget *speed;

    /** \brief  Stateful data used by the speed widget */
    statusbar_speed_widget_state_t speed_state;

    /** \brief  Status bar messages */
    GtkWidget *msg;

    /** \brief  Recording control/display widget */
    GtkWidget *record;

    /** \brief CRT control widget checkbox */
    GtkWidget *crt;

    /** \brief  Mixer control widget checkbox */
    GtkWidget *mixer;

    /** \brief The Tape #1 Status widget. */
    GtkWidget *tape1;

    /** \brief The Tape #2 Status widget (PET only). */
    GtkWidget *tape2;

    /** \brief  Used to optimise tape widget updates */
    int displayed_tape_counter[2];

    /** \brief The Tape #1 Status widget's popup menu. */
    GtkWidget *tape1_menu;

    /** \brief The Tape #2 Status widget's popup menu. */
    GtkWidget *tape2_menu;

    /** \brief The joyport status widget. */
    GtkWidget *joysticks;

    /** \brief The drive status widgets. */
    GtkWidget *drives[NUM_DISK_UNITS];

    /** \brief The popup menus associated with each drive. */
    GtkWidget *drive_popups[NUM_DISK_UNITS];

    /** \brief The volume control
     *
     * Only enabled for VSID at the moment.
     */
    GtkWidget *volume;

    /** \brief The hand-shaped cursor to change to when popup menus
     *         are available. */
    GdkCursor *hand_ptr;

    /** \brief  Keyboard debugging widget */
    GtkWidget *kbd_debug;

    /** \brief PRIMARY_WINDOW, SECONDARY_WINDOW, etc */
    int window_identity;
} ui_statusbar_t;


/** \brief The collection of status bars currently active.
 *
 *  Inactive status bars have a NULL pointer for their "bar" field. */
static ui_statusbar_t allocated_bars[MAX_STATUS_BARS];


/** \brief  Cursor used when hovering over the joysticks widgets
 *
 * TODO:    figure out if I need to clean this up or that Gtk will?
 */
static GdkCursor *joywidget_mouse_ptr = NULL;


/** \brief  Timeout ID of the message widget
 */
static guint timeout_id = 0;


/* Forward decl. */
static void tape_dir_autostart_callback(const char *image,
                                        int index,
                                        int device,
                                        unsigned int drive);
static void disk_dir_autostart_callback(const char *image,
                                        int index,
                                        int device,
                                        unsigned int drive);


/** \brief  Trigger redraw of a widget on the UI thread
 *
 * \param[in,out]   user_data   widget to redraw
 *
 * \return  FALSE
 */
static gboolean redraw_widget_on_ui_thread_impl(gpointer user_data)
{
    gtk_widget_queue_draw((GtkWidget *)user_data);

    return FALSE;
}

/** \brief Queue a redraw of widget on the ui thread.
 *
 * It's not safe to ask a widget to redraw from the vice thread.
 *
 * \param[in,out]   widget  widget to redraw
 */
static void redraw_widget_on_ui_thread(GtkWidget *widget)
{
    gdk_threads_add_timeout(0, redraw_widget_on_ui_thread_impl, (gpointer)widget);
}

/** \brief Get a locked reference to sb_state */
static ui_sb_state_t *lock_sb_state(void)
{
    pthread_mutex_lock(&sb_state_lock);
    return &sb_state_do_not_use_directly;
}

/** \brief Release a locked reference to sb_state_do_not_use_directly */
static void unlock_sb_state(void)
{
    pthread_mutex_unlock(&sb_state_lock);
}

/*****************************************************************************
 *                          Gtk3 event handlers                              *
 ****************************************************************************/


/** \brief  Timeout callback for the stausbar message widget
 *
 * \param[in,out]   data    message widget
 *
 * \return  FALSE (delete timer source)
 */
static gboolean message_timeout_handler(gpointer data)
{
    GtkLabel *label = data;

    gtk_label_set_text(label, "");
    timeout_id = 0; /* signal no timeouts pending (this should be fun) */
    return FALSE;
}



/** \brief Draws the tape icon based on the current control and motor status.
 *
 *  \param widget  The tape icon GtkDrawingArea being drawn to.
 *  \param cr      The cairo context that handles the drawing.
 *  \param data    tape port index
 *
 *  \return FALSE, telling GTK to continue event processing
 *
 *  \todo Once multiple tape drives are supported, the data parameter
 *        will be the integer index of which tape drive this widget
 *        represents.
 */
static gboolean draw_tape_icon_cb(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    int width, height;
    double x, y, inset;
    int tape_motor_status;
    int tape_control;
    ui_sb_state_t *sb_state;
    int index = GPOINTER_TO_INT(data);

    /* Copy any sb_state that we need to use - don't hold lock while drawing */
    sb_state = lock_sb_state();
    tape_motor_status = sb_state->tape_motor_status[index];
    tape_control = sb_state->tape_control[index];
    unlock_sb_state();

    width = gtk_widget_get_allocated_width(widget);
    height = gtk_widget_get_allocated_height(widget);
    if (width > height) {
        x = (width - height) / 2.0;
        y = 0.0;
        inset = height / 10.0;
    } else {
        x = 0.0;
        y = (height - width) / 2.0;
        inset = width / 10.0;
    }

    if (tape_motor_status) {
        cairo_set_source_rgb(cr, 0, 0.75, 0);
    } else {
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    }
    cairo_rectangle(cr, x + inset, y + inset, inset * 8, inset * 8);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0, 0, 0);
    switch (tape_control) {
    case DATASETTE_CONTROL_STOP:
        cairo_rectangle(cr, x + 2.5*inset, y + 2.5*inset, inset * 5, inset * 5);
        cairo_fill(cr);
        break;
    case DATASETTE_CONTROL_START:
        cairo_move_to(cr, x + 3*inset, y + 2.5*inset);
        cairo_line_to(cr, x + 3*inset, y + 7.5*inset);
        cairo_line_to(cr, x + 7*inset, y + 5*inset);
        cairo_close_path(cr);
        cairo_fill(cr);
        break;
    case DATASETTE_CONTROL_FORWARD:
        cairo_move_to(cr, x + 2.5*inset, y + 2.5*inset);
        cairo_line_to(cr, x + 2.5*inset, y + 7.5*inset);
        cairo_line_to(cr, x + 5*inset, y + 5*inset);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_move_to(cr, x + 5*inset, y + 2.5*inset);
        cairo_line_to(cr, x + 5*inset, y + 7.5*inset);
        cairo_line_to(cr, x + 7.5*inset, y + 5*inset);
        cairo_close_path(cr);
        cairo_fill(cr);
        break;
    case DATASETTE_CONTROL_REWIND:
        cairo_move_to(cr, x + 5*inset, y + 2.5*inset);
        cairo_line_to(cr, x + 5*inset, y + 7.5*inset);
        cairo_line_to(cr, x + 2.5*inset, y + 5*inset);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_move_to(cr, x + 7.5*inset, y + 2.5*inset);
        cairo_line_to(cr, x + 7.5*inset, y + 7.5*inset);
        cairo_line_to(cr, x + 5*inset, y + 5*inset);
        cairo_close_path(cr);
        cairo_fill(cr);
        break;
    case DATASETTE_CONTROL_RECORD:
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + 5*inset, y + 5*inset, 2.5*inset, 0, 2 * G_PI);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1, 0, 0);
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + 5*inset, y + 5*inset, 2*inset, 0, 2 * G_PI);
        cairo_close_path(cr);
        cairo_fill(cr);
        break;
    case DATASETTE_CONTROL_RESET:
    case DATASETTE_CONTROL_RESET_COUNTER:
    default:
        /* Things that aren't really controls look like we stop it. */
        /* TODO: Should RESET_COUNTER be wiped out by the time it gets here? */
        cairo_rectangle(cr, x + 2.5*inset, y + 2.5*inset, inset * 5, inset * 5);
        cairo_fill(cr);
    }

    return FALSE;
}


/** \brief  Handler for the 'activate' event of the 'Configure drives' item
 *
 * Opens the settings UI at the drive settings "page".
 *
 * \param[in]   widget  menu item triggering the event (unused)
 * \param[in]   data    extra event data (unused)
 */
static void on_drive_configure_activate(GtkWidget *widget, gpointer data)
{
    ui_settings_dialog_create_and_activate_node("peripheral/drive");
}


/** \brief  Handler for the 'activate' event of the 'Reset drive \#X' item
 *
 * Triggers a reset for drive ((int)data + 8)
 *
 * \param[in]   widget  menu item triggering the event (unused)
 * \param[in]   data    drive number (0-3)
 */
static void on_drive_reset_clicked(GtkWidget *widget, gpointer data)
{
    drive_cpu_trigger_reset(GPOINTER_TO_INT(data));
}


/** \brief  Handler for the 'activate' event of the 'Reset drive \#X in ... mode' item
 *
 * Triggers a reset for drive ((int)data + 8).
 *
 * \param[in]   widget  menu item triggering the event (unused)
 * \param[in]   data    drive number (0-3)
 */
static void on_drive_reset_config_clicked(GtkWidget *widget, gpointer data)
{
#if 0
    debug_gtk3("Resetting drive %d (button=%d)", ((GPOINTER_TO_INT(data)>>4)&15) + 8,
       GPOINTER_TO_INT(data) & 15 );
#endif
    drive_cpu_trigger_reset_button(((GPOINTER_TO_INT(data)>>4)&15),
       GPOINTER_TO_INT(data) & 15 );
}



/** \brief  Handler for the 'activate' event of 'Add image to fliplist'
 *
 * Adds the currently attached image of a drive to the fliplist.
 *
 * \param[in]   widget  menu item (unused)
 * \param[in]   data    unit number (int: 8-11)
 *
 * \todo    Support dual-drive units.
 */
static void on_drive_fliplist_add_activate(GtkWidget *widget, gpointer data)
{
    const struct disk_image_s *image;
    int unit = GPOINTER_TO_INT(data);

    image = file_system_get_image(unit, 0);
    if (image != NULL) {
        debug_gtk3("Adding '%s' to fliplist for unit #%d",
                   image->media.fsimage->name, unit);
        fliplist_add_image((unsigned int)unit);
    }
}


/** \brief  Handler for the 'activate' event of 'Clear fliplist'
 *
 * Clear the fliplist of a drive.
 *
 * \param[in]   widget  menu item (unused)
 * \param[in]   data    unit number (int: 8-11)
 *
 * \todo    Support dual-drive units.
 */
static void on_drive_fliplist_clear_activate(GtkWidget *widget, gpointer data)
{
    int unit = GPOINTER_TO_INT(data);

    debug_gtk3("Clearing fliplist of unit #%d", unit);
    fliplist_clear_list((unsigned int)unit);
}



/** \brief Draw the LED associated with some drive's LED state.
 *
 *  \param widget  The drive LED GtkDrawingArea being drawn to.
 *  \param cr      The cairo context that handles the drawing.
 *  \param data    The index (0-3) of which drive this represents.
 *
 *  \return FALSE, telling GTK to continue event processing
 */
static gboolean draw_drive_led_cb(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    int width, height;
    int unit;
    int drive;
    int i;
    double red = 0.0, green = 0.0, x, y, w, h;
    ui_sb_state_t *sb_state;

    width = gtk_widget_get_allocated_width(widget);
    height = gtk_widget_get_allocated_height(widget);
    unit = GPOINTER_TO_INT(data) & 0xff;
    drive = GPOINTER_TO_INT(data) >> 8;
#if 0
    debug_gtk3("unit = %d, drive = %d", unit, drive);
#endif
    sb_state = lock_sb_state();

    for (i = 0; i < 2; ++i) {
        int led_color = sb_state->drive_led_types[unit] & (1 << i);
        if (led_color) {
            green += sb_state->current_drive_leds[unit][drive][i] / 1000.0;
        } else {
            red += sb_state->current_drive_leds[unit][drive][i] / 1000.0;
        }
    }
    unlock_sb_state();

    /* Cairo clamps these for us */
    cairo_set_source_rgb(cr, red, green, 0);
    /* LED is half text height and aims for a 2x1 aspect ratio */
    h = height / 2.0;
    w = 2.0 * h;
    x = (width / 2.0) - h;
    y = height / 4.0;
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    return FALSE;
}


/** \brief Draw the current input status from a joyport.
 *
 *  This produces five squares arranged in a + shape, with directions
 *  represented as green squares when active and black when not. The
 *  fire buttons are represented by the central square, with red,
 *  green, and blue components representing the three possible
 *  buttons.
 *
 *  For traditional Commodore joysticks, there is only one fire button
 *  and this will be diplayed as a red square when the button is
 *  pressed.
 *
 *  \param widget  The joyport GtkDrawingArea being drawn to.
 *  \param cr      The cairo context that handles the drawing.
 *  \param data    The index (0-4) of which joyport this represents.
 *
 *  \return FALSE, telling GTK to continue event processing
 */
static gboolean draw_joyport_cb(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    int width, height, val;
    double e, s, x, y;
    ui_sb_state_t *sb_state;

    /* FIXME This is called very often due to cpu/fps label updates
     * triggering a relayout/redraw */

    width = gtk_widget_get_allocated_width(widget);
    height = gtk_widget_get_allocated_height(widget);

    sb_state = lock_sb_state();
    val = sb_state->current_joyports[GPOINTER_TO_INT(data)];
    unlock_sb_state();

    /* This widget "wants" to draw 6x6 squares inside a 20x20
     * space. We compute x and y offsets for a scaled square within
     * the real widget space, and then the actual widths for a square
     * edge (e) and the spaces between them (s). */

    if (width > height) {
        s = height / 20.0;
        x = (width - height) / 2.0;
        y = 0.0;
    } else {
        s = width / 20.0;
        y = (height - width) / 2.0;
        x = 0.0;
    }
    e = s * 5.0;

    /* Then we render the five squares. This seems like it could be
     * done more programatically, but enough changes each iteration
     * that we might as well unroll it. */

    /* Up: Bit 0 */
    cairo_set_source_rgb(cr, 0, (val&0x01) ? 1 : 0, 0);
    cairo_rectangle(cr, x + e + 2*s, y+s, e, e);
    cairo_fill(cr);
    /* Down: Bit 1 */
    cairo_set_source_rgb(cr, 0, (val&0x02) ? 1 : 0, 0);
    cairo_rectangle(cr, x + e + 2*s, y + 2*e + 3*s, e, e);
    cairo_fill(cr);
    /* Left: Bit 2 */
    cairo_set_source_rgb(cr, 0, (val&0x04) ? 1 : 0, 0);
    cairo_rectangle(cr, x + s, y + e + 2*s, e, e);
    cairo_fill(cr);
    /* Right: Bit 3 */
    cairo_set_source_rgb(cr, 0, (val&0x08) ? 1 : 0, 0);
    cairo_rectangle(cr, x + 2*e + 3*s, y + e + 2*s, e, e);
    cairo_fill(cr);
    /* Fire buttons: Bits 4-6. Each of the three notional fire buttons
     * controls the red, green, or blue color of the fire button
     * area. By default, we are using one-button joysticks and so this
     * region will be either black or red. */
    cairo_set_source_rgb(cr, (val&0x10) ? 1 : 0,
                             (val&0x20) ? 1 : 0,
                             (val&0x40) ? 1 : 0);
    cairo_rectangle(cr, x + e + 2*s, y + e + 2*s, e, e);
    cairo_fill(cr);

    return FALSE;
}


/** \brief Respond to mouse clicks on the tape status widget.
 *
 *  This displays the tape control popup menu.
 *
 *  \param widget  The GtkWidget that received the click. Ignored.
 *  \param event   The event representing the bottom operation.
 *  \param data    An integer representing which window's status bar was
 *                 clicked and thus where the popup window should go,
 *                 and tape port number shifted 8 bits to the right.
 *
 *  \return TRUE if further event processing should be skipped.
 *
 *  \todo This callback and the way it is configured both will need to
 *        be significantly reworked to manage multiple tape drives.
 */
static gboolean ui_do_datasette_popup(GtkWidget *widget,
                                      GdkEvent *event,
                                      gpointer data)
{
    int i = GPOINTER_TO_INT(data) & 0xff;
    int port = GPOINTER_TO_INT(data) >> 8;

    mainlock_assert_is_not_vice_thread();

    if (((GdkEventButton *)event)->button == GDK_BUTTON_PRIMARY) {
        GtkWidget *tape_menu;

        if (port == TAPEPORT_UNIT_1) {
            if (allocated_bars[i].tape1 && allocated_bars[i].tape1_menu
                    && event->type == GDK_BUTTON_PRESS) {
                tape_menu = allocated_bars[i].tape1_menu;

                /* update sensitivity of tape controls */
                ui_datasette_update_sensitive(tape_menu, port);

                gtk_menu_popup_at_widget(GTK_MENU(tape_menu),
                                     allocated_bars[i].tape1,
                                     GDK_GRAVITY_NORTH_EAST,
                                     GDK_GRAVITY_SOUTH_EAST,
                                     event);
            }
            return TRUE;
        } else {
            if (allocated_bars[i].tape2 && allocated_bars[i].tape2_menu
                    && event->type == GDK_BUTTON_PRESS) {
                tape_menu = allocated_bars[i].tape2_menu;

                /* update sensitivity of tape controls */
                ui_datasette_update_sensitive(tape_menu, port);

                gtk_menu_popup_at_widget(GTK_MENU(tape_menu),
                                     allocated_bars[i].tape2,
                                     GDK_GRAVITY_NORTH_EAST,
                                     GDK_GRAVITY_SOUTH_EAST,
                                     event);
            }
            return TRUE;
        }

    } else if (((GdkEventButton *)event)->button == GDK_BUTTON_SECONDARY) {
        GtkWidget *dir_menu;

        dir_menu = dir_menu_popup_create(port,
                                         tapecontents_read,
                                         tape_dir_autostart_callback);
        gtk_menu_popup_at_widget(GTK_MENU(dir_menu),
                                 widget,
                                 GDK_GRAVITY_NORTH_EAST,
                                 GDK_GRAVITY_SOUTH_EAST,
                                 event);
        return TRUE;
    }
    return FALSE;
}


/** \brief  Respond to mouse clicks on a disk drive status widget.
 *
 *  This displays the drive control popup menu.
 *
 *  \param[in]  widget  The GtkWidget that received the click. Ignored.
 *  \param[in]  event   The event representing the bottom operation.
 *  \param[in]  data    An integer representing which window's status bar was
 *                      clicked and thus where the popup window should go.
 *
 *  \return TRUE if further event processing should be skipped.
 */
static gboolean ui_do_drive_popup(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    GtkWidget *drive_menu;
    GtkWidget *drive_menu_item;
    gchar buffer[256];
    int i = GPOINTER_TO_INT(data);

    mainlock_assert_is_not_vice_thread();

    drive_menu = allocated_bars[0].drive_popups[i];
    ui_populate_fliplist_menu(drive_menu, i + DRIVE_UNIT_MIN, 0);

    /* XXX: this code is a duplicate of the drive_menu creation code, so we
     *      should probably refactor this a bit
     */
    gtk_container_add(GTK_CONTAINER(drive_menu),
            gtk_separator_menu_item_new());
    drive_menu_item = gtk_menu_item_new_with_label("Configure drives ...");
    g_signal_connect(drive_menu_item, "activate",
            G_CALLBACK(on_drive_configure_activate), NULL);
    gtk_container_add(GTK_CONTAINER(drive_menu), drive_menu_item);

    /*
     * Add drive reset item
     */
    g_snprintf(buffer, sizeof(buffer), "Reset drive #%d", i + DRIVE_UNIT_MIN);
    drive_menu_item = gtk_menu_item_new_with_label(buffer);
    g_signal_connect(drive_menu_item, "activate",
            G_CALLBACK(on_drive_reset_clicked), GINT_TO_POINTER(i));
    gtk_container_add(GTK_CONTAINER(drive_menu), drive_menu_item);

    /* Add reset to configuration mode for CMD HDs */
    if ((drive_has_buttons(i) & 1) == 1) {
        g_snprintf(buffer, sizeof(buffer),
                "Reset drive #%d to Configuration Mode", i + DRIVE_UNIT_MIN);
        drive_menu_item = gtk_menu_item_new_with_label(buffer);
        g_signal_connect(drive_menu_item, "activate",
               G_CALLBACK(on_drive_reset_config_clicked),
               GINT_TO_POINTER((i << 4) + 1));
        gtk_container_add(GTK_CONTAINER(drive_menu), drive_menu_item);
    }
    /* Add reset to installation mode for CMD HDs */
    if ((drive_has_buttons(i) & 6) == 6) {
        g_snprintf(buffer, sizeof(buffer),
                "Reset drive #%d to Installation Mode", i + DRIVE_UNIT_MIN);
        drive_menu_item = gtk_menu_item_new_with_label(buffer);
        g_signal_connect(drive_menu_item, "activate",
               G_CALLBACK(on_drive_reset_config_clicked),
               GINT_TO_POINTER((i << 4) + 6));
        gtk_container_add(GTK_CONTAINER(drive_menu), drive_menu_item);
    }

    /* Add 'add image to fliplist' */
    gtk_container_add(GTK_CONTAINER(drive_menu), gtk_separator_menu_item_new());
    drive_menu_item = gtk_menu_item_new_with_label("Add current image to fliplist");
    g_signal_connect(drive_menu_item, "activate",
            G_CALLBACK(on_drive_fliplist_add_activate),
            GINT_TO_POINTER(i + DRIVE_UNIT_MIN));
    gtk_widget_set_sensitive(drive_menu_item,
                            file_system_get_image(i + DRIVE_UNIT_MIN, 0) != NULL);
    gtk_container_add(GTK_CONTAINER(drive_menu), drive_menu_item);

    /* Add 'clear fliplist' */
    g_snprintf(buffer,
               sizeof(buffer),
               "Clear drive #%d:0 fliplist",
               i + DRIVE_UNIT_MIN);
    drive_menu_item = gtk_menu_item_new_with_label(buffer);
    g_signal_connect(drive_menu_item,
                     "activate",
                     G_CALLBACK(on_drive_fliplist_clear_activate),
                     GINT_TO_POINTER(i + DRIVE_UNIT_MIN));
    gtk_widget_set_sensitive(drive_menu_item,
                             fliplist_init_iterate(i + DRIVE_UNIT_MIN) != NULL);
    gtk_container_add(GTK_CONTAINER(drive_menu), drive_menu_item);

    gtk_widget_show_all(drive_menu);

    if (((GdkEventButton *)event)->button == GDK_BUTTON_PRIMARY) {
        /* show popup for attaching/detaching disk images */
        gtk_menu_popup_at_widget(GTK_MENU(drive_menu),
                                 widget,
                                 GDK_GRAVITY_NORTH_EAST,
                                 GDK_GRAVITY_SOUTH_EAST,
                                 event);
    } else if (((GdkEventButton *)event)->button == GDK_BUTTON_SECONDARY) {
        /* show popup to run file in currently attached image */
        GtkWidget *dir_menu = dir_menu_popup_create(i + DRIVE_UNIT_MIN,
                                                    diskcontents_filesystem_read,
                                                    disk_dir_autostart_callback);

        /* show popup for selecting file in currently attached image */
        gtk_menu_popup_at_widget(GTK_MENU(dir_menu),
                                 widget,
                                 GDK_GRAVITY_NORTH_EAST,
                                 GDK_GRAVITY_SOUTH_EAST,
                                 event);
    }

    return TRUE;
}


/** \brief  Handler for the enter/leave-notify events of the joysticks widget
 *
 * \param[in]   widget      widget triggering the event
 * \param[in]   event       event reference
 * \param[in]   user_data   extra event data (unused)
 *
 * \return  bool    (FALSE = keep propagating event, TRUE = stop)
 */
static gboolean on_joystick_widget_hover(GtkWidget *widget, GdkEvent *event,
                                         gpointer user_data)
{
    if (event != NULL) {
        GdkDisplay *display = gtk_widget_get_display(widget);
        GdkWindow *window = gtk_widget_get_window(widget);
        GdkCursor *cursor;

        if (display == NULL) {
            debug_gtk3("failed to retrieve GdkDisplay.");
            return FALSE;
        }
        if (window == NULL) {
            debug_gtk3("failed to retrieve GdkWindow.");
            return FALSE;
        }

        if (event->type == GDK_ENTER_NOTIFY) {
            if (joywidget_mouse_ptr == NULL) {
                joywidget_mouse_ptr = gdk_cursor_new_from_name(display, "pointer");
            }
            cursor = joywidget_mouse_ptr;

        } else {
            cursor = NULL;
        }
        gdk_window_set_cursor(window, cursor);
    }
    return FALSE;
}


/** \brief  Handler for button-press events of the joysticks widget
 *
 * \param[in]   widget      widget triggering the event
 * \param[in]   event       event reference
 * \param[in]   user_data   extra event data (unused)
 *
 * \return  TRUE to stop other handlers, FALSE to propagate event further
 */
static gboolean on_joystick_widget_button_press(GtkWidget *widget,
                                                GdkEvent *event,
                                                gpointer user_data)
{
    mainlock_assert_is_not_vice_thread();

    if (((GdkEventButton *)event)->button == GDK_BUTTON_PRIMARY) {
        GtkWidget *menu = joystick_menu_popup_create();
        gtk_menu_popup_at_widget(GTK_MENU(menu), widget,
                GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_SOUTH_WEST,
                event);
        return TRUE;
    }
    return FALSE;
}


/** Event handler for hovering over a clickable part of the status bar.
 *
 *  This will switch to or from the "hand" cursor as needed, creating
 *  it if necessary.
 *
 *  \param widget    The widget firing the event
 *  \param event     The GdkEventCross that caused the callback
 *  \param user_data The ui_statusbar_t object containing widget
 *
 *  \return TRUE if further event processing should be blocked.
 */
static gboolean ui_statusbar_cross_cb(GtkWidget *widget,
                                      GdkEvent *event,
                                      gpointer user_data)
{
    ui_statusbar_t *sb = (ui_statusbar_t *)user_data;

    if (event && event->type == GDK_ENTER_NOTIFY) {
        GdkDisplay *display;
        /* Sanity check arguments */
        if (sb == NULL) {
            /* Should be impossible */
            return FALSE;
        }
        /* If the "hand" pointer hasn't been created yet, create it */
        display = gtk_widget_get_display(widget);
        if (display != NULL && sb->hand_ptr == NULL) {
            sb->hand_ptr = gdk_cursor_new_from_name(display, "pointer");
            if (sb->hand_ptr != NULL) {
                g_object_ref_sink(G_OBJECT(sb->hand_ptr));
            } else {
                fprintf(stderr, "GTK3 CURSOR: Could not allocate custom"
                       " pointer for status bar\n");
            }
        }
        /* If the "hand" pointer is OK, use it */
        if (sb->hand_ptr != NULL) {
            GdkWindow *window = gtk_widget_get_window(widget);
            if (window) {
                gdk_window_set_cursor(window, sb->hand_ptr);
            }
        }
    } else {
        /* We're leaving the target widget, so change the pointer back
         * to default */
        GdkWindow *window = gtk_widget_get_window(widget);
        if (window) {
            gdk_window_set_cursor(window, NULL);
        }
    }
    return FALSE;
}


/** \brief  Widget destruction callback for status bars.
 *
 * \param sb      The status bar being destroyed. This should be
 *                registered in some ui_statusbar_t structure as the
 *                bar field.
 * \param ignored User data pointer mandated by GTK. Unused.
 */
static void destroy_statusbar_cb(GtkWidget *sb, gpointer ignored)
{
    int i, j;

    mainlock_assert_is_not_vice_thread();

    for (i = 0; i < MAX_STATUS_BARS; ++i) {
        if (allocated_bars[i].bar == sb) {
            allocated_bars[i].bar = NULL;

            if (allocated_bars[i].msg) {
                g_object_unref(G_OBJECT(allocated_bars[i].msg));
            }

            if (allocated_bars[i].record) {
                g_object_unref(G_OBJECT(allocated_bars[i].record));
                allocated_bars[i].record = NULL;
            }

            if (allocated_bars[i].speed != NULL) {
                g_object_unref(G_OBJECT(allocated_bars[i].speed));
                allocated_bars[i].speed = NULL;
            }

            if (allocated_bars[i].crt != NULL) {
                g_object_unref(G_OBJECT(allocated_bars[i].crt));
                allocated_bars[i].crt = NULL;
            }
            if (allocated_bars[i].mixer != NULL) {
                g_object_unref(G_OBJECT(allocated_bars[i].mixer));
                allocated_bars[i].mixer = NULL;
            }
            if (allocated_bars[i].tape1) {
                g_object_unref(G_OBJECT(allocated_bars[i].tape1));
                allocated_bars[i].tape1 = NULL;
            }
            if (allocated_bars[i].tape2) {
                g_object_unref(G_OBJECT(allocated_bars[i].tape2));
                allocated_bars[i].tape2 = NULL;
            }
            if (allocated_bars[i].tape1_menu) {
                g_object_unref(G_OBJECT(allocated_bars[i].tape1_menu));
                allocated_bars[i].tape1_menu = NULL;
            }
            if (allocated_bars[i].tape1_menu) {
                g_object_unref(G_OBJECT(allocated_bars[i].tape1_menu));
                allocated_bars[i].tape1_menu = NULL;
            }
            if (allocated_bars[i].joysticks) {
                g_object_unref(G_OBJECT(allocated_bars[i].joysticks));
                allocated_bars[i].joysticks = NULL;
            }
            for (j = 0; j < NUM_DISK_UNITS; ++j) {
                if (allocated_bars[i].drives[j]) {
                    g_object_unref(G_OBJECT(allocated_bars[i].drives[j]));
                    g_object_unref(G_OBJECT(allocated_bars[i].drive_popups[j]));
                    allocated_bars[i].drives[j] = NULL;
                    allocated_bars[i].drive_popups[j] = NULL;
                }
            }
            if (allocated_bars[i].volume != NULL) {
                g_object_unref(G_OBJECT(allocated_bars[i].volume));
                allocated_bars[i].volume = NULL;
            }
            /* why? */
            if (allocated_bars[i].kbd_debug != NULL) {
                g_object_unref(G_OBJECT(allocated_bars[i].kbd_debug));
                allocated_bars[i].kbd_debug = NULL;
            }


            if (allocated_bars[i].hand_ptr) {
                g_object_unref(G_OBJECT(allocated_bars[i].hand_ptr));
                allocated_bars[i].hand_ptr = NULL;
            }
        }
    }
}


/** \brief  Handler for the 'toggled' event of the CRT controls checkbox
 *
 * Toggles the display state of the CRT controls
 *
 * \param[in]   widget  checkbox triggering the event
 * \param[in]   data    extra event data (unused
 */
static void on_crt_toggled(GtkWidget *widget, gpointer data)
{
    gboolean state;

    mainlock_assert_is_not_vice_thread();

    state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    ui_enable_crt_controls((gboolean)state);
}


/** \brief  Handler for the 'toggled' event of the mixer controls checkbox
 *
 * Toggles the display state of the mixer controls
 *
 * \param[in]   widget  checkbox triggering the event
 * \param[in]   data    extra event data (unused
 */
static void on_mixer_toggled(GtkWidget *widget, gpointer data)
{
    gboolean state;

    mainlock_assert_is_not_vice_thread();

    state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    ui_enable_mixer_controls((gboolean)state);
}


/** \brief  Handler for the 'value-changed' event of the volume control
 *
 * Updates the master volume
 *
 * \param[in]   widget  GtkVolumeButton control
 * \param[in]   value   new volume value (1.0 - 0.0)
 * \param[in]   data    extra event data (unused
 */
static void on_volume_value_changed(GtkScaleButton *widget,
                                    gdouble value,
                                    gpointer data)
{
    resources_set_int("SoundVolume", (int)(value * 100.0));
}


/*****************************************************************************
 *                          Private functions                                *
 ****************************************************************************/


/** \brief Extracts the list of enabled drives from the DriveType
 *         resources.
 *
 *  \return A bitmask value suitable for ui_sb_state_s::drives_enabled.
 */
static int compute_drives_enabled_mask(void)
{
    int unit, mask;
    int result = 0;
    for (unit = 0, mask = 1; unit < NUM_DISK_UNITS; ++unit, mask <<= 1) {
        int status = 0, value = 0;
        status = resources_get_int_sprintf("Drive%dType", &value, unit + DRIVE_UNIT_MIN);
        if (status == 0 && value != 0) {
            result |= mask;
        }
    }
    return result;
}


/** \brief Create a new drive widget for inclusion in the status bar.
 *
 *  \param unit The drive unit to create (0-3, indicating devices
 *              8-11)
 *
 *  \return The constructed widget. This widget will be a floating
 *          reference.
 */
static GtkWidget *ui_drive_widget_create(int unit)
{
    GtkWidget *grid, *number, *track, *led;
    GtkCssProvider *css_provider;
    mainlock_assert_is_not_vice_thread();

    grid = gtk_grid_new();
    gtk_widget_set_hexpand(grid, FALSE);
    /* create reusable CSS provider for the unit/drive and track labels */
    css_provider = vice_gtk3_css_provider_new(DRIVE_WIDGET_CSS);

    for (int drive_num = 0; drive_num < 2; drive_num++) {
        char drive_id[8];

        g_snprintf(drive_id,
                   sizeof(drive_id),
                   "%2d:%d",
                   unit + DRIVE_UNIT_MIN,
                   drive_num);
        number = gtk_label_new(drive_id);
        gtk_widget_set_halign(number, GTK_ALIGN_START);
        vice_gtk3_css_provider_add(number, css_provider);

        track = gtk_label_new(" 18.5");
        gtk_widget_set_hexpand(track, TRUE);
        gtk_widget_set_halign(track, GTK_ALIGN_END);
        vice_gtk3_css_provider_add(track, css_provider);

        led = gtk_drawing_area_new();
        gtk_widget_set_size_request(led, 30, 15);
        gtk_widget_set_no_show_all(led, TRUE);

        gtk_grid_attach(GTK_GRID(grid), number, 0, drive_num, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), track, 1, drive_num, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), led, 2, drive_num, 1, 1);

        /* Labels will notice clicks by default, but drawing areas need to
         * be told to. */
        gtk_widget_add_events(led, GDK_BUTTON_PRESS_MASK);
        g_signal_connect_unlocked(led,
                                  "draw",
                                  G_CALLBACK(draw_drive_led_cb),
                                  GINT_TO_POINTER(unit | (drive_num << 8)));
    }

    return grid;
}


/** \brief  Callback for the disk directory popup menu
 *
 * Autostarts the selected file in the directory
 *
 * \param[in]   image   image name
 * \param[in]   index   directory index of the file to start
 * \param[in]   device  device number (0-3)
 * \param[in]   drive   drive number (0 or 1) of device
 */
static void disk_dir_autostart_callback(const char *image,
                                        int index,
                                        int device,
                                        unsigned int drive)
{
    char *autostart_image;

    /* make a copy of the image name since autostart will reattach the disk
     * image, freeing memory used by the image name passed to us in the process
     */
    autostart_image = lib_strdup(image);
    autostart_disk(device + 8, drive, autostart_image, NULL, index + 1, AUTOSTART_MODE_RUN);
    lib_free(autostart_image);
}


/** \brief  Callback for the tape directory popup menu
 *
 * Autostarts the selected file in the directory
 *
 * \param[in]   image   image name
 * \param[in]   index   directory index of the file to start
 * \param[in]   device  device number (unused, but perhaps useful for PET)
 * \param[in]   drive   drive number (unused)
 */
static void tape_dir_autostart_callback(const char *image,
                                        int index,
                                        int device,
                                        unsigned int drive)
{
    char *autostart_image;

    /* make a copy of the image name since autostart will reattach the tape
     * image, freeing memory used by the image name passed to us in the process
     */
    autostart_image = lib_strdup(image);
    autostart_tape(autostart_image, NULL, index + 1, AUTOSTART_MODE_RUN, TAPEPORT_PORT_1 /* FIXME */);
    lib_free(autostart_image);
}


/** \brief Create a new tape widget for inclusion in the status bar.
 *
 *  \return The constructed widget. This widget will be a floating
 *          reference.
 */
static GtkWidget *ui_tape_widget_create(int port)
{
    GtkWidget *grid, *header, *counter, *state;
    gchar title[256];

    mainlock_assert_is_not_vice_thread();


    grid = gtk_grid_new();
    gtk_orientable_set_orientation(GTK_ORIENTABLE(grid),
            GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_hexpand(grid, FALSE);
    gtk_widget_set_vexpand(grid, FALSE);

    if (machine_class == VICE_MACHINE_PET) {
        g_snprintf(title, sizeof(title), "Tape #%d:", port);
        header = gtk_label_new(title);
    } else {
        header = gtk_label_new("Tape:");
    }
    gtk_widget_set_hexpand(header, FALSE);
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    g_object_set(header, "margin-right", 8, NULL);

    counter = gtk_label_new("?");
    state = gtk_drawing_area_new();
    gtk_widget_set_size_request(state, 20, 20);
    /* Labels will notice clicks by default, but drawing areas need to
     * be told to. */
    gtk_widget_add_events(state, GDK_BUTTON_PRESS_MASK);
    gtk_container_add(GTK_CONTAINER(grid), header);
    gtk_container_add(GTK_CONTAINER(grid), counter);
    gtk_container_add(GTK_CONTAINER(grid), state);
    g_signal_connect_unlocked(state, "draw", G_CALLBACK(draw_tape_icon_cb),
            GINT_TO_POINTER(port - 1));
    return grid;
}


/** \brief Alter widget visibility within the joyport widget so that
 *         only currently existing joystick ports are displayed.
 *
 * \param[in]   state_snaphshot previous state of the joyport widget
 *
 * \note    It is safe to call this routine regularly, as it will only trigger
 *          UI refresh operations if the configuration has changed to no
 *          longer match the current layout.
 */
static void update_joyport_layout(ui_sb_state_t *state_snapshot)
{
    int i, ok[JOYPORT_MAX_PORTS];
    int new_joyport_mask = 0;

    for (i = 0; i < JOYPORT_MAX_PORTS; ++i) {
        ok[i] = joyport_port_is_active(i);
    }

    /* Now that we have a list of disabled/enabled ports, let's check
     * to see if anything has changed */
    for (i = 0; i < JOYPORT_MAX_PORTS; ++i) {
        new_joyport_mask <<= 1;
        if (ok[i]) {
            new_joyport_mask |= 1;
        }
    }
    if (new_joyport_mask != state_snapshot->joyports_enabled) {
        int j;
        state_snapshot->joyports_enabled = new_joyport_mask;
        for (j = 0; j < MAX_STATUS_BARS; ++j) {
            GtkWidget *joyports_grid;

            if (allocated_bars[j].joysticks == NULL) {
                continue;
            }
            joyports_grid =  gtk_bin_get_child(
                    GTK_BIN(allocated_bars[j].joysticks));

            /* Hide and show the joystick ports as required */
            for (i = 0; i < JOYPORT_MAX_PORTS; ++i) {
                GtkWidget *child = gtk_grid_get_child_at(
                        GTK_GRID(joyports_grid), 1+i, 0);
                if (child) {
                    if (ok[i]) {
                        gtk_widget_set_no_show_all(child, FALSE);
                        gtk_widget_show_all(child);
                    } else {
                        gtk_widget_set_no_show_all(child, TRUE);
                        gtk_widget_hide(child);
                    }
                }
            }
        }
    }
}


/** \brief  Create a master joyport widget for inclusion in the status bar.
 *
 *  Individual joyport representations are part of this widget and
 *  update functions will index the GtkGrid in the master widget to
 *  reach them.
 *
 *  \return The constructed widget. This widget will be a floating
 *          reference.
 */
static GtkWidget *ui_joystick_widget_create(void)
{
    GtkWidget *grid;
    GtkWidget *label;
    GtkWidget *event_box;
    int i;

    mainlock_assert_is_not_vice_thread();

    grid = gtk_grid_new();
    gtk_orientable_set_orientation(GTK_ORIENTABLE(grid),
            GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_hexpand(grid, FALSE);
    label = gtk_label_new("Joysticks:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(label, FALSE);
    g_object_set(label, "margin-right", 8, NULL);
    gtk_container_add(GTK_CONTAINER(grid), label);
    /* Create all possible joystick displays */
    for (i = 0; i < JOYPORT_MAX_PORTS; ++i) {
        GtkWidget *joyport = gtk_drawing_area_new();
        /* add events it should respond to */
        gtk_widget_add_events(joyport,
                GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|
                GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK);
        gtk_widget_set_size_request(joyport,20,20);
        gtk_container_add(GTK_CONTAINER(grid), joyport);
        g_signal_connect_unlocked(joyport, "draw", G_CALLBACK(draw_joyport_cb),
                GINT_TO_POINTER(i));
        gtk_widget_set_no_show_all(joyport, TRUE);
        gtk_widget_hide(joyport);
    }

    /*
     * Pack the joystick grid into an event box so we can have a popup menu and
     * also change the cursor shape to indicate to the user the joystick widget
     * is clickable.
     */
    event_box = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box), FALSE);
    gtk_container_add(GTK_CONTAINER(event_box), grid);

    g_signal_connect(event_box, "button-press-event",
            G_CALLBACK(on_joystick_widget_button_press), NULL);
    g_signal_connect(event_box, "enter-notify-event",
            G_CALLBACK(on_joystick_widget_hover), NULL);
    g_signal_connect(event_box, "leave-notify-event",
            G_CALLBACK(on_joystick_widget_hover), NULL);

    return event_box;
}


/** \brief Lay out the disk drive widgets inside a status bar.
 *
 *  Depending on which drives are enabled, any given drive may appear
 *  on different columns or even rows. This routine handles that flow
 *  as the configuration changes.
 *
 *  \param bar_index Which status bar to lay out.
 */
static void layout_statusbar_drives(ui_sb_state_t *state_snapshot, int bar_index)
{
    int i, j, state, tde = 0;
    unsigned int dual;
    int enabled_drive_index = 0;
    GtkWidget *bar = allocated_bars[bar_index].bar;

    if (!bar) {
        return;
    }
    /* Delete all the drives and dividers that may exist. WARNING:
     * This code assumes that the drive widgets are the rightmost
     * elements of the status bar. */
    for (i = 0; i < ((NUM_DISK_UNITS + 1) / 2) * 2; ++i) {
        for (j = 0; j < 2; ++j) {
            GtkWidget *child = gtk_grid_get_child_at(GTK_GRID(bar),
                    SB_COL_DRIVE + i, j);
            if (child) {
                /* Fun GTK3 fact! If you destroy an event box, then
                 * even if the thing it contains still has references
                 * left, that child is destroyed _anyway_. To avoid
                 * this tragic eventuality, we detach the child files
                 * before removing the box. */
                /* TODO: This implies that we really should not be
                 * relying on g_object_ref to preserve objects, and
                 * instead keep track of widget indices the hard
                 * way. */
                if (GTK_IS_EVENT_BOX(child)) {
                    GtkWidget *grandchild = gtk_bin_get_child(GTK_BIN(child));
                    gtk_container_remove(GTK_CONTAINER(child), grandchild);
                }
                gtk_container_remove(GTK_CONTAINER(bar), child);
            }
        }
    }

    state = state_snapshot->drives_enabled;
    tde = state_snapshot->drives_tde_enabled;
    dual = state_snapshot->drives_dual;
    for (i = 0; i < NUM_DISK_UNITS; ++i) {

        if ((state & 1) || (dual & 1)) {
            GtkWidget *drive = allocated_bars[bar_index].drives[i];
            GtkWidget *event_box = gtk_event_box_new();
            int row = enabled_drive_index % 2;
            int column = (enabled_drive_index / 2) * 2 + SB_COL_DRIVE;
            unsigned int drive_dual = drive_check_dual(diskunit_context[i]->type);

            if (row == 0) {
                gtk_grid_attach(GTK_GRID(bar),
                        gtk_separator_new(GTK_ORIENTATION_VERTICAL),
                        column - 1, 0, 1, 2);
            }
            gtk_container_add(GTK_CONTAINER(event_box), drive);
            gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box), FALSE);
            g_signal_connect(event_box, "button-press-event",
                    G_CALLBACK(ui_do_drive_popup), GINT_TO_POINTER(i));
            g_signal_connect(event_box, "enter-notify-event",
                    G_CALLBACK(ui_statusbar_cross_cb), &allocated_bars[bar_index]);
            g_signal_connect(event_box, "leave-notify-event",
                    G_CALLBACK(ui_statusbar_cross_cb), &allocated_bars[bar_index]);
            gtk_widget_show_all(event_box);

            if (drive_dual) {
                /* drive:unit and track.sector */
                gtk_widget_show(gtk_grid_get_child_at(GTK_GRID(drive), 0, 1));
                gtk_widget_show(gtk_grid_get_child_at(GTK_GRID(drive), 1, 1));
            } else {
                /* all widgets for the second drive */
                gtk_widget_hide(gtk_grid_get_child_at(GTK_GRID(drive), 0, 1));
                gtk_widget_hide(gtk_grid_get_child_at(GTK_GRID(drive), 1, 1));
                gtk_widget_hide(gtk_grid_get_child_at(GTK_GRID(drive), 2, 1));
            }

            if (tde & 1) {
                /* drive LEDs */
                gtk_widget_show(gtk_grid_get_child_at(GTK_GRID(drive), 2, 0));
                if (drive_dual) {
                    gtk_widget_show(gtk_grid_get_child_at(GTK_GRID(drive), 2, 1));
                }
            } else {
                gtk_widget_hide(gtk_grid_get_child_at(GTK_GRID(drive), 2, 0));
                gtk_widget_hide(gtk_grid_get_child_at(GTK_GRID(drive), 2, 1));
            }

            gtk_grid_attach(GTK_GRID(bar), event_box, column, row, 1, 2);
            ++enabled_drive_index;
        }
        state >>= 1;
        tde >>= 1;
        dual >>= 1;
    }
    gtk_widget_show_all(bar);
}


/** \brief Create a popup menu to attach to a disk drive widget.
 *
 *  \param unit The index of the drive, 0-3 for drives 8-11.
 *
 *  \return The GtkMenu for use as a popup, as a floating reference.
 */
static GtkWidget *ui_drive_menu_create(int unit)
{
    char buf[128];
    GtkWidget *drive_menu = gtk_menu_new();
    GtkWidget *drive_menu_item;
    int drive;

    mainlock_assert_is_not_vice_thread();

    snprintf(buf, sizeof(buf), "Attach disk to drive #%d...", unit + DRIVE_UNIT_MIN);
    buf[sizeof(buf) - 1] = 0;

    drive_menu_item = gtk_menu_item_new_with_label(buf);
    g_signal_connect(drive_menu_item, "activate",
            G_CALLBACK(ui_disk_attach_dialog_show),
            GINT_TO_POINTER(unit + DRIVE_UNIT_MIN));
    gtk_container_add(GTK_CONTAINER(drive_menu), drive_menu_item);


    for (drive = 0; drive < NUM_DRIVES; drive++) {
        snprintf(buf, sizeof(buf), "Detach disk from drive #%d:%d", unit + DRIVE_UNIT_MIN, drive);
        buf[sizeof(buf) - 1] = 0;

        drive_menu_item = gtk_menu_item_new_with_label(buf);
        g_signal_connect(drive_menu_item, "activate",
                G_CALLBACK(ui_disk_detach_callback),
                UNIT_DRIVE_TO_PTR(unit + DRIVE_UNIT_MIN, drive));
        gtk_container_add(GTK_CONTAINER(drive_menu), drive_menu_item);
    }

    /* GTK2/GNOME UI put TDE and Read-only checkboxes here, but that
     * seems excessive or possibly too fine-grained, so skip that for
     * now. Also: make fliplist usable for drive 1. */
    ui_populate_fliplist_menu(drive_menu, unit + DRIVE_UNIT_MIN, 0);
    gtk_container_add(GTK_CONTAINER(drive_menu),
            gtk_separator_menu_item_new());

    drive_menu_item = gtk_menu_item_new_with_label("Configure drives ...");
    g_signal_connect(drive_menu_item, "activate",
            G_CALLBACK(on_drive_configure_activate), NULL);
    gtk_container_add(GTK_CONTAINER(drive_menu), drive_menu_item);

    gtk_widget_show_all(drive_menu);
    return drive_menu;
}


/** \brief  Create volume button for the status bar
 *
 * \return  GtkVolumeButton
 */
static GtkWidget *ui_volume_button_create(void)
{
    GtkWidget *volume;
    int sound_vol = 0;

    volume = gtk_volume_button_new();
    g_object_ref_sink(volume);
    gtk_widget_set_can_focus(volume, FALSE);

    resources_get_int("SoundVolume", &sound_vol);
    gtk_scale_button_set_value(GTK_SCALE_BUTTON(volume),
            (gdouble)sound_vol / 100.0);

    /* FIXME: there's too much padding to the right of the widget in VSID */
    g_object_set(
            volume,
            "use-symbolic", TRUE,
            NULL);

    g_signal_connect(volume, "value-changed",
            G_CALLBACK(on_volume_value_changed), NULL);

    return volume;
}


/*****************************************************************************
 *                              Public functions                             *
 ****************************************************************************/

/** \brief Initialize the status bar subsystem.
 *
 *  \warning This function _must_ be called before any call to
 *           ui_statusbar_create() and _must not_ be called after any
 *           call to it.
 */
void ui_statusbar_init(void)
{
    int i;
    ui_sb_state_t *sb_state;

    /* Most things need initialisation to zero and allocated_bars is
     * static, so not much to do here. */
    for (i = 0; i < MAX_STATUS_BARS; ++i) {
        allocated_bars[i].displayed_tape_counter[0] = -1;
        allocated_bars[i].displayed_tape_counter[1] = -1;
    }


    sb_state = lock_sb_state();
    /* Set an impossible number of joyports to enabled so that the status
     * is guarenteed to be updated. */
    sb_state->joyports_enabled = ~0;
    unlock_sb_state();
}

/** \brief Clean up any resources the statusbar system uses that
 *         weren't cleaned up when the status bars themselves were
 *         destroyed. */
void ui_statusbar_shutdown(void)
{
    mainlock_assert_is_not_vice_thread();
}


/** \brief  Create a new status bar.
 *
 *  This function should be called once as part of creating a new
 *  machine window.
 *
 *  \param[in]  window_identity window identity
 *
 *  \return A new status bar, as a floating reference, or NULL if all
 *          possible status bars have been allocated already.
 */
GtkWidget *ui_statusbar_create(int window_identity)
{
    GtkWidget *sb;
    GtkWidget *speed;
    GtkWidget *joysticks;
    GtkWidget *tape1;
    GtkWidget *tape2;
    GtkWidget *tape1_events;
    GtkWidget *tape2_events;
    GtkWidget *sep;
    GtkWidget *crt = NULL;
    GtkWidget *mixer = NULL;
    GtkWidget *volume = NULL;
    GtkWidget *message;
    GtkWidget *recording;
    GtkWidget *kbd_debug_widget;
    int i;
    int j;

    mainlock_assert_is_not_vice_thread();

    for (i = 0; i < MAX_STATUS_BARS; ++i) {
        if (allocated_bars[i].bar == NULL) {
            break;
        }
    }

    if (i >= MAX_STATUS_BARS) {
        /* Fatal error? */
        return NULL;
    }

    allocated_bars[i].window_identity = window_identity;

    /* While the status bar itself is returned floating, we sink all
     * of its information-bearing subwidgets. This is so that we can
     * remove or add them to the status bar as the configuration
     * demands, while ensuring they remain alive. They receive an
     * extra dereference in ui_statusbar_destroy() so nothing should
     * leak. */
    sb = vice_gtk3_grid_new_spaced(8, 0);
    gtk_widget_set_hexpand(sb, FALSE);
    g_signal_connect(sb, "destroy", G_CALLBACK(destroy_statusbar_cb), NULL);
    allocated_bars[i].bar = sb;

    /* First column: CPU/FPS - No FPS on VDC Window for now */
    speed = statusbar_speed_widget_create(&allocated_bars[i].speed_state);
    g_object_ref_sink(G_OBJECT(speed));
    g_object_set(speed, "margin-left", 8, NULL);

    allocated_bars[i].speed = speed;
    gtk_grid_attach(GTK_GRID(sb), speed, SB_COL_SPEED, 0, 1, 3);

    /* Second column: separator */
    sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_hexpand(sep, FALSE);
    gtk_grid_attach(GTK_GRID(sb),sep,
                    SB_COL_SEP_SPEED, 0, 1, 2);

    /* don't add CRT or Mixer controls when VSID */
    if (machine_class != VICE_MACHINE_VSID) {
        crt = gtk_check_button_new_with_label("CRT controls");
        gtk_widget_set_can_focus(crt, FALSE);
        g_object_ref_sink(G_OBJECT(crt));
        gtk_widget_set_halign(crt, GTK_ALIGN_START);
        gtk_widget_set_hexpand(crt, FALSE);
        gtk_widget_show_all(crt);
        g_signal_connect(crt, "toggled", G_CALLBACK(on_crt_toggled), NULL);

        mixer = gtk_check_button_new_with_label("Mixer controls");
        gtk_widget_set_can_focus(mixer, FALSE);
        g_object_ref_sink(G_OBJECT(mixer));
        gtk_widget_set_halign(mixer, GTK_ALIGN_START);
        gtk_widget_set_hexpand(mixer, FALSE);
        gtk_widget_show_all(mixer);
        g_signal_connect(mixer, "toggled", G_CALLBACK(on_mixer_toggled), NULL);
    }

    /* Messages
     *
     * Moved to a separate, full row, needs testing
     */


    sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_grid_attach(GTK_GRID(sb), sep, 0, SB_ROW_MSG_ROW_SEP, 11, 1);

    message = gtk_label_new(NULL);
    gtk_widget_set_hexpand(message, FALSE);
    gtk_widget_set_halign(message, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(message), PANGO_ELLIPSIZE_END);
    g_object_set(G_OBJECT(message),
                 "margin-left", 8,
                 "margin-right", 8,
                 NULL);
    g_object_ref_sink(message);
    allocated_bars[i].msg = message;
    gtk_grid_attach(GTK_GRID(sb), message, SB_COL_MSG, SB_ROW_MSG, 5, 1);
    /* add horizontal separator */
    gtk_grid_attach(GTK_GRID(sb),
                    gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                    SB_COL_MSG_SEP, SB_ROW_MSG_SEP, 1, 1);

    /* Recording */
    recording = statusbar_recording_widget_create();
    gtk_widget_set_hexpand(recording, TRUE);
    g_object_ref_sink(recording);
    allocated_bars[i].record = recording;
    gtk_grid_attach(GTK_GRID(sb), recording, SB_COL_RECORD, SB_ROW_RECORD, 5, 1);
//    /* add sep */
//    gtk_grid_attach(GTK_GRID(sb), gtk_separator_new(GTK_ORIENTATION_VERTICAL),
//            SB_COL_SEP_RECORD, SB_ROW_SEP_RECORD, 1, 2);


    /* TODO: skip VSID and add another separator after the checkbox */
    if (machine_class != VICE_MACHINE_VSID) {
        allocated_bars[i].crt = crt;
        gtk_grid_attach(GTK_GRID(sb), crt, SB_COL_CRT, SB_ROW_CRT, 1, 1);
        allocated_bars[i].mixer = mixer;
        gtk_grid_attach(GTK_GRID(sb), mixer, SB_COL_MIXER, SB_ROW_MIXER, 1, 1);
        /* add separator */
        gtk_grid_attach(GTK_GRID(sb), gtk_separator_new(GTK_ORIENTATION_VERTICAL),
                SB_COL_SEP_CRT, SB_ROW_SEP_CRT, 1, 3);
    }

    /* No datasette for DTV, SCPU or VSID */
    if ((machine_class != VICE_MACHINE_C64DTV) &&
            (machine_class != VICE_MACHINE_SCPU64) &&
            (machine_class != VICE_MACHINE_VSID)) {
        tape1 = ui_tape_widget_create(TAPEPORT_UNIT_1);
        g_object_ref_sink(G_OBJECT(tape1));
        /* Clicking the tape status is supposed to pop up a window. This
         * requires a way to make sure events are captured by random
         * internal widgets; the GtkEventBox manages that task for us. */
        tape1_events = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(tape1_events), FALSE);
        gtk_container_add(GTK_CONTAINER(tape1_events), tape1);
        gtk_grid_attach(GTK_GRID(sb), tape1_events, SB_COL_TAPE, 0, 1, 1);
        /* add separator */
        gtk_grid_attach(GTK_GRID(sb), gtk_separator_new(GTK_ORIENTATION_VERTICAL),
                SB_COL_SEP_TAPE, SB_ROW_SEP_TAPE, 1, 3);
        allocated_bars[i].tape1 = tape1;
        allocated_bars[i].tape1_menu = ui_create_datasette_control_menu(TAPEPORT_UNIT_1);
        g_object_ref_sink(G_OBJECT(allocated_bars[i].tape1_menu));
        g_signal_connect(tape1_events, "button-press-event",
                G_CALLBACK(ui_do_datasette_popup),
                GINT_TO_POINTER(i | (TAPEPORT_UNIT_1 << 8)));
        g_signal_connect(tape1_events, "enter-notify-event",
                G_CALLBACK(ui_statusbar_cross_cb), &allocated_bars[i]);
        g_signal_connect(tape1_events, "leave-notify-event",
                G_CALLBACK(ui_statusbar_cross_cb), &allocated_bars[i]);
    }

    /* Only the PET has two datasettes */
    if (machine_class == VICE_MACHINE_PET) {
        tape2 = ui_tape_widget_create(TAPEPORT_UNIT_2);
        g_object_ref_sink(G_OBJECT(tape2));
        /* Clicking the tape status is supposed to pop up a window. This
         * requires a way to make sure events are captured by random
         * internal widgets; the GtkEventBox manages that task for us. */
        tape2_events = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(tape2_events), FALSE);
        gtk_container_add(GTK_CONTAINER(tape2_events), tape2);
        gtk_grid_attach(GTK_GRID(sb), tape2_events, SB_COL_TAPE, 1, 1, 1);
        allocated_bars[i].tape2 = tape2;
        allocated_bars[i].tape2_menu = ui_create_datasette_control_menu(TAPEPORT_UNIT_2);
        g_object_ref_sink(G_OBJECT(allocated_bars[i].tape2_menu));
        g_signal_connect(tape2_events, "button-press-event",
                G_CALLBACK(ui_do_datasette_popup),
                GINT_TO_POINTER(i | (TAPEPORT_UNIT_2 << 8)));
        g_signal_connect(tape2_events, "enter-notify-event",
                G_CALLBACK(ui_statusbar_cross_cb), &allocated_bars[i]);
        g_signal_connect(tape2_events, "leave-notify-event",
                G_CALLBACK(ui_statusbar_cross_cb), &allocated_bars[i]);
    }

    if (machine_class != VICE_MACHINE_VSID) {
        joysticks = ui_joystick_widget_create();
        g_object_ref(joysticks);
        gtk_widget_set_halign(joysticks, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(sb), joysticks, SB_COL_TAPE, 2, 1, 1);
        allocated_bars[i].joysticks = joysticks;
    }

    /* Third column on: Drives. */
    for (j = 0; j < NUM_DISK_UNITS; ++j) {
        GtkWidget *drive = ui_drive_widget_create(j);
        GtkWidget *drive_menu = ui_drive_menu_create(j);
        g_object_ref_sink(G_OBJECT(drive));
        g_object_ref_sink(G_OBJECT(drive_menu));
        allocated_bars[i].drives[j] = drive;
        allocated_bars[i].drive_popups[j] = drive_menu;
    }

    /*
     * Add volume control widget
     *
     * FIXME: The widget doesn't show on MacOS/Windows due to the rendering
     *        canvas somehow having z-index priority over the widget. This
     *        works fine on Linux (as far as we know).
     */
#if (!defined(ARCHDEP_OS_WINDOWS)) && (!defined(ARCHDEP_OS_MACOS))
    volume = ui_volume_button_create();
    if (machine_class == VICE_MACHINE_VSID) {
        gtk_grid_attach(GTK_GRID(sb), volume, 4, 0, 1, 3);
    } else {
        /* FIXME: use a larger column-index than should be required, since
         *        the drive widgets will otherwise clash with the volume
         *        widget when using more than 2 drives.
         */
        gtk_grid_attach(GTK_GRID(sb), volume, SB_COL_VOLUME + 2, 0, 1, 3);
    }
    gtk_widget_set_halign(volume, GTK_ALIGN_END);
    gtk_widget_set_hexpand(volume, TRUE);
#else
    /* Windows or MacOS, only create the volume button for VSID */
    if (machine_class == VICE_MACHINE_VSID) {
        volume = ui_volume_button_create();
        gtk_grid_attach(GTK_GRID(sb), volume, 4, 0, 1, 3);
    }
#endif
    allocated_bars[i].volume = volume;

    /*
     * Add keyboard debugging widget
     */
    if (machine_class != VICE_MACHINE_VSID) {
        sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_grid_attach(GTK_GRID(sb), sep, 0, SB_ROW_KBD_ROW_SEP, 11, 1);

        kbd_debug_widget = kbd_debug_widget_create();
        allocated_bars[i].kbd_debug = kbd_debug_widget;
        g_object_ref_sink(kbd_debug_widget);
        gtk_grid_attach(GTK_GRID(sb), kbd_debug_widget, 0, SB_ROW_KBD, 11, 1);
    }

    return sb;
}


/** \brief Statusbar API function to register an elapsed time.
 *
 *  \param current The current time value in seconds
 *  \param total   The maximum time value in seconds
 *
 */
void ui_display_event_time(unsigned int current, unsigned int total)
{
    GtkWidget *widget;

    /* Ok to call from VICE thread */

    widget = allocated_bars[0].record;

    statusbar_recording_widget_set_time(widget, current, total);
}


/** \brief Statusbar API function to display playback status.
 *
 *  \param playback_status  Unknown.
 *  \param version          seems to be the VICE version major.minor during
 *                          playback and `NULL` when playback is done.
 *
 *  \todo This function is not implemented and its API is not
 *        understood.
 *
 * \note    Since the statusbar message display widget has been removed, we
 *          have some space to implement a widget to display information
 *          regarding playback/recording.
 */
void ui_display_playback(int playback_status, char *version)
{
    GtkWidget *widget = allocated_bars[0].record;

    /* Ok to call from VICE thread */

    statusbar_recording_widget_set_event_playback(widget, version);
}

/** \brief  Statusbar API function to display recording status.
 *
 *  \param  recording_status    seems to be bool indicating recording active
 *
 *  \todo   This function is not implemented and its API is not
 *          understood.
 *
 * \note    Since the statusbar message display widget has been removed, we
 *          have some space to implement a widget to display information
 *          regarding playback/recording.
 */
void ui_display_recording(int recording_status)
{
    GtkWidget *widget;

    /* Ok to call from VICE thread */

    widget = allocated_bars[0].record;

    statusbar_recording_widget_set_recording_status(widget, recording_status);
}


/** \brief  Statusbar API function to display a message in the status bar.
 *
 *  \param  text        The text to display.
 *  \param  fade_out    If nonzero, erase the text after five seconds
 *                      unless it has already been replaced.
 */
void ui_display_statustext(const char *text, int fade_out)
{
    mainlock_assert_is_not_vice_thread();

    GtkWidget *widget = allocated_bars[0].msg;


    /* remove any previous timeout source, if present */
    if (timeout_id > 0) {
        /* still an old timeout running, but the message was overwritten,
         * remove */
        g_source_remove(timeout_id);
        timeout_id = 0;
    }

    gtk_label_set_text(GTK_LABEL(widget), text);

    /* set up timeout if requested */
    if (fade_out) {
        timeout_id = g_timeout_add_seconds(
                MESSAGE_TIMEOUT,
                message_timeout_handler,
                widget);
    }
}

/** \brief  Statusbar API function to display current volume
 *
 * This function is a NOP since the volume can be checked and altered via the
 * Mixer Controls via the statusbar.
 *
 * \param[in]   vol     new volume level
 */
void ui_display_volume(int vol)
{
    /* NOP */
}


/** \brief  Statusbar API function to display current joyport inputs.
 *  \param  joyport An array of bytes of size at least
 *                  JOYPORT_MAX_PORTS+1, with data regarding each
 *                  active joyport.
 *  \warning The joyport array is, for all practical purposes,
 *           _1-indexed_. joyport[0] is unused.
 *  \sa ui_sb_state_s::current_joyports Describes the format of the
 *      data encoded in the joyport array. Note that current_joyports
 *      is 0-indexed as is typical for C arrays.
 */

/* FIXME: during the joystick data extension the joyport type has become uint16_t,
   I did not change anything except the parameter of the function
 */
void ui_display_joyport(uint16_t *joyport)
{
    int i;
    ui_sb_state_t *sb_state;

    sb_state = lock_sb_state();

    for (i = 0; i < JOYPORT_MAX_PORTS; ++i) {
        /* Compare the new value to the current one, set the new
         * value, and queue a redraw if and only if there was a
         * change. And yes, the input joystick ports are 1-indexed. I
         * don't know either. */
        if (sb_state->current_joyports[i] != joyport[i+1]) {
            int j;
            sb_state->current_joyports[i] = joyport[i+1];
            for (j = 0; j < MAX_STATUS_BARS; ++j) {
                if (allocated_bars[j].joysticks) {
                    GtkWidget *grid;
                    GtkWidget *widget;

                    grid = gtk_bin_get_child(GTK_BIN(allocated_bars[j].joysticks));
                    widget = gtk_grid_get_child_at(GTK_GRID(grid), i + 1, 0);
                    if (widget) {
                        redraw_widget_on_ui_thread(widget);
                    }
                }
            }
        }
    }

    unlock_sb_state();
}


/** \brief  Statusbar API function to report changes in tape control status.
 *
 * \param[in]   port    tape port index (0 or 1)
 * \param[in]   control The new tape control. See the DATASETTE_CONTROL_*
 *                      constants in datasette.h for legal values of this
 *                      parameter.
 */
void ui_display_tape_control_status(int port, int control)
{
    ui_sb_state_t *sb_state;
    int index = port;

    /* Ok to call from VICE thread */

    sb_state = lock_sb_state();

    if (control != sb_state->tape_control[index]) {
        int i;
        sb_state->tape_control[index] = control;
        for (i = 0; i < MAX_STATUS_BARS; ++i) {
            GtkWidget *grid;

            if (index == 0) {
                grid = allocated_bars[i].tape1;
            } else {
                grid = allocated_bars[i].tape2;
            }

            if (grid != NULL) {
                GtkWidget *widget = gtk_grid_get_child_at(GTK_GRID(grid), 2, 0);

                if (widget != NULL) {
                    redraw_widget_on_ui_thread(widget);
                }
            }
        }
    }

    unlock_sb_state();
}

/** \brief  Statusbar API function to report changes in tape position.
 *
 * \param[in]   port    tape port index (0 or 1)
 * \param[in]   counter the new value of the position counter
 *
 *  \note   Only the last three digits of the counter will be displayed.
 */
void ui_display_tape_counter(int port, int counter)
{
    ui_sb_state_t *sb_state;
    int index = port;

    sb_state = lock_sb_state();
    sb_state->tape_counter[index] = counter;
    unlock_sb_state();
}


/** \brief  Statusbar API function to report changes in the tape motor.
 *
 * \param[in]   port    tape port index (0 or 1)
 * \param[in]   motor   Nonzero if the tape motor is now on.
 */
void ui_display_tape_motor_status(int port, int motor)
{
    ui_sb_state_t *sb_state;
    int index = port;

    /* Ok to call from VICE thread */
    sb_state = lock_sb_state();

    if (motor != sb_state->tape_motor_status[index]) {
        int i;
        sb_state->tape_motor_status[index] = motor;
        for (i = 0; i < MAX_STATUS_BARS; ++i) {
            GtkWidget *grid;

            if (index == 0) {
                grid = allocated_bars[i].tape1;
            } else{
                grid = allocated_bars[i].tape2;
            }
            if (grid != NULL) {
                GtkWidget *widget = gtk_grid_get_child_at(GTK_GRID(grid), 2, 0);

                if (widget != NULL) {
                    redraw_widget_on_ui_thread(widget);
                }
            }
        }
    }

    unlock_sb_state();
}


/** \brief  Statusbar API function to report changes in tape status.
 *  \param  tape_status The new tape status.
 *  \note   This function does nothing and its API is not
 *          understood. Furthermore, no other extant UIs appear to react
 *          to this call.
 */
void ui_set_tape_status(int port, int tape_status)
{
    /* printf("TAPE DRIVE STATUS: %d\n", tape_status); */

    /* Ok to call from VICE thread */
}


/** \brief  Statusbar API function to report mounting or unmounting of a tape
 *          image.
 *
 *  \param  image   The filename of the tape image (if mounted), or the
 *                  empty string or NULL (if unmounting).
 */
void ui_display_tape_current_image(int port, const char *image)
{
#if 0
    char buf[256];

    mainlock_assert_is_not_vice_thread();

    if (image && *image) {
        snprintf(buf, 256, "Attached %s to tape unit", image);
    } else {
        snprintf(buf, 256, "Tape unit is empty");
    }

    buf[255] = 0;
    ui_display_statustext(buf, 1);
#endif
}


/** \brief  Statusbar API function to report changes in drive LED intensity.
 *
 * This function simply updates global state, rendering occurs in ui_update_statusbars().
 *
 *  \param  drive_number    The unit to update (0-3 for drives 8-11)
 *  \param  drive_base      Drive 0 or 1 of dualdrives
 *  \param  led_pwm1        The intensity of the first LED (0=off,
 *                          1000=maximum intensity)
 *  \param  led_pwm2        The intensity of the second LED (0=off,
 *                          1000=maximum intensity)
 *  \todo   Dual drive code doesn't have its separate Error LED.
 *          Its setting is mixed in in the led_status of drive 0.
 *          Also those error LEDs don't flash but are just on
 *          (or on some models, change the green/red LED to red).
 */
void ui_display_drive_led(unsigned int drive_number,
                          unsigned int drive_base,
                          unsigned int led_pwm1,
                          unsigned int led_pwm2)
{
    ui_sb_state_t *sb_state;

    /* Ok to call from VICE thread */

    if (drive_number > NUM_DISK_UNITS - 1) {
        /* TODO: Fatal error? */
        debug_gtk3("Error: illegal drive number %u.", drive_number);
        abort();
    }

    sb_state = lock_sb_state();
    sb_state->current_drive_leds[drive_number][drive_base][0] = led_pwm1;
    sb_state->current_drive_leds[drive_number][drive_base][1] = led_pwm2;
    sb_state->current_drive_leds_updated[drive_number][drive_base][0] = true;
    sb_state->current_drive_leds_updated[drive_number][drive_base][1] = true;
    unlock_sb_state();
}


/** \brief  Statusbar API function to report changes in drive head location.
 *
 * This function simply updates global state, rendering occurs in ui_update_statusbars().
 *
 *  \param  drive_number        The unit to update (0-3 for drives 8-11)
 *  \param  drive_base          Drive 0 or 1 of dualdrives
 *  \param  half_track_number   Twice the value of the head
 *                              location. 18.0 is 36, while 18.5 would be 37.
 *  \param  drive_side          drive side for dual-head drives (0 or 1)
 *
 *  \todo   The statusbar API does not yet support dual-unit disk
 *          drives. The drive_base argument will likely come into play
 *          once it does.
 */
void ui_display_drive_track(unsigned int drive_number,
                            unsigned int drive_base,
                            unsigned int half_track_number,
                            unsigned int drive_side)
{
    ui_sb_state_t *sb_state;
    int doubleside, dualdrive;

    /* Ok to call from VICE thread */

    if (drive_number > NUM_DISK_UNITS - 1) {
        /* TODO: Fatal error? */
        return;
    }

    sb_state = lock_sb_state();
    doubleside = drive_get_num_heads(sb_state->drives_type[drive_number]) == 2 ? 1 : 0;
    dualdrive = drive_check_dual(sb_state->drives_type[drive_number]);

    if (dualdrive) {
        snprintf(
            sb_state->current_drive_unit_str[drive_number][drive_base],
            DRIVE_UNIT_STR_MAX_LEN - 1,
            "%u:%u",
            drive_number + 8,
            drive_base);
    } else {
        snprintf(
            sb_state->current_drive_unit_str[drive_number][drive_base],
            DRIVE_UNIT_STR_MAX_LEN - 1,
            "%u",
            drive_number + 8);
    }
    sb_state->current_drive_unit_str[drive_number][drive_base][DRIVE_UNIT_STR_MAX_LEN - 1] = '\0';
    sb_state->current_drive_unit_str_updated[drive_number][drive_base] = true;

    if (doubleside) {
        snprintf(
            sb_state->current_drive_track_str[drive_number][drive_base],
            DRIVE_TRACK_STR_MAX_LEN - 1,
            " %u:%04.1lf",  /* space instead of 0 padding looks weird with the
                               drive side in front */
            drive_side,
            half_track_number / 2.0);
    } else {
        snprintf(
            sb_state->current_drive_track_str[drive_number][drive_base],
            DRIVE_TRACK_STR_MAX_LEN - 1,
            " %4.1lf",
            half_track_number / 2.0);
    }

    sb_state->current_drive_track_str[drive_number][drive_base][DRIVE_TRACK_STR_MAX_LEN - 1] = '\0';
    sb_state->current_drive_track_str_updated[drive_number][drive_base] = true;

    unlock_sb_state();
}


/** \brief Update information about each drive.
 *
 *  \param state           A bitmask int, where bits 0-3 indicate
 *                         whether or not drives 8-11 respectively are
 *                         being emulated carefully enough to provide
 *                         LED information.
 *  \param drive_led_color An array of size at least NUM_DISK_UNITS that
 *                         provides information about the LEDs on this
 *                         drive. An element of this array will only
 *                         be checked if the corresponding bit in
 *                         state is 1.
 *  \note Before calling this function, the drive configuration
 *        resources (Drive8Type, Drive9Type, etc) should all be set to
 *        the values you wish to display.
 *  \warning If a drive's LEDs are active when its LED values change,
 *           the UI will not reflect the LED type change until the
 *           next time the led's values are updated. This should not
 *           happen under normal circumstances.
 *  \sa compute_drives_enabled_mask() for how this function determines
 *      which drives are truly active
 *  \sa ui_sb_state_s::drive_led_types for the data in each element of
 *      drive_led_color
 */
void ui_enable_drive_status(ui_drive_enable_t state, int *drive_led_color)
{
    int i, enabled;
    ui_sb_state_t *sb_state;

    /* Ok to call from VICE thread */

    sb_state = lock_sb_state();

    /* Update the drive LEDs first, unconditionally. */
    enabled = state;
    for (i = 0; i < NUM_DISK_UNITS; ++i) {
        for (int d = 0; d < 2; d++) {
            if (enabled & 1) {
                sb_state->drive_led_types[i] = drive_led_color[i];
                sb_state->current_drive_leds[i][d][0] = 0;
                sb_state->current_drive_leds[i][d][1] = 0;
            }
            enabled >>= 1;
        }
    }

    /* Determine drive types and determine dual-drive changes */
    sb_state->drives_dual = 0;
    for (i = 0; i < NUM_DISK_UNITS; i++) {
        int curtype;
        bool old_dual;
        bool new_dual;

        if (resources_get_int_sprintf("Drive%dType", &curtype, i + DRIVE_UNIT_MIN) < 0) {
            curtype = 0;
        }
#if 0
        debug_gtk3("Old drive %d type = %d", i + 8, sb_state->drives_type[i]);
        debug_gtk3("New drive %d type = %d", i + 8, curtype);
#endif
        old_dual = (bool)drive_check_dual(sb_state->drives_type[i]);
        new_dual = (bool)drive_check_dual(curtype);
#if 0
        debug_gtk3("Old drive %d dual = %s", i + 8, old_dual ? "true" : "false");
        debug_gtk3("New drive %d dual = %s", i + 8, new_dual ? "true" : "false");
#endif
        if (old_dual != new_dual) {
            sb_state->drives_dual |= 1 << i;
        }

        /* update drive type */
        sb_state->drives_type[i] = curtype;
    }
#if 0
    debug_gtk3("drives_dual = %02x", sb_state->drives_dual);
#endif

    /* Now give enabled its "real" value based on the drive
     * definitions. */
    enabled = compute_drives_enabled_mask();

    /* Now, if necessary, update the status bar layouts. We won't need
     * to do this if the only change was the kind of drives hooked up,
     * instead of the number */
    if ((state != sb_state->drives_tde_enabled)
            || (enabled != sb_state->drives_enabled)
            || (sb_state->drives_dual != 0)) {
        sb_state->drives_enabled = enabled;
        sb_state->drives_tde_enabled = state;
        sb_state->drives_layout_needed = true;
    }

    unlock_sb_state();
}

/** \brief  Statusbar API function to report mounting or unmounting of
 *          a disk image.
 *
 *  \param  unit_number     0-3 to represent disk units at device 8-11.
 *  \param  drive_number    0-1 to represent the drives in a unit
 *  \param  image           The filename of the disk image (if mounted),
 *                          or the empty string or NULL (if unmounting).
 *  \todo This API is insufficient to describe drives with two disk units.
 */
void ui_display_drive_current_image(unsigned int unit_number, unsigned int drive_number, const char *image)
{
#if 0
    char buf[256];

    mainlock_assert_is_not_vice_thread();

    if (image && *image) {
        snprintf(buf, 256, "Attached %s to unit %d", image, unit_number + 8);
    } else {
        snprintf(buf, 256, "Unit %d is empty", unit_number + 8);
    }
    buf[255] = 0;
    ui_display_statustext(buf, 1);
#endif
}


/** \brief  Determine if the CRT controls widget is enabled in \a window
 *
 * \param[in]   window  GtkWindow instance
 *
 * \return  bool
 */
gboolean ui_statusbar_crt_controls_enabled(GtkWidget *window)
{
    GtkWidget *bin;
    GtkWidget *bar;
    GtkWidget *check;
    gboolean active;

    mainlock_assert_is_not_vice_thread();

    bin = gtk_bin_get_child(GTK_BIN(window));
    if (bin != NULL) {
        bar = gtk_grid_get_child_at(GTK_GRID(bin), 0, 2);  /* FIX */
        if (bar != NULL) {
            check = gtk_grid_get_child_at(GTK_GRID(bar), SB_COL_CRT, 0);
            if (check != NULL) {
                active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check));
                return active;
            }
        }
    }
    return FALSE;
}


/** \brief  Determine if the mixer controls widget is enabled in \a window
 *
 * \param[in]   window  GtkWindow instance
 *
 * \return  bool
 */
gboolean ui_statusbar_mixer_controls_enabled(GtkWidget *window)
{
    GtkWidget *bin;
    GtkWidget *bar;
    GtkWidget *check;
    gboolean active;

    mainlock_assert_is_not_vice_thread();

    bin = gtk_bin_get_child(GTK_BIN(window));
    if (bin != NULL) {
        bar = gtk_grid_get_child_at(GTK_GRID(bin), 0, 2);  /* FIX */
        if (bar != NULL) {
            check = gtk_grid_get_child_at(GTK_GRID(bar), SB_COL_CRT, 1);
            if (check != NULL) {
                active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check));
                return active;
            }
        }
    }
    return FALSE;
}

/** \brief  Statusbar API to display emulation metrics and drive status */
void ui_update_statusbars(void)
{
    /* TODO: Don't call this for each top level window as it updates all statusbars */
    GtkWidget *speed_widget, *tape_counter, *drive, *track, *led, *driveunit;
    ui_statusbar_t *bar;
    int i, j;
    ui_sb_state_t *sb_state;
    ui_sb_state_t state_snapshot;

    sb_state = lock_sb_state();

    /* Take a safe copy of the sb_state so we don't hold the lock during display */
    state_snapshot = *sb_state;

    /* Reset any 'updated needed' flags */
    sb_state->drives_layout_needed = false;

    for (j = 0; j < NUM_DISK_UNITS; ++j) {
        sb_state->current_drive_track_str_updated[j][0] = false;
        sb_state->current_drive_track_str_updated[j][1] = false;
        for (int d = 0; d < 2; d++) {
            sb_state->current_drive_leds_updated[j][d][0] = false;
            sb_state->current_drive_leds_updated[j][d][1] = false;
        }
    }

    unlock_sb_state();

    for (i = 0; i < MAX_STATUS_BARS; ++i) {
        bar = &allocated_bars[i];
        if (!bar->bar) {
            continue;
        }

        /*
         * Emulation speed, fps, warp
         */

        speed_widget = bar->speed;
        if (speed_widget != NULL) {
            statusbar_speed_widget_update(speed_widget, &bar->speed_state, bar->window_identity);
        }

        /*
         * Update Tape
         */

        if (bar->tape1 && bar->displayed_tape_counter[0] != state_snapshot.tape_counter[0]) {
            tape_counter = gtk_grid_get_child_at(GTK_GRID(bar->tape1), 1, 0);
            if (tape_counter) {
                char buf[8];
                snprintf(buf, 8, "%03d", state_snapshot.tape_counter[0] % 1000);
                buf[7] = 0;

                gtk_label_set_text(GTK_LABEL(tape_counter), buf);
            }
            bar->displayed_tape_counter[0] = state_snapshot.tape_counter[0];
        }
        if (bar->tape2 && bar->displayed_tape_counter[1] != state_snapshot.tape_counter[1]) {
            tape_counter = gtk_grid_get_child_at(GTK_GRID(bar->tape2), 1, 0);
            if (tape_counter) {
                char buf[8];
                snprintf(buf, 8, "%03d", state_snapshot.tape_counter[1] % 1000);
                buf[7] = 0;

                gtk_label_set_text(GTK_LABEL(tape_counter), buf);
            }
            bar->displayed_tape_counter[1] = state_snapshot.tape_counter[1];
        }

        /*
         * Joystick
         */

        update_joyport_layout(&state_snapshot);

        /*
         * Drive track, half track, and led
         */

        if (state_snapshot.drives_layout_needed) {
            /* WARNING: The current implementation of ui_enable_drive_status()
             * relies on the fact that the drives are the last elements of the
             * statusbar display. If more widgets are added past this point,
             * that function will need to change as well. */
            layout_statusbar_drives(&state_snapshot, i);
        }

        for (j = 0; j < NUM_DISK_UNITS; ++j) {
            drive = bar->drives[j];
            if (!drive) {
                continue;
            }

            /* Only update the label if it has changed .. */
            for (int d = 0; d < 2; d++) {
                if (state_snapshot.current_drive_track_str_updated[j][d]) {
                    track = gtk_grid_get_child_at(GTK_GRID(drive), 1, d);
                    if (track) {
                        gtk_label_set_text(GTK_LABEL(track),
                                           state_snapshot.current_drive_track_str[j][d]);
                    }
                }

                if (state_snapshot.current_drive_unit_str_updated[j][d]) {
                    driveunit = gtk_grid_get_child_at(GTK_GRID(drive), 0, d);
                    if (driveunit) {
                        gtk_label_set_text(GTK_LABEL(driveunit),
                                           state_snapshot.current_drive_unit_str[j][d]);
                    }
                }

                /* Only draw the LEDs if they have changed */
                if (state_snapshot.current_drive_leds_updated[j][d]) {
                    led = gtk_grid_get_child_at(GTK_GRID(drive), 2, d);
                    if (led) {
                        gtk_widget_queue_draw(led);
                    }
                }
            }
        }
    }
}


/** \brief  Show/hide the statusbar kdb debug widget of \a window
 *
 * \param[in,out]   window  GtkWindow instance
 * \param[in]       state   Display state
 *
 * \todo    Replace integer literals
 */
static void kbd_statusbar_widget_enable(GtkWidget *window, gboolean state)
{
    GtkWidget *main_grid;
    GtkWidget *statusbar;
    GtkWidget *kbd;

    mainlock_assert_is_not_vice_thread();

    main_grid = gtk_bin_get_child(GTK_BIN(window));
    if (main_grid != NULL) {
        statusbar = gtk_grid_get_child_at(GTK_GRID(main_grid), 0, 2);
        if (statusbar != NULL) {
            kbd = gtk_grid_get_child_at(GTK_GRID(statusbar), 0, SB_ROW_KBD);
            if (kbd != NULL) {
                if (state) {
                    gtk_widget_show_all(kbd);
                } else {
                    gtk_widget_hide(kbd);
                }
            }
        }
    }
}


/** \brief  Show/hide the keyboard debugging widget on the statusbar
 *
 * \param[in]   state   visible state
 */
void ui_statusbar_set_kbd_debug(gboolean state)
{
    GtkWidget *window;

    mainlock_assert_is_not_vice_thread();

    /* standard VIC/VICII/TED/CRTC window */
    window = ui_get_window_by_index(0);
    kbd_statusbar_widget_enable(window, state);
    /* reduce window size so we don't have weird extra lines */
#if 0
    gtk_window_resize(GTK_WINDOW(window), 1, 1);
#endif

    /* C128: Handle the VDC */
    if (machine_class == VICE_MACHINE_C128) {
        window = ui_get_window_by_index(1); /* VDC */
        kbd_statusbar_widget_enable(window, state);
#if 0
        gtk_window_resize(GTK_WINDOW(window), 1, 1);
#endif
    }
}


/** \brief  Get active 'Recording' widget
 *
 * \return  recording widget
 */
GtkWidget *ui_statusbar_get_recording_widget(void)
{
    int w = ui_get_main_window_index();
    return allocated_bars[w].record;
}
