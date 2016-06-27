/*
 This file is part of CurrDiff.
 CurrDiff is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 Subsonic is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 You should have received a copy of the GNU General Public License
 along with Subsonic.  If not, see <http://www.gnu.org/licenses/>.
 Copyright 2016 (C) Viktor Nareiko
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/record.h>
#include <X11/extensions/XTest.h>
#include <X11/XKBlib.h>
#include <libnotify/notify.h>

/************************************************************************
 * Internal data types
 ***********************************************************************/
typedef struct _Key_t {
	KeyCode key;
	struct _Key_t *next;
} Key_t;

typedef struct _KeyMap_t {
	Bool UseKeyCode;
	KeySym ks;
	KeyCode kc;
	Bool used;
	Bool pressed;
	Bool mouse;
	struct timeval down_at;
	struct _KeyMap_t *next;
} KeyMap_t;

typedef struct _XCape_t {
	Display *data_conn;
	Display *ctrl_conn;
	XRecordContext record_ctx;
	pthread_t sigwait_thread;
	sigset_t sigset;
	Bool debug;
	KeyMap_t *map;
} XCape_t;

/************************************************************************
 * Internal function declarations
 ***********************************************************************/
void *sig_handler(void *user_data);

void intercept(XPointer user_data, XRecordInterceptData *data);

KeyMap_t *parse_mapping(Display *ctrl_conn, char *mapping);

Key_t *key_add_key(Key_t *keys, KeyCode key);

/************************************************************************
 * Main function
 ***********************************************************************/
int main(int argc, char **argv) {
	XCape_t *self = malloc(sizeof(XCape_t));
	int dummy, ch;
	static char default_mapping[] = "Num_Lock;Caps_Lock";
	char *mapping = default_mapping;
	self->debug = False;

	while ((ch = getopt(argc, argv, "d")) != -1) {
		switch (ch) {
		case 'd':
			self->debug = True;
			break;
		default:
			fprintf(stdout, "Usage: %s [-d]\n", argv[0]);
			fprintf(stdout, "Runs as a daemon unless -d flag is set\n");
			return EXIT_SUCCESS;
		}
	}

	self->data_conn = XOpenDisplay(NULL);
	self->ctrl_conn = XOpenDisplay(NULL);

	if (!self->data_conn || !self->ctrl_conn) {
		fprintf(stderr, "Unable to connect to X11 display. Is $DISPLAY set?\n");
		exit(EXIT_FAILURE);
	}
	if (!XQueryExtension(self->ctrl_conn, "XTEST", &dummy, &dummy, &dummy)) {
		fprintf(stderr, "Xtst extension missing\n");
		exit(EXIT_FAILURE);
	}
	if (!XRecordQueryVersion(self->ctrl_conn, &dummy, &dummy)) {
		fprintf(stderr, "Failed to obtain xrecord version\n");
		exit(EXIT_FAILURE);
	}
	if (!XkbQueryExtension(self->ctrl_conn, &dummy, &dummy, &dummy, &dummy,
			&dummy)) {
		fprintf(stderr, "Failed to obtain xkb version\n");
		exit(EXIT_FAILURE);
	}

	self->map = parse_mapping(self->ctrl_conn, mapping);

	if (self->map == NULL)
		exit(EXIT_FAILURE);

	if (self->debug != True)
		daemon(0, 0);

	sigemptyset(&self->sigset);
	sigaddset(&self->sigset, SIGINT);
	sigaddset(&self->sigset, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &self->sigset, NULL);

	pthread_create(&self->sigwait_thread,
	NULL, sig_handler, self);

	XRecordRange *rec_range = XRecordAllocRange();
	rec_range->device_events.first = KeyPress;
	rec_range->device_events.last = ButtonRelease;
	XRecordClientSpec client_spec = XRecordAllClients;

	self->record_ctx = XRecordCreateContext(self->ctrl_conn, 0, &client_spec, 1,
			&rec_range, 1);

	if (self->record_ctx == 0) {
		fprintf(stderr, "Failed to create xrecord context\n");
		exit(EXIT_FAILURE);
	}

	XSync(self->ctrl_conn, False);

	if (!XRecordEnableContext(self->data_conn, self->record_ctx, intercept,
			(XPointer) self)) {
		fprintf(stderr, "Failed to enable xrecord context\n");
		exit(EXIT_FAILURE);
	}

	if (!XRecordFreeContext(self->ctrl_conn, self->record_ctx)) {
		fprintf(stderr, "Failed to free xrecord context\n");
	}

	XCloseDisplay(self->ctrl_conn);
	XCloseDisplay(self->data_conn);

	if (self->debug)
		fprintf(stdout, "main exiting\n");

	return EXIT_SUCCESS;
}

/************************************************************************
 * Internal functions
 ***********************************************************************/
void *sig_handler(void *user_data) {
	XCape_t *self = (XCape_t*) user_data;
	int sig;

	if (self->debug)
		fprintf(stdout, "sig_handler running...\n");

	sigwait(&self->sigset, &sig);

	if (self->debug)
		fprintf(stdout, "Caught signal %d!\n", sig);

	if (!XRecordDisableContext(self->ctrl_conn, self->record_ctx)) {
		fprintf(stderr, "Failed to disable xrecord context\n");
		exit(EXIT_FAILURE);
	}

	XSync(self->ctrl_conn, False);

	if (self->debug)
		fprintf(stdout, "sig_handler exiting...\n");

	return NULL;
}

Key_t *key_add_key(Key_t *keys, KeyCode key) {
	Key_t *rval = keys;

	if (keys == NULL) {
		keys = malloc(sizeof(Key_t));
		rval = keys;
	} else {
		while (keys->next != NULL)
			keys = keys->next;
		keys = (keys->next = malloc(sizeof(Key_t)));
	}

	keys->key = key;
	keys->next = NULL;

	return rval;
}

void handle_key(XCape_t *self, KeyMap_t *key,
		Bool mouse_pressed, int key_event) {

	if (key_event == KeyRelease) {
		unsigned n;
		int caps_state = 0;
		int num_state = 0;
		char msg[18] = "";
		char *key_name = XKeysymToString(key->ks);
		XkbGetIndicatorState(self->ctrl_conn, XkbUseCoreKbd, &n);

		if (self->debug)
			fprintf(stdout, "%s key state %d\n", key_name, n);

		caps_state = (n & 0x01) == 1;
		num_state = (n & 0x02) == 2;

		if (strcmp(key_name, "Num_Lock") == 0) {
			if (num_state) {
				strcpy(msg, "Num Lock is on");
			} else {
				strcpy(msg, "Num Lock is off");
			}
		}

		if (strcmp(key_name, "Caps_Lock") == 0) {
			if (caps_state) {
				strcpy(msg, "Caps Lock is on");
			} else {
				strcpy(msg, "Caps Lock is off");
			}
		}

		notify_init("Lock key pressed");
		NotifyNotification * notification = notify_notification_new(
				"Lock key pressed", msg, "dialog-information");
		notify_notification_show(notification, NULL);
		g_object_unref(G_OBJECT(notification));
		notify_uninit();
	}

	XFlush(self->ctrl_conn);
}

void intercept(XPointer user_data, XRecordInterceptData *data) {
	XCape_t *self = (XCape_t*) user_data;
	static Bool mouse_pressed = False;
	KeyMap_t *km;

	if (data->category == XRecordFromServer) {
		int key_event = data->data[0];
		KeyCode key_code = data->data[1];

		if (self->debug)
			fprintf(stdout, "Intercepted key event %d, key code %d\n",
					key_event, key_code);

		for (km = self->map; km != NULL; km = km->next) {
			if ((km->UseKeyCode == False
					&& XkbKeycodeToKeysym(self->ctrl_conn, key_code, 0, 0)
							== km->ks)
					|| (km->UseKeyCode == True && key_code == km->kc)) {
				handle_key(self, km, mouse_pressed, key_event);
			} else if (km->pressed && key_event == KeyPress) {
				km->used = True;
			}
		}

	}
}

KeyMap_t *parse_token(Display *dpy, char *token) {
	KeyMap_t *km = NULL;
	KeySym ks;

	if (token != NULL) {
		km = calloc(1, sizeof(KeyMap_t));

		if ((ks = XStringToKeysym(token)) == NoSymbol) {
			fprintf(stderr, "Invalid key: %s\n", token);
			return NULL;
		}

		km->UseKeyCode = False;
		km->ks = ks;
	} else
		fprintf(stderr, "WARNING: Mapping without token has no effect: '%s'\n",
				token);

	return km;
}

KeyMap_t *parse_mapping(Display *ctrl_conn, char *mapping) {
	char *token;
	KeyMap_t *rval, *km, *nkm;

	rval = km = NULL;

	for (;;) {
		token = strsep(&mapping, ";");
		if (token == NULL)
			break;

		nkm = parse_token(ctrl_conn, token);

		if (nkm != NULL) {
			if (km == NULL)
				rval = km = nkm;
			else {
				km->next = nkm;
				km = nkm;
			}
		}
	}

	return rval;
}
