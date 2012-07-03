/*
 * gcal-editable-combo.h
 *
 * Copyright (C) 2012 - Erick Pérez Castellanos
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GCAL_EDITABLE_COMBO_H__
#define __GCAL_EDITABLE_COMBO_H__

#include "gcal-editable.h"

G_BEGIN_DECLS

#define GCAL_TYPE_EDITABLE_COMBO                       (gcal_editable_combo_get_type ())
#define GCAL_EDITABLE_COMBO(obj)                       (G_TYPE_CHECK_INSTANCE_CAST((obj), GCAL_TYPE_EDITABLE_COMBO, GcalEditableCombo))
#define GCAL_EDITABLE_COMBO_CLASS(klass)               (G_TYPE_CHECK_CLASS_CAST((klass), GCAL_TYPE_EDITABLE_COMBO, GcalEditableComboClass))
#define GCAL_IS_EDITABLE_COMBO(obj)                    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GCAL_TYPE_EDITABLE_COMBO))
#define GCAL_IS_EDITABLE_COMBO_CLASS(klass)            (G_TYPE_CHECK_CLASS_TYPE((klass), GCAL_TYPE_EDITABLE_COMBO))
#define GCAL_EDITABLE_COMBO_GET_CLASS(obj)             (G_TYPE_INSTANCE_GET_CLASS((obj), GCAL_TYPE_EDITABLE_COMBO, GcalEditableComboClass))

typedef struct _GcalEditableCombo                       GcalEditableCombo;
typedef struct _GcalEditableComboClass                  GcalEditableComboClass;
typedef struct _GcalEditableComboPrivate                GcalEditableComboPrivate;

struct _GcalEditableCombo
{
  GcalEditable parent;

  /* add your public declarations here */
  GcalEditableComboPrivate *priv;
};

struct _GcalEditableComboClass
{
  GcalEditableClass parent_class;
};

GType          gcal_editable_combo_get_type           (void);

GtkWidget*     gcal_editable_combo_new                (void);

GtkWidget*     gcal_editable_combo_new_with_content   (const gchar       *content);

void           gcal_editable_combo_set_model          (GcalEditableCombo *combo,
                                                       GtkTreeModel      *model);

void           gcal_editable_combo_set_content        (GcalEditableCombo *entry,
                                                       const gchar       *content);
G_END_DECLS

#endif /* __GCAL_EDITABLE_COMBO_H__ */
