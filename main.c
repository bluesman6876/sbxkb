/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * main.c
 * Copyright (C) Sergey Tikhonov 2014 <bluesman@bk.ru>
 * Copyright (C) Yrij Tuljakov 2009 <w00zy@yandex.ru>
 * Copyright (C) 2002 Anatoly Asviyan (aka Arsen)
 * Copyright (C) 2000 Peter Zelezny
 * 
 * main.c is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * main.c is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <stdio.h>
#include <X11/XKBlib.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define DEBUG

#define ERROR(fmt, args...) fprintf(stderr, fmt, ## args)

#ifdef DEBUG
#define DBG(fmt, args...) fprintf(stderr, "%s:%-5d: " fmt, __FUNCTION__,\
    __LINE__, ## args)
#else
#define DBG(fmt, args...) do {  } while(0)
#endif

typedef struct _kbd_info {
	gchar *sym;
	gchar *name;
	GdkPixbuf *flag;
} kbd_info;

/* *********************** Xorg root ******************** */
static Atom a_XKB_RULES_NAMES;

static Display *dpy;

static int xkb_event_type;

/* ************ internal state mashine ******************** */
static int cur_group;

static int ngroups;

static GHashTable *sym2pix;

static kbd_info group2info[XkbNumKbdGroups];

static GdkPixbuf *null_flag;

/* ********************************************************** */
static GtkStatusIcon *sb_dock;

static XFocusChangeEvent focused_event;

static const gchar *image_pefix = NULL;

static int default_group = 0;

static GHashTable *stateWindow;

static Window activeWindow = 0;

static Window focus = 0;

static int revet = 0;

static Window winRoot = 0;

static gboolean init_done;

/* ************************ header *************************** */
static int init(void);

static void read_kbd_description(void);

static void update_flag(int no);

static GdkFilterReturn filter(XEvent *xev, GdkEvent *event, gpointer data);

static void Xerror_handler(Display *d, XErrorEvent *ev);

static GdkPixbuf *sym2flag(const char *sym);

static void sb_dock_create(void);

static gboolean my_str_equal(gchar *a, gchar *b)
{
	return a[0] == b[0] && a[1] == b[1];
}

static GdkPixbuf *sym2flag(const char *sym)
{
	GdkPixbuf *flag;
	static GString *s = NULL;
	char tmp[3];

	g_assert(sym != NULL && strlen(sym) > 1);
	flag = g_hash_table_lookup(sym2pix, sym);

	if (flag) {
		return flag;
	}

	if (!s) {
		s = g_string_new(image_pefix);
		g_string_append(s, "TT.png");
	}

	s->str[s->len - 6] = sym[0];
	s->str[s->len - 5] = sym[1];

	flag = gdk_pixbuf_new_from_file_at_scale(s->str, 21, 14, TRUE, NULL);

	if (flag) {
		tmp[0] = sym[0];
		tmp[1] = sym[1];
		tmp[2] = 0;
		g_hash_table_insert(sym2pix, tmp, flag);
	} else {
		flag = null_flag;
	}

	return flag;
}

static gboolean sb_dock_pressed(GtkStatusIcon *icon, GdkEvent *event,
    gpointer data)
{
	int no;

	if (event->type == GDK_BUTTON_PRESS &&
	    ((GdkEventButton *)event)->button == 1) {
		no = (cur_group + 1) % ngroups;
		DBG("no=%d\n", no);
		XkbLockGroup(dpy, XkbUseCoreKbd, no);
		return TRUE;
	} else {
		return FALSE;
	}
}

static void sb_dock_create(void)
{
	sb_dock = gtk_status_icon_new();
	g_signal_connect(G_OBJECT(sb_dock), "button-press-event",
	    G_CALLBACK(sb_dock_pressed), NULL);
}

static void read_kbd_description(void)
{
	XkbDescRec *kbd_desc_ptr;
	XkbStateRec xkb_state;
	Atom sym_name_atom;
	int i;

	/* clean group */
	cur_group = ngroups = 0;

	for (i = 0; i < XkbNumKbdGroups; i++) {
		g_free(group2info[i].sym);
		g_free(group2info[i].name);
	}

	bzero(group2info, sizeof(group2info));
	/* get kbd info */
	kbd_desc_ptr = XkbAllocKeyboard();

	if (!kbd_desc_ptr) {
		ERROR("can't alloc kbd info\n");
		goto out_us;
	}

	kbd_desc_ptr->dpy = dpy;

	if (XkbGetControls(dpy, XkbAllControlsMask, kbd_desc_ptr) !=
	    Success) {
		ERROR("can't get Xkb controls\n");
		goto error_out;
	}

	ngroups = kbd_desc_ptr->ctrls->num_groups;

	if (ngroups < 1) {
		goto error_out;
	}

	if (XkbGetState(dpy, XkbUseCoreKbd, &xkb_state) != Success) {
		ERROR("can't get Xkb state\n");
		goto error_out;
	}

	cur_group = xkb_state.group;
	default_group = xkb_state.group;
	DBG("cur_group = %d ngroups = %d\n", cur_group, ngroups);
	g_assert(cur_group < ngroups);

	if (XkbGetNames(dpy, XkbSymbolsNameMask, kbd_desc_ptr) != Success) {
		ERROR("can't get Xkb symbol description\n");
		goto error_out;
	}

	if (XkbGetNames(dpy, XkbGroupNamesMask, kbd_desc_ptr) != Success) {
		ERROR("Failed to get keyboard description\n");
	}

	g_assert(kbd_desc_ptr->names);
	sym_name_atom = kbd_desc_ptr->names->symbols;
	
	/* parse kbd info */
	if (sym_name_atom != None) {
		char *sym_name, *tmp, *tok;
		int no;

		sym_name = XGetAtomName(dpy, sym_name_atom);

		if (!sym_name) {
			goto error_out;
		}

		DBG("sym_name=%s\n", sym_name);

		for (tok = strtok(sym_name, "+"); tok;
		     tok = strtok(NULL, "+")) {
			DBG("tok=%s\n", tok);
			tmp = strchr(tok, ':');

			if (tmp) {
				if (sscanf(tmp + 1, "%d", &no) != 1) {
					ERROR("can't read kbd number\n");
				}

				no--;
				*tmp = 0;
			} else {
				no = 0;
			}

			for (tmp = tok; isalpha(*tmp); tmp++) {}

			*tmp = 0;
			DBG("map=%s no=%d\n", tok, no);

			if (!strcmp(tok, "pc") || strlen(tok) != 2) {
				continue;
			}

			g_assert(no >= 0 && no < ngroups);

			if (group2info[no].sym != NULL) {
				ERROR("xkb group #%d is already defined\n", no);
			}

			group2info[no].sym = g_strdup(tok);
			group2info[no].flag = sym2flag(tok);
			group2info[no].name = XGetAtomName(dpy,
			    kbd_desc_ptr->names->groups[no]);
		}

		XFree(sym_name);
	}
error_out:
	XkbFreeKeyboard(kbd_desc_ptr, 0, True);
	/* sanity check: group numbering must be continous */
	for (i = 0; i < XkbNumKbdGroups && group2info[i].sym; i++) {}

	if (i != ngroups) {
		ERROR("kbd group numbering is not continous\n");
		ERROR("run 'xlsatoms | grep pc' to know what hapends\n");
		exit(1);
	}
out_us:
	/* no groups were defined just add default 'us' kbd group */
	if (!ngroups) {
		ngroups = 1;
		cur_group = 0;
		group2info[0].sym = g_strdup("us");
		group2info[0].flag = sym2flag("us");
		group2info[0].name = NULL;
		ERROR("no kbd groups defined. adding default 'us' group\n");
	}
}

static void update_flag(int no)
{
	kbd_info *k;

	k = &group2info[no];
	g_assert(k);
	DBG("k->sym=%s\n", k->sym);
	gtk_status_icon_set_from_pixbuf(sb_dock, k->flag);
	gtk_status_icon_set_tooltip_text(sb_dock, k->sym);
}

static void sb_removed_window(int window)
{
	if (g_hash_table_lookup_extended(stateWindow, GINT_TO_POINTER(window),
	    NULL, NULL)) {
		g_hash_table_remove(stateWindow, GINT_TO_POINTER(window));
		DBG("Window %d hash destroy\n", (int)window);
	}
}

static void sb_add_window(int window, int group)
{
	if (g_hash_table_lookup_extended(stateWindow, GINT_TO_POINTER(window),
	    NULL, NULL)) {
		DBG("Replaced window %d hash\n", (int)window);
		g_hash_table_replace(stateWindow, GINT_TO_POINTER(window),
		    GINT_TO_POINTER(group));
	} else {
		DBG("New window: %d\n", window);
		focused_event.window = (Window)window;
		XSelectInput(dpy, (Window)window,
		    FocusChangeMask | StructureNotifyMask);
		XPutBackEvent(dpy, (XEvent *)&focused_event);
		g_hash_table_insert(stateWindow, GINT_TO_POINTER(window),
		    GINT_TO_POINTER(group));
		DBG("Insert window hash %d\n", (int)window);
	}
}

static Window sb_get_focus(void)
{
	XGetInputFocus(dpy, &focus, &revet);
	return focus;
}

static GdkFilterReturn filter(XEvent *xev, GdkEvent *event, gpointer data)
{
	if (!init_done) {
		return GDK_FILTER_CONTINUE;
	}

	if (xev->xany.type == DestroyNotify) {
		DBG("destroy %x\n", xev->xdestroywindow.window);
		sb_removed_window(xev->xdestroywindow.window);
		return GDK_FILTER_REMOVE;
	}

	if (xev->xany.type == CreateNotify) {
		Window create = xev->xcreatewindow.window;
		/*g_return_val_if_fail (create, GDK_FILTER_REMOVE);*/

		if (!create) {
			DBG("!create %x\n", xev->xcreatewindow.window);
			return GDK_FILTER_CONTINUE;
		}

		DBG("Create window %d event\n", (int)create);
		sb_add_window((int)create, default_group);
		return GDK_FILTER_REMOVE;
	}

	if (xev->xany.type == FocusIn) {
		sb_get_focus();
		/*g_return_val_if_fail (focus, GDK_FILTER_REMOVE);*/

		if (!focus) {
			DBG("Assertion focus failed\n");
			return GDK_FILTER_REMOVE;
		}

		if (focus == activeWindow) {
			return GDK_FILTER_REMOVE;
		}

		activeWindow = focus;
		int h_group = GPOINTER_TO_INT(g_hash_table_lookup(stateWindow,
		    GINT_TO_POINTER(activeWindow)));

		if (h_group != cur_group) {
			cur_group = h_group;
			XkbLockGroup(dpy, XkbUseCoreKbd, cur_group);
			update_flag(cur_group);
		}

		DBG("Focus %d, Lawout %d\n", (int)focus, h_group);
		return GDK_FILTER_REMOVE;
	} else if (xev->type == xkb_event_type) {
		XkbEvent *xkbev = (XkbEvent *)xev;
		DBG("XkbTypeEvent %d \n", xkbev->any.xkb_type);

		if (xkbev->any.xkb_type == XkbStateNotify) {
			DBG("XkbStateNotify: %d\n", xkbev->state.group);
			sb_get_focus();
			activeWindow = focus;
			cur_group = xkbev->state.group;

			if (cur_group < ngroups) {
				update_flag(cur_group);
				sb_add_window(focus, cur_group);
			}
		} else if (xkbev->any.xkb_type == XkbNewKeyboardNotify) {
			DBG("XkbNewKeyboardNotify\n");
			read_kbd_description();
			update_flag(cur_group);
		}

		return GDK_FILTER_REMOVE;
	}

	return GDK_FILTER_CONTINUE;
}

static int init(void)
{
	int dummy, retval;

	retval = -1;
	sym2pix = g_hash_table_new(g_str_hash, (GEqualFunc)my_str_equal);
	stateWindow = g_hash_table_new(g_direct_hash, NULL);
	dpy = gdk_x11_get_default_xdisplay();
	winRoot = GDK_ROOT_WINDOW();
	focused_event.type = FocusIn;
	focused_event.display = dpy;
	a_XKB_RULES_NAMES = XInternAtom(dpy, "_XKB_RULES_NAMES", False);

	if (a_XKB_RULES_NAMES == None) {
		ERROR("_XKB_RULES_NAMES - can't get this atom\n");
	} else if (XkbQueryExtension(dpy, &dummy, &xkb_event_type, &dummy,
		   &dummy, &dummy)) {
		DBG("xkb_event_type=%d\n", xkb_event_type);
		XkbSelectEventDetails(dpy, XkbUseCoreKbd, XkbStateNotify,
		    XkbAllStateComponentsMask, XkbGroupStateMask);
		gdk_window_add_filter(NULL, (GdkFilterFunc)filter, NULL);
		null_flag = gdk_pixbuf_new_from_file(PACKAGE_DATA_DIR"/C.png",
		    NULL);
		retval = 0;
	}

	return retval;
}

int main(int argc, char *argv[], char *env[])
{
	int retval;

	setlocale(LC_CTYPE, "");
	gtk_init(&argc, &argv);
	XSetLocaleModifiers("");
	XSetErrorHandler((XErrorHandler)Xerror_handler);

	/* find users flags image. standart GNOME dirs ~/.icons/flags */
	image_pefix = g_build_filename(g_get_home_dir(), ".icons/flags/", NULL);

	if (!g_file_test(image_pefix, G_FILE_TEST_IS_DIR)) {
		image_pefix = PACKAGE_DATA_DIR"/"PACKAGE"/";
	}

	DBG("image pefix = %s\n", image_pefix);
	retval = init();

	if (retval < 0) {
		ERROR("can't init sbxkb. exiting\n");
	} else {
		read_kbd_description();
		sb_dock_create();
		update_flag(cur_group);
		XSelectInput(dpy, (Window)winRoot, FocusChangeMask |
		    SubstructureNotifyMask);
		init_done = TRUE;
		gtk_main();
		retval = 0;
	}

	return retval;
}

/********************************************************************/
void Xerror_handler(Display *d, XErrorEvent *ev)
{
}
