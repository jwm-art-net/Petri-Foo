/*  Petri-Foo is a fork of the Specimen audio sampler.

    Original Specimen author Pete Bessman
    Copyright 2005 Pete Bessman
    Copyright 2011 James W. Morris

    This file is part of Petri-Foo.

    Petri-Foo is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as
    published by the Free Software Foundation.

    Petri-Foo is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Petri-Foo.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "session.h"

#include <assert.h>
#include <getopt.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bank-ops.h"
#include "config.h"
#include "dish_file.h"
#include "driver.h"
#include "gui.h"
#include "instance.h"
#include "jackdriver.h"
#include "midi.h"
#include "msg_log.h"
#include "patch_util.h"
#include "petri-foo.h"
#include "nsm.h"


const char*  session_names[] = { "None", "JACK", "NSM", "OPEN", "CLOSED" };

typedef struct _pf_session
{
    int     type;
    int     state;
    char*   path;
    char*   bank;

    nsm_client_t*   nsm_client;
    int             nsm_state;
    char*           nsm_client_id;

} pf_session;


static pf_session* _session = 0;


#if HAVE_JACK_SESSION_H
/* JACK Session Management */
#include <jack/session.h>
static void     session_jack_cb(jack_session_event_t *event, void *arg);
static gboolean gui_jack_session_cb(void*);
#endif


/* Non Session Management */
static int  session_nsm_open_cb(const char* name, const char* display_name,
                                const char* client_id, char** out_msg,
                                void* userdata);

static int  session_nsm_save_cb(char** out_msg, void* userdata);

static gboolean session_poll_nsm_events(gpointer data)
{
    nsm_check_wait((nsm_client_t*)data, 0);
    return TRUE;
}

/*
static void msg_log_to_nsm(const char* msg, int msg_base_type)
{
    nsm_send_message(_session->nsm_client, 3, msg);
}
*/


void session_idle_add_event_poll(void)
{
    debug("session_type:'%s'\n", session_names[_session->type]);

    if (_session->type == SESSION_TYPE_NSM)
    {
        debug("Adding NSM event poll\n");
        g_idle_add(session_poll_nsm_events, _session->nsm_client);
    }
}


static char* bankname(const char* path)
{
    char* filename;
    const char* bank = "/bank.petri-foo";
    size_t len = strlen(path);

    if (path[len - 1] == '/')
        ++bank;

    len+= strlen(bank);
    filename = malloc(len + 1);
    snprintf(filename, len + 1, "%s%s", path, bank);
    return filename;
}


int session_init(int argc, char* argv[])
{
    assert(_session == 0);
    pf_session* s;
    gboolean possibly_autoconnect = FALSE;
    const char* nsm_url;
    int opt = 0;
    int opt_ix = 0;
    extern int optind;

    static struct option opts[] =
    {
        { "autoconnect",    0, 0, 'a'},
        { "jack-name",      1, 0, 'j'},
        { "unconnected",    0, 0, 'u'},
        { "uuid",           1, 0, 'U'},
        { 0, 0, 0, 0}
    };

    debug("processing command line options...\n");
    nsm_url = getenv( "NSM_URL" );

    if (!(_session = malloc(sizeof(*_session))))
    {
        msg_log(MSG_ERROR, "Failed to create session support data\n");
        return -1;
    }

    s = _session;
    s->type = SESSION_TYPE_NONE;
    s->state = SESSION_STATE_CLOSED;
    s->path = 0;
    s->bank = 0;

    while((opt = getopt_long(argc, argv, "aj:uU:", opts, &opt_ix)) > 0)
    {
        switch (opt)
        {
        case 'a':
            if (!nsm_url) possibly_autoconnect = TRUE;
            else msg_log(MSG_WARNING,
                        "Ignoring --autoconnect option\n");
            break;

        case 'j':
            if (!nsm_url)
            {
                set_instance_name(optarg);
                msg_log(MSG_MESSAGE, "Setting JACK client name to '%s'\n",
                                    optarg);
            }
            else msg_log(MSG_WARNING, "Ignoring --jack-name option\n");
            break;

        case 'u':
            if (!nsm_url)
            {
                msg_log(MSG_WARNING, "--unconnected option is "
                                         "deprecated for future removal\n");
            }
            else msg_log(MSG_WARNING,
                        "Ignoring --unconnected option\n");
            break;

        case 'U':
            if (!nsm_url)
            {
                jackdriver_set_uuid(strdup(optarg));
                msg_log(MSG_MESSAGE, "Setting JACK session UUID to '%s'\n",
                                    optarg);
                s->type = SESSION_TYPE_JACK;
            }
            else msg_log(MSG_WARNING, "Ignoring --uuid option\n");
            break;

        default:
            msg_log(MSG_WARNING, "Ignoring unknown option '--%s'\n",
                                                        opts[opt_ix].name);
        }
    }

    debug("Initializing session support data\n");

    s->nsm_client = 0;
    s->nsm_state = SESSION_STATE_CLOSED;
    s->nsm_client_id = 0;

    if (nsm_url)
    {
        int timeout = 50;
        debug("NSM_URL:'%s'\n", nsm_url);

        if (!(s->nsm_client = nsm_new()))
        {
            debug("Failed to create NSM client\n");
            return 0;
        }

        nsm_set_open_callback(s->nsm_client, session_nsm_open_cb, s);
        nsm_set_save_callback(s->nsm_client, session_nsm_save_cb, s);

        if (nsm_init(s->nsm_client, nsm_url) != 0)
        {
            debug("Failed to initialize NSM\n");
            nsm_free(s->nsm_client);
            s->nsm_client = 0;
            return -1;
        }

        debug("NSM initialized sending announce and waiting for open..\n");

        nsm_send_announce(  s->nsm_client,
                            "Petri-Foo",
                            ":switch:", /*message:",*/
                            "petri-foo");

        s->type = SESSION_TYPE_NSM;

        /* wait for NSM open message */
        while(s->nsm_state == SESSION_STATE_CLOSED && timeout --> 0)
        {
            nsm_check_wait(s->nsm_client, 10);
        }

        /*msg_log_set_message_cb(msg_log_to_nsm);*/
    }

    if (s->type == SESSION_TYPE_NONE)
    {
        msg_log(MSG_MESSAGE, "Session management not running\n");
    }
    else
    {
        msg_log(MSG_MESSAGE, "Session mangement running under %s\n",
                                    session_names[s->type]);
    }

    if (possibly_autoconnect && s->type == SESSION_TYPE_NONE)
        jackdriver_set_autoconnect(true);

    if (!nsm_url)
    {
        jackdriver_set_session_cb(session_jack_cb);
    }

    driver_start();
    midi_start();

    if (nsm_url)
    {
        if (optind < argc)
            msg_log(MSG_WARNING, "Ignoring bank file option\n");

        dish_file_read(s->bank);
    }
    else
    {
        if (optind < argc)
        {
            dish_file_read(argv[optind]);
            bank_ops_force_name(argv[optind]);
        }
        else
            patch_create_default();
    }

    session_idle_add_event_poll();

    return 0;
}


void session_cleanup(void)
{
    if (_session)
    {
        debug("cleaning up session data\n");

        free(_session->path);
        free(_session->bank);
        free(_session->nsm_client_id);

        if (_session->nsm_client)
            nsm_free(_session->nsm_client);

        free(_session);
        _session = 0;
    }
}


int session_get_type(void)
{
    return _session->type;
}


const char* session_get_path(void)
{
    return _session->path;
}

const char* session_get_bank(void)
{
    return _session->bank;
}


/* Non Session Management */
int session_nsm_open_cb(const char* name, const char* display_name,
                        const char* client_id, char** out_msg,
                        void* userdata)
{
    (void)display_name;(void)out_msg;
    struct stat st;
    pf_session* s = (pf_session*)userdata;
    gboolean session_switch = FALSE;

    debug("NSM Open\n");
    debug("name:'%s'\ndisplay_name:'%s'\nclient_id:'%s'\n",
        name, display_name, client_id);
    debug("session callback data:%p\n", s);

    if (s->nsm_client_id)
    {
        debug("Detected session switch\n");
        /* the switch */
        free(s->path);
        free(s->bank);
        free(s->nsm_client_id);
        s->path = 0;
        s->bank = 0;
        s->nsm_client_id = 0;
        session_switch = TRUE;
    }

    s->nsm_state = SESSION_STATE_OPEN;
    s->nsm_client_id = strdup(client_id);
    set_instance_name(client_id);
    s->path = strdup(name);
    s->bank = bankname(s->path);

    /*  The best we can do here is make the directory if it
        does not exist. We cannot load a bank yet because
        samples might need resampling to JACK's sample rate and
        as we're not yet registered with JACK we don't know what
        rate to resample to.
     */
    if (stat(s->path, &st) != 0)
    {
        mkdir(s->path, 0777);
    }

    if (session_switch)
    {
        debug("re-initializing driver, and reading '%s'\n", s->bank);
        driver_start();
        patch_destroy_all();
        dish_file_read(s->bank);
        bank_ops_force_name(s->bank);
        gui_refresh();
    }

    return ERR_OK;
}


int session_nsm_save_cb(char** out_msg, void* userdata)
{
    (void)out_msg;
    pf_session* s = (pf_session*)userdata;
    debug("NSM Save\n");
    dish_file_write(s->bank);
    return ERR_OK;
}


#if HAVE_JACK_SESSION_H

/*  this is the actual callback which JACK Session calls */
void session_jack_cb(jack_session_event_t *event, void *arg )
{
    (void)arg;
    /*  use g_idle_add to save state from within main thread
        rather than JACK session thread here.
     */
    g_idle_add(gui_jack_session_cb, event);
}


static gboolean gui_jack_session_cb(void *data)
{
    char command_buf[8192];
    const char* instancename = get_instance_name();
    jack_session_event_t *ev = (jack_session_event_t *)data;
    pf_session* s = _session;

    if (s->type != SESSION_TYPE_JACK
     && s->type != SESSION_TYPE_NONE)
    {
        msg_log(MSG_ERROR,  "Already under %s session management, "
                            "refusing simultaneous management by %s\n",
                            session_names[s->type],
                            session_names[SESSION_TYPE_JACK]);
        return FALSE;
    }

    s->type = SESSION_TYPE_JACK;

    /*  FIXME: tell the GUI to switch to session support mode
        (that is, remove particular menu items/functionality).
     */

    s->bank = bankname(ev->session_dir);

    if (instancename)
        snprintf(   command_buf, sizeof(command_buf),
                    "petri-foo --jack-name %s --unconnected -U %s %s",
                    instancename, ev->client_uuid, s->bank);
    else
        snprintf(   command_buf, sizeof(command_buf),
                    "petri-foo --unconnected -U %s %s",
                    ev->client_uuid, s->bank);

    debug("command:%s\n", command_buf);

    dish_file_write(s->bank);

    ev->command_line = strdup(command_buf);
    jack_session_reply(jackdriver_get_client(), ev);

    if (ev->type == JackSessionSaveAndQuit)
         gtk_main_quit();

    jack_session_event_free(ev);

    gui_set_session_mode();

    return FALSE;
}


#endif