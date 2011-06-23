#ifndef MOD_SRC_GUI_H
#define MOD_SRC_GUI_H

#include <gtk/gtk.h>

#include "patch.h"

enum {
    /* LFO inputs */
    FM1, FM2, AM1, AM2
};



GtkWidget*      mod_src_new_combo_with_cell();

/*  mod_src_new_pitch_adjustment
        creates a phat slider button with semitones label within.
*/
GtkWidget*      mod_src_new_pitch_adjustment(void);


/*  mod_src_callback_helper

        for use in the mod src combo box callback, it reads the
        combo box value and sets the patch accordingly.
*/

gboolean        mod_src_callback_helper(int patch_id,
                                        int slot,
                                        GtkComboBox* combo,
                                        PatchParamType par);

gboolean        mod_src_callback_helper_lfo(int patch_id,
                                            int lfo_input,
                                            GtkComboBox* combo,
                                            int lfo_id);

/*  mod_src_combo_get_iter_with_id
        searches the GtkTreeModel combo is using for GtkTreeIter
        with mod_src_id value.
*/
gboolean        mod_src_combo_get_iter_with_id( GtkComboBox*,
                                                int mod_src_id,
                                                GtkTreeIter*);

gboolean        mod_src_combo_set_model(GtkComboBox*, int model_id);

int             mod_src_combo_get_model_id(GtkComboBox*);


#endif