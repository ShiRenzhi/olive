/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2019  Olive Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "preferencesdialog.h"

#include <QMenuBar>
#include <QAction>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QGroupBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QDialogButtonBox>
#include <QTreeWidget>
#include <QVector>
#include <QPushButton>
#include <QTreeWidgetItem>
#include <QList>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QAudioDeviceInfo>
#include <QApplication>
#include <QProcess>
#include <QDebug>

#include "global/global.h"
#include "global/config.h"
#include "global/path.h"
#include "rendering/audio.h"
#include "rendering/pixelformats.h"
#include "panels/panels.h"
#include "ui/columnedgridlayout.h"
#include "ui/mainwindow.h"
#include "dialogs/newsequencedialog.h"

KeySequenceEditor::KeySequenceEditor(QWidget* parent, QAction* a)
  : QKeySequenceEdit(parent), action(a) {
  setKeySequence(action->shortcut());
}

void KeySequenceEditor::set_action_shortcut() {
  action->setShortcut(keySequence());
}

void KeySequenceEditor::reset_to_default() {
  setKeySequence(action->property("default").toString());
}

QString KeySequenceEditor::action_name() {
  return action->property("id").toString();
}

QString KeySequenceEditor::export_shortcut() {
  QString ks = keySequence().toString();
  if (ks != action->property("default")) {
    return action->property("id").toString() + "\t" + ks;
  }
  return nullptr;
}

PreferencesDialog::PreferencesDialog(QWidget *parent) :
  QDialog(parent)
{
  setWindowTitle(tr("Preferences"));

  setup_ui();

  setup_kbd_shortcuts(olive::MainWindow->menuBar());

  // set up default sequence
  default_sequence.name = tr("Default Sequence");
  default_sequence.width = olive::config.default_sequence_width;
  default_sequence.height = olive::config.default_sequence_height;
  default_sequence.frame_rate = olive::config.default_sequence_framerate;
  default_sequence.audio_frequency = olive::config.default_sequence_audio_frequency;
  default_sequence.audio_layout = olive::config.default_sequence_audio_channel_layout;
}

void PreferencesDialog::setup_kbd_shortcut_worker(QMenu* menu, QTreeWidgetItem* parent) {
  QList<QAction*> actions = menu->actions();
  for (int i=0;i<actions.size();i++) {
    QAction* a = actions.at(i);

    if (!a->isSeparator() && a->property("keyignore").isNull()) {
      QTreeWidgetItem* item = new QTreeWidgetItem(parent);
      item->setText(0, a->text().replace("&", ""));

      parent->addChild(item);

      if (a->menu() != nullptr) {
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        setup_kbd_shortcut_worker(a->menu(), item);
      } else {
        key_shortcut_items.append(item);
        key_shortcut_actions.append(a);
      }
    }
  }
}

void PreferencesDialog::delete_previews(PreviewDeleteTypes type) {
  char delete_char = 0;

  switch (type) {
  case DELETE_WAVEFORMS:
    delete_char = 'w';
    break;
  case DELETE_THUMBNAILS:
    delete_char = 't';
    break;
  case DELETE_BOTH:
    delete_char = 1;
    break;
  case DELETE_NONE:
    break;
  }

  if (delete_char != 't' && delete_char != 'w' && delete_char != 1) return;

  QDir preview_path(get_data_path() + "/previews");

  if (delete_char == 1) {
    // indiscriminately delete everything
    preview_path.removeRecursively();
  } else {
    QStringList preview_file_list = preview_path.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (int i=0;i<preview_file_list.size();i++) {

      const QString& preview_file_str = preview_file_list.at(i);

      // use filename to determine whether this is a thumbnail or a waveform
      int identifier_char_index = qMax(0, preview_file_str.size()-2);

      // find identifier char
      while (identifier_char_index >= 0
             && preview_file_str.at(identifier_char_index) >= 48
             && preview_file_str.at(identifier_char_index) <= 57) {
        identifier_char_index--;
      }

      // thumbnails will have a 't' towards the end of the filenames, waveforms will have a 'w'
      // if they match the type of preview we're deleting, remove them
      if (preview_file_str.at(identifier_char_index) == delete_char) {
        QFile::remove(preview_path.filePath(preview_file_str));
      }
    }
  }
}

void PreferencesDialog::populate_ocio_menus(OCIO::ConstConfigRcPtr config)
{
  if (!config) {

    // Just clear everything
    ocio_display->clear();
    ocio_default_input->clear();
    ocio_view->clear();
    ocio_look->clear();

  } else {

    // Get input color spaces for setting the default input color space
    ocio_default_input->clear();
    for (int i=0;i<config->getNumColorSpaces();i++) {
      QString colorspace = config->getColorSpaceNameByIndex(i);

      ocio_default_input->addItem(colorspace);

      if (colorspace == olive::config.ocio_default_input_colorspace) {
        ocio_default_input->setCurrentIndex(i);
      }
    }

    // Get current display name (if the config is empty, get the current default display)
    QString current_display = olive::config.ocio_display;
    if (current_display.isEmpty()) {
      current_display = config->getDefaultDisplay();
    }

    // Populate the display menu
    ocio_display->clear();
    for (int i=0;i<config->getNumDisplays();i++) {
      ocio_display->addItem(config->getDisplay(i));

      // Check if this index is the currently selected
      if (config->getDisplay(i) == current_display) {
        ocio_display->setCurrentIndex(i);
      }
    }

    update_ocio_view_menu(config);

    // Populate the look menu
    ocio_look->clear();
    ocio_look->addItem(tr("(None)"), QString());
    for (int i=0;i<config->getNumLooks();i++) {
      const char* look = config->getLookNameByIndex(i);

      ocio_look->addItem(look, look);

      if (look == olive::config.ocio_look) {
        ocio_look->setCurrentIndex(i+1);
      }
    }

  }
}

OCIO::ConstConfigRcPtr PreferencesDialog::TestOCIOConfig(const QString &url)
{
  // Check whether OCIO can load it
  OCIO::ConstConfigRcPtr config;
  try {
    config = OCIO::Config::CreateFromFile(url.toUtf8());
  } catch (OCIO::Exception& e) {
    QMessageBox::critical(this,
                          tr("OpenColorIO Config Error"),
                          tr("Failed to set OpenColorIO configuration: %1").arg(e.what()),
                          QMessageBox::Ok);
  }
  return config;
}

void PreferencesDialog::update_ocio_view_menu(OCIO::ConstConfigRcPtr config)
{

  // Get views for the current display set in `ocio_display`
  QString display = ocio_display->currentText();

  // Get current view
  QString current_view = olive::config.ocio_view;
  if (current_view.isEmpty()) {
    current_view = config->getDefaultView(display.toUtf8());
  }

  // Populate the view menu
  int ocio_view_count = config->getNumViews(display.toUtf8());
  ocio_view->clear();
  for (int i=0;i<ocio_view_count;i++) {
    const char* view = config->getView(display.toUtf8(), i);

    ocio_view->addItem(view);

    if (current_view == view) {
      ocio_view->setCurrentIndex(i);
    }
  }
}

void PreferencesDialog::update_ocio_config(const QString &s)
{
  OCIO::ConstConfigRcPtr file_config;

  if (!s.isEmpty() && QFileInfo::exists(s)) {
    file_config = TestOCIOConfig(s);
  }

  populate_ocio_menus(file_config);
}

void PreferencesDialog::AddBoolPair(QCheckBox *ui, bool *value, bool restart_required)
{
  bool_ui.append(ui);
  bool_value.append(value);
  bool_restart_required.append(restart_required);

  ui->setChecked(*value);
}

void PreferencesDialog::setup_kbd_shortcuts(QMenuBar* menubar) {
  QList<QAction*> menus = menubar->actions();

  for (int i=0;i<menus.size();i++) {
    QMenu* menu = menus.at(i)->menu();

    QTreeWidgetItem* item = new QTreeWidgetItem(keyboard_tree);
    item->setText(0, menu->title().replace("&", ""));

    keyboard_tree->addTopLevelItem(item);

    setup_kbd_shortcut_worker(menu, item);
  }

  for (int i=0;i<key_shortcut_items.size();i++) {
    if (!key_shortcut_actions.at(i)->property("id").isNull()) {
      KeySequenceEditor* editor = new KeySequenceEditor(keyboard_tree, key_shortcut_actions.at(i));
      keyboard_tree->setItemWidget(key_shortcut_items.at(i), 1, editor);
      key_shortcut_fields.append(editor);
    }
  }
}

void PreferencesDialog::accept() {
  bool restart_after_saving = false;
  bool reinit_audio = false;
  bool reload_language = false;
  bool reload_effects = false;
  bool reset_ocio_shaders = false;
  bool reset_render_threads = false;

  // Validate whether the specified CSS file exists
  if (!custom_css_fn->text().isEmpty() && !QFileInfo::exists(custom_css_fn->text())) {
    QMessageBox::critical(
          this,
          tr("Invalid CSS File"),
          tr("CSS file '%1' does not exist.").arg(custom_css_fn->text())
          );
    return;
  }

  // Validate whether the chosen OCIO configuration file
  if (enable_color_management->isChecked()) {

    // Check whether the file exists
    if (!QFileInfo::exists(ocio_config_file->text())) {

      QString msg_title = tr("Invalid OpenColorIO Configuration File");
      QString msg_body;

      if (ocio_config_file->text().isEmpty()) {
        msg_body = tr("You must specify an OpenColorIO configuration file if color management is enabled.");
      } else {
        msg_body = tr("OpenColorIO configuration file '%1' does not exist.").arg(ocio_config_file->text());
      }

      QMessageBox::critical(
            this,
            msg_title,
            msg_body
            );
      return;

    } else if (olive::config.ocio_config_path != ocio_config_file->text()) {

      // Check whether OCIO can load it
      OCIO::ConstConfigRcPtr file_config = TestOCIOConfig(ocio_config_file->text().toUtf8());

      if (!file_config) {
        return;
      }

    }
  }

  // Validate whether one of the bool options requires a restart
  bool bool_requires_restart = false;
  for (int i=0;i<bool_restart_required.size();i++) {
    if (bool_restart_required.at(i)
        && bool_ui.at(i)->isChecked() != *bool_value.at(i)) {
      bool_requires_restart = true;
      break;
    }
  }

  // Check if any settings will require a restart of Olive (including the bool options determined above)
  if (bool_requires_restart
      || olive::config.thumbnail_resolution != thumbnail_res_spinbox->value()
      || olive::config.waveform_resolution != waveform_res_spinbox->value()
      || olive::config.css_path != custom_css_fn->text()
      || olive::config.style != static_cast<olive::styling::Style>(ui_style->currentData().toInt())) {

    // any changes to these settings will require a restart - ask the user if we should do one now or later

    int ret = QMessageBox::question(this,
                                    "Restart Required",
                                    "Some of the changed settings will require a restart of Olive. Would you like "
                                    "to restart now?",
                                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (ret == QMessageBox::Cancel) {
      // Return to Preferences dialog without saving any settings
      return;
    } else if (ret == QMessageBox::Yes) {

      // Check if we can close the current project. If not, we'll treat it as if the user clicked "Cancel".
      if (olive::Global->can_close_project()) {
        restart_after_saving = true;
      } else {
        return;
      }
    }
    // Selecting "No" will save the settings and not restart. They will become active next time Olive opens.

  }


  // Everything checks out, start saving settings from the UI to the backend
  olive::config.css_path = custom_css_fn->text();
  olive::config.recording_mode = recordingComboBox->currentIndex() + 1;
  olive::config.img_seq_formats = imgSeqFormatEdit->text();
  olive::config.upcoming_queue_size = upcoming_queue_spinbox->value();
  olive::config.upcoming_queue_type = upcoming_queue_type->currentIndex();
  olive::config.previous_queue_size = previous_queue_spinbox->value();
  olive::config.previous_queue_type = previous_queue_type->currentIndex();

  // Audio settings may require the audio device to be re-initiated.
  if (olive::config.preferred_audio_output != audio_output_devices->currentData().toString()
      || olive::config.preferred_audio_input != audio_input_devices->currentData().toString()
      || olive::config.audio_rate != audio_sample_rate->currentData().toInt()) {
    reinit_audio = true;
  }
  olive::config.preferred_audio_output = audio_output_devices->currentData().toString();
  olive::config.preferred_audio_input = audio_input_devices->currentData().toString();
  olive::config.audio_rate = audio_sample_rate->currentData().toInt();

  olive::config.effect_textbox_lines = effect_textbox_lines_field->value();

  // see if the language file should be reloaded (not necessary if the app is restarting anyway)
  if (!restart_after_saving
      && olive::config.language_file != language_combobox->currentData().toString()) {
    reload_language = true;
  }
  olive::config.language_file = language_combobox->currentData().toString();

  // Check whether OCIO settings will require a reset of the render threads
  if (olive::config.playback_bit_depth != playback_bit_depth->currentIndex()
      || olive::config.export_bit_depth != export_bit_depth->currentIndex()) {
    reset_render_threads = true;
  }
  if (olive::config.ocio_config_path != ocio_config_file->text()
      || olive::config.ocio_display != ocio_display->currentText()
      || olive::config.ocio_view != ocio_view->currentText()
      || olive::config.ocio_look != ocio_look->currentData().toString()) {
    reset_ocio_shaders = true;
  }
  if (olive::config.ocio_config_path != ocio_config_file->text()) {
    OCIO::SetCurrentConfig(OCIO::Config::CreateFromFile(ocio_config_file->text().toUtf8()));
    olive::config.ocio_config_path = ocio_config_file->text();
  }
  olive::config.enable_color_management = enable_color_management->isChecked();
  olive::config.playback_bit_depth = playback_bit_depth->currentIndex();
  olive::config.export_bit_depth = export_bit_depth->currentIndex();
  olive::config.ocio_display = ocio_display->currentText();
  olive::config.ocio_default_input_colorspace = ocio_default_input->currentText();
  olive::config.ocio_view = ocio_view->currentText();

  // We use data here instead of text because there's a "(None)" option with an empty string
  olive::config.ocio_look = ocio_look->currentData().toString();


  // Set default sequence options
  olive::config.default_sequence_width = default_sequence.width;
  olive::config.default_sequence_height = default_sequence.height;
  olive::config.default_sequence_framerate = default_sequence.frame_rate;
  olive::config.default_sequence_audio_frequency = default_sequence.audio_frequency;
  olive::config.default_sequence_audio_channel_layout = default_sequence.audio_layout;

  // Set all bool options
  for (int i=0;i<bool_ui.size();i++) {
    *bool_value[i] = bool_ui.at(i)->isChecked();
  }

  // Set new style
  olive::config.style = static_cast<olive::styling::Style>(ui_style->currentData().toInt());

  // Check if the thumbnail or waveform icon fields have changed, we may need to recreate the previews if so
  if (olive::config.thumbnail_resolution != thumbnail_res_spinbox->value()
      || olive::config.waveform_resolution != waveform_res_spinbox->value()) {
    // we're changing the size of thumbnails and waveforms, so let's delete them and regenerate them next start

    // delete nothing
    PreviewDeleteTypes delete_type = DELETE_NONE;

    if (olive::config.thumbnail_resolution != thumbnail_res_spinbox->value()) {
      // delete existing thumbnails
      olive::config.thumbnail_resolution = thumbnail_res_spinbox->value();

      // delete only thumbnails
      delete_type = DELETE_THUMBNAILS;
    }

    if (olive::config.waveform_resolution != waveform_res_spinbox->value()) {
      // delete existing waveforms
      olive::config.waveform_resolution = waveform_res_spinbox->value();

      // if we're already deleting thumbnails
      if (delete_type == DELETE_THUMBNAILS) {
        // delete all
        delete_type = DELETE_BOTH;
      } else {
        // just delete waveforms
        delete_type = DELETE_WAVEFORMS;
      }
    }

    delete_previews(delete_type);
  }

  // Save keyboard shortcuts
  for (int i=0;i<key_shortcut_fields.size();i++) {
    key_shortcut_fields.at(i)->set_action_shortcut();
  }

  QDialog::accept();

  if (restart_after_saving) {

    // since we already ran can_close_project(), bypass checking again by running set_modified(false)
    olive::Global->set_modified(false);

    olive::MainWindow->close();

    QProcess::startDetached(QApplication::applicationFilePath(), { olive::ActiveProjectFilename });
  } else {

    // Audio settings may require the audio device to be re-initiated.
    if (reinit_audio) {
      init_audio();
    }

    if (reload_effects) {
      panel_effect_controls->Reload();
    }

    // reload language file if it changed
    if (reload_language) {
      olive::Global->load_translation_from_config();
    }

    if (reset_render_threads) {
      if (panel_footage_viewer->seq != nullptr) {
        panel_footage_viewer->seq->Close();
      }
      panel_footage_viewer->viewer_widget()->get_renderer()->delete_ctx();
      if (panel_sequence_viewer->seq != nullptr) {
        panel_sequence_viewer->seq->Close();
      }
      panel_sequence_viewer->viewer_widget()->get_renderer()->delete_ctx();
    } else if (reset_ocio_shaders) {
      panel_footage_viewer->viewer_widget()->get_renderer()->destroy_ocio();
      panel_sequence_viewer->viewer_widget()->get_renderer()->destroy_ocio();
    }

  }
}

void PreferencesDialog::reset_default_shortcut() {
  QList<QTreeWidgetItem*> items = keyboard_tree->selectedItems();
  for (int i=0;i<items.size();i++) {
    QTreeWidgetItem* item = keyboard_tree->selectedItems().at(i);
    static_cast<KeySequenceEditor*>(keyboard_tree->itemWidget(item, 1))->reset_to_default();
  }
}

void PreferencesDialog::reset_all_shortcuts() {
  if (QMessageBox::question(
        this,
        tr("Confirm Reset All Shortcuts"),
        tr("Are you sure you wish to reset all keyboard shortcuts to their defaults?"),
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    for (int i=0;i<key_shortcut_fields.size();i++) {
      key_shortcut_fields.at(i)->reset_to_default();
    }
  }
}

bool PreferencesDialog::refine_shortcut_list(const QString &s, QTreeWidgetItem* parent) {
  if (parent == nullptr) {
    for (int i=0;i<keyboard_tree->topLevelItemCount();i++) {
      refine_shortcut_list(s, keyboard_tree->topLevelItem(i));
    }
  } else {
    parent->setExpanded(!s.isEmpty());

    bool all_children_are_hidden = !s.isEmpty();

    for (int i=0;i<parent->childCount();i++) {
      QTreeWidgetItem* item = parent->child(i);
      if (item->childCount() > 0) {
        all_children_are_hidden = refine_shortcut_list(s, item);
      } else {
        item->setHidden(false);
        if (s.isEmpty()) {
          all_children_are_hidden = false;
        } else {
          QString shortcut;
          if (keyboard_tree->itemWidget(item, 1) != nullptr) {
            shortcut = static_cast<QKeySequenceEdit*>(keyboard_tree->itemWidget(item, 1))->keySequence().toString();
          }
          if (item->text(0).contains(s, Qt::CaseInsensitive) || shortcut.contains(s, Qt::CaseInsensitive)) {
            all_children_are_hidden = false;
          } else {
            item->setHidden(true);
          }
        }
      }
    }

    if (parent->text(0).contains(s, Qt::CaseInsensitive)) all_children_are_hidden = false;

    parent->setHidden(all_children_are_hidden);

    return all_children_are_hidden;
  }
  return true;
}

void PreferencesDialog::load_shortcut_file() {
  QString fn = QFileDialog::getOpenFileName(this, tr("Import Keyboard Shortcuts"));
  if (!fn.isEmpty()) {
    QFile f(fn);
    if (f.exists() && f.open(QFile::ReadOnly)) {
      QByteArray ba = f.readAll();
      f.close();
      for (int i=0;i<key_shortcut_fields.size();i++) {
        int index = ba.indexOf(key_shortcut_fields.at(i)->action_name());
        if (index == 0 || (index > 0 && ba.at(index-1) == '\n')) {
          while (index < ba.size() && ba.at(index) != '\t') index++;
          QString ks;
          index++;
          while (index < ba.size() && ba.at(index) != '\n') {
            ks.append(ba.at(index));
            index++;
          }
          key_shortcut_fields.at(i)->setKeySequence(ks);
        } else {
          key_shortcut_fields.at(i)->reset_to_default();
        }
      }
    } else {
      QMessageBox::critical(
            this,
            tr("Error saving shortcuts"),
            tr("Failed to open file for reading")
            );
    }
  }
}

void PreferencesDialog::save_shortcut_file() {
  QString fn = QFileDialog::getSaveFileName(this, tr("Export Keyboard Shortcuts"));
  if (!fn.isEmpty()) {
    QFile f(fn);
    if (f.open(QFile::WriteOnly)) {
      bool start = true;
      for (int i=0;i<key_shortcut_fields.size();i++) {
        QString s = key_shortcut_fields.at(i)->export_shortcut();
        if (!s.isEmpty()) {
          if (!start) f.write("\n");
          f.write(s.toUtf8());
          start = false;
        }
      }
      f.close();
      QMessageBox::information(this, tr("Export Shortcuts"), tr("Shortcuts exported successfully"));
    } else {
      QMessageBox::critical(this, tr("Error saving shortcuts"), tr("Failed to open file for writing"));
    }
  }
}

void PreferencesDialog::browse_css_file() {
  QString fn = QFileDialog::getOpenFileName(this, tr("Browse for CSS file"));
  if (!fn.isEmpty()) {
    custom_css_fn->setText(fn);
  }
}

void PreferencesDialog::browse_ocio_config()
{
  QString fn = QFileDialog::getOpenFileName(this, tr("Browse for OpenColorIO configuration"));
  if (!fn.isEmpty()) {
    ocio_config_file->setText(fn);
    enable_color_management->setChecked(true);
  }
}

void PreferencesDialog::update_ocio_view_menu()
{
  update_ocio_view_menu(OCIO::GetCurrentConfig());
}

void PreferencesDialog::delete_all_previews() {
  if (QMessageBox::question(this,
                            tr("Delete All Previews"),
                            tr("Are you sure you want to delete all previews?"),
                            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    delete_previews(DELETE_BOTH);
    QMessageBox::information(this,
                             tr("Previews Deleted"),
                             tr("All previews deleted successfully. You may have to re-open your current project for "
                                "changes to take effect."),
                             QMessageBox::Ok);
  }
}

void PreferencesDialog::edit_default_sequence_settings()
{
  NewSequenceDialog nsd(this, nullptr, &default_sequence);
  nsd.SetNameEditable(false);
  nsd.exec();
}

void PreferencesDialog::setup_ui() {
  QVBoxLayout* verticalLayout = new QVBoxLayout(this);
  QTabWidget* tabWidget = new QTabWidget(this);

  // row counter used to ease adding new rows
  int row = 0;

  // General
  QWidget* general_tab = new QWidget(this);
  QGridLayout* general_layout = new QGridLayout(general_tab);

  // General -> Language
  general_layout->addWidget(new QLabel(tr("Language:")), row, 0);

  language_combobox = new QComboBox();

  // add default language (en-US)
  language_combobox->addItem(QLocale::languageToString(QLocale("en-US").language()));

  // add languages from file
  QList<QString> translation_paths = get_language_paths();

  // iterate through all language search paths
  for (int j=0;j<translation_paths.size();j++) {
    QDir translation_dir(translation_paths.at(j));
    if (translation_dir.exists()) {
      QStringList translation_files = translation_dir.entryList({"*.qm"}, QDir::Files | QDir::NoDotAndDotDot);
      for (int i=0;i<translation_files.size();i++) {
        // get path of translation relative to the application path
        QString locale_full_path = translation_dir.filePath(translation_files.at(i));
        QString locale_relative_path = QDir(get_app_path()).relativeFilePath(locale_full_path);

        QFileInfo locale_file(translation_files.at(i));
        QString locale_file_basename = locale_file.baseName();
        QString locale_str = locale_file_basename.mid(locale_file_basename.lastIndexOf('_')+1);
        language_combobox->addItem(QLocale(locale_str).nativeLanguageName(), locale_relative_path);

        if (olive::config.language_file == locale_relative_path) {
          language_combobox->setCurrentIndex(language_combobox->count() - 1);
        }
      }
    }
  }

  general_layout->addWidget(language_combobox, row, 1, 1, 4);

  row++;

  // General -> Image Sequence Formats
  general_layout->addWidget(new QLabel(tr("Image sequence formats:"), this), row, 0);

  imgSeqFormatEdit = new QLineEdit(general_tab);
  imgSeqFormatEdit->setText(olive::config.img_seq_formats);
  general_layout->addWidget(imgSeqFormatEdit, row, 1, 1, 4);

  row++;

  // General -> Thumbnail and Waveform Resolution
  general_layout->addWidget(new QLabel(tr("Thumbnail Resolution:"), this), row, 0);

  thumbnail_res_spinbox = new QSpinBox(this);
  thumbnail_res_spinbox->setMinimum(0);
  thumbnail_res_spinbox->setMaximum(INT_MAX);
  thumbnail_res_spinbox->setValue(olive::config.thumbnail_resolution);
  general_layout->addWidget(thumbnail_res_spinbox, row, 1);

  general_layout->addWidget(new QLabel(tr("Waveform Resolution:"), this), row, 2);

  waveform_res_spinbox = new QSpinBox(this);
  waveform_res_spinbox->setMinimum(0);
  waveform_res_spinbox->setMaximum(INT_MAX);
  waveform_res_spinbox->setValue(olive::config.waveform_resolution);
  general_layout->addWidget(waveform_res_spinbox, row, 3);

  QPushButton* delete_preview_btn = new QPushButton(tr("Delete Previews"));
  general_layout->addWidget(delete_preview_btn, row, 4);
  connect(delete_preview_btn, SIGNAL(clicked(bool)), this, SLOT(delete_all_previews()));

  row++;

  QHBoxLayout* misc_general = new QHBoxLayout();

  // General -> Use Software Fallbacks When Possible
  QCheckBox* use_software_fallbacks_checkbox = new QCheckBox(tr("Use Software Fallbacks When Possible"));
  AddBoolPair(use_software_fallbacks_checkbox, &olive::config.use_software_fallback, true);
  misc_general->addWidget(use_software_fallbacks_checkbox);

  // General -> Don't Use Proxies When Exporting
  QCheckBox* dont_use_proxies_when_exporting = new QCheckBox(tr("Don't Use Proxies When Exporting"));
  dont_use_proxies_when_exporting->setToolTip(tr("Use originals instead of proxies when exporting"));
  AddBoolPair(dont_use_proxies_when_exporting, &olive::config.dont_use_proxies_on_export);
  misc_general->addWidget(dont_use_proxies_when_exporting);

  // General -> Default Sequence Settings
  QPushButton* default_sequence_settings = new QPushButton(tr("Default Sequence Settings"));
  connect(default_sequence_settings, SIGNAL(clicked(bool)), this, SLOT(edit_default_sequence_settings()));
  misc_general->addWidget(default_sequence_settings);

  general_layout->addLayout(misc_general, row, 0, 1, 5);

  row++;

  tabWidget->addTab(general_tab, tr("General"));

  // Behavior
  QWidget* behavior_tab = new QWidget(this);
  tabWidget->addTab(behavior_tab, tr("Behavior"));

  ColumnedGridLayout* behavior_tab_layout = new ColumnedGridLayout(behavior_tab, 2);

  QCheckBox* add_default_effects_to_clips = new QCheckBox(tr("Add Default Effects to New Clips"));
  AddBoolPair(add_default_effects_to_clips, &olive::config.add_default_effects_to_clips);
  behavior_tab_layout->Add(add_default_effects_to_clips);

  QCheckBox* auto_seek_to_beginning = new QCheckBox(tr("Automatically Seek to the Beginning When Playing at the End of a Sequence"));
  AddBoolPair(auto_seek_to_beginning, &olive::config.auto_seek_to_beginning);
  behavior_tab_layout->Add(auto_seek_to_beginning);

  QCheckBox* selecting_also_seeks = new QCheckBox(tr("Selecting Also Seeks"));
  AddBoolPair(selecting_also_seeks, &olive::config.select_also_seeks);
  behavior_tab_layout->Add(selecting_also_seeks);

  QCheckBox* edit_tool_also_seeks = new QCheckBox(tr("Edit Tool Also Seeks"));
  AddBoolPair(edit_tool_also_seeks, &olive::config.edit_tool_also_seeks);
  behavior_tab_layout->Add(edit_tool_also_seeks);

  QCheckBox* edit_tool_selects_links = new QCheckBox(tr("Edit Tool Selects Links"));
  AddBoolPair(edit_tool_selects_links, &olive::config.edit_tool_selects_links);
  behavior_tab_layout->Add(edit_tool_selects_links);

  QCheckBox* seek_also_selects = new QCheckBox(tr("Seek Also Selects"));
  AddBoolPair(seek_also_selects, &olive::config.seek_also_selects);
  behavior_tab_layout->Add(seek_also_selects);

  QCheckBox* seek_to_end_of_pastes = new QCheckBox(tr("Seek to the End of Pastes"));
  AddBoolPair(seek_to_end_of_pastes, &olive::config.paste_seeks);
  behavior_tab_layout->Add(seek_to_end_of_pastes);

  QCheckBox* scroll_wheel_zooms = new QCheckBox(tr("Scroll Wheel Zooms"));
  scroll_wheel_zooms->setToolTip(tr("Hold CTRL to toggle this setting"));
  AddBoolPair(scroll_wheel_zooms, &olive::config.scroll_zooms);
  behavior_tab_layout->Add(scroll_wheel_zooms);

  QCheckBox* invert_timeline_scroll_axes = new QCheckBox(tr("Invert Timeline Scroll Axes"));
  AddBoolPair(invert_timeline_scroll_axes, &olive::config.invert_timeline_scroll_axes);
  behavior_tab_layout->Add(invert_timeline_scroll_axes);

  QCheckBox* enable_drag_files_to_timeline = new QCheckBox(tr("Enable Drag Files to Timeline"));
  AddBoolPair(enable_drag_files_to_timeline, &olive::config.enable_drag_files_to_timeline);
  behavior_tab_layout->Add(enable_drag_files_to_timeline);

  QCheckBox* autoscale_by_default = new QCheckBox(tr("Auto-Scale By Default"));
  AddBoolPair(autoscale_by_default, &olive::config.autoscale_by_default);
  behavior_tab_layout->Add(autoscale_by_default);

  QCheckBox* enable_seek_to_import = new QCheckBox(tr("Auto-Seek to Imported Clips"));
  AddBoolPair(enable_seek_to_import, &olive::config.enable_seek_to_import);
  behavior_tab_layout->Add(enable_seek_to_import);

  QCheckBox* enable_audio_scrubbing = new QCheckBox(tr("Audio Scrubbing"));
  AddBoolPair(enable_audio_scrubbing, &olive::config.enable_audio_scrubbing);
  behavior_tab_layout->Add(enable_audio_scrubbing);

  QCheckBox* enable_drop_on_media_to_replace = new QCheckBox(tr("Drop Files on Media to Replace"));
  AddBoolPair(enable_drop_on_media_to_replace, &olive::config.drop_on_media_to_replace);
  behavior_tab_layout->Add(enable_drop_on_media_to_replace);

  QCheckBox* enable_hover_focus = new QCheckBox(tr("Enable Hover Focus"));
  AddBoolPair(enable_hover_focus, &olive::config.hover_focus);
  behavior_tab_layout->Add(enable_hover_focus);

  QCheckBox* set_name_and_marker = new QCheckBox(tr("Ask For Name When Setting Marker"));
  AddBoolPair(set_name_and_marker, &olive::config.set_name_with_marker);
  behavior_tab_layout->Add(set_name_and_marker);

  // Appearance
  QWidget* appearance_tab = new QWidget(this);
  tabWidget->addTab(appearance_tab, tr("Appearance"));

  row = 0;

  QGridLayout* appearance_layout = new QGridLayout(appearance_tab);

  // Appearance -> Theme
  appearance_layout->addWidget(new QLabel(tr("Theme")), row, 0);

  ui_style = new QComboBox();
  ui_style->addItem(tr("Olive Dark (Default)"), olive::styling::kOliveDefaultDark);
  ui_style->addItem(tr("Olive Light"), olive::styling::kOliveDefaultLight);
  ui_style->addItem(tr("Native"), olive::styling::kNativeDarkIcons);
  ui_style->addItem(tr("Native (Light Icons)"), olive::styling::kNativeLightIcons);
  ui_style->setCurrentIndex(olive::config.style);
  appearance_layout->addWidget(ui_style, row, 1, 1, 2);

  row++;

#ifdef Q_OS_WIN
  // Native menu styling is only available on Windows. Environments like Ubuntu and Mac use the native menu system by
  // default
  QCheckBox* native_menus = new QCheckBox(tr("Use Native Menu Styling"));
  AddBoolPair(native_menus, &olive::config.use_native_menu_styling, true);
  appearance_layout->addWidget(native_menus, row, 0, 1, 3);

  row++;
#endif

  // Appearance -> Custom CSS
  appearance_layout->addWidget(new QLabel(tr("Custom CSS:"), this), row, 0);

  custom_css_fn = new QLineEdit(general_tab);
  custom_css_fn->setText(olive::config.css_path);
  appearance_layout->addWidget(custom_css_fn, row, 1);

  QPushButton* custom_css_browse = new QPushButton(tr("Browse"), general_tab);
  connect(custom_css_browse, SIGNAL(clicked(bool)), this, SLOT(browse_css_file()));
  appearance_layout->addWidget(custom_css_browse, row, 2);

  row++;

  // Appearance -> Effect Textbox Lines
  appearance_layout->addWidget(new QLabel(tr("Effect Textbox Lines:"), this), row, 0);

  effect_textbox_lines_field = new QSpinBox(general_tab);
  effect_textbox_lines_field->setMinimum(1);
  effect_textbox_lines_field->setValue(olive::config.effect_textbox_lines);
  appearance_layout->addWidget(effect_textbox_lines_field, row, 1, 1, 2);

  row++;

  // Playback
  QWidget* playback_tab = new QWidget(this);
  QVBoxLayout* playback_tab_layout = new QVBoxLayout(playback_tab);

  // Playback -> Memory Usage
  QGroupBox* memory_usage_group = new QGroupBox(playback_tab);
  memory_usage_group->setTitle(tr("Memory Usage"));
  QGridLayout* memory_usage_layout = new QGridLayout(memory_usage_group);
  memory_usage_layout->addWidget(new QLabel(tr("Upcoming Frame Queue:"), playback_tab), 0, 0);
  upcoming_queue_spinbox = new QDoubleSpinBox(playback_tab);
  upcoming_queue_spinbox->setValue(olive::config.upcoming_queue_size);
  memory_usage_layout->addWidget(upcoming_queue_spinbox, 0, 1);
  upcoming_queue_type = new QComboBox(playback_tab);
  upcoming_queue_type->addItem(tr("frames"));
  upcoming_queue_type->addItem(tr("seconds"));
  upcoming_queue_type->setCurrentIndex(olive::config.upcoming_queue_type);
  memory_usage_layout->addWidget(upcoming_queue_type, 0, 2);
  memory_usage_layout->addWidget(new QLabel(tr("Previous Frame Queue:"), playback_tab), 1, 0);
  previous_queue_spinbox = new QDoubleSpinBox(playback_tab);
  previous_queue_spinbox->setValue(olive::config.previous_queue_size);
  memory_usage_layout->addWidget(previous_queue_spinbox, 1, 1);
  previous_queue_type = new QComboBox(playback_tab);
  previous_queue_type->addItem(tr("frames"));
  previous_queue_type->addItem(tr("seconds"));
  previous_queue_type->setCurrentIndex(olive::config.previous_queue_type);
  memory_usage_layout->addWidget(previous_queue_type, 1, 2);
  playback_tab_layout->addWidget(memory_usage_group);

  tabWidget->addTab(playback_tab, tr("Playback"));

  // Audio
  QWidget* audio_tab = new QWidget(this);

  QGridLayout* audio_tab_layout = new QGridLayout(audio_tab);

  row = 0;

  // Audio -> Output Device

  audio_tab_layout->addWidget(new QLabel(tr("Output Device:")), row, 0);

  audio_output_devices = new QComboBox();
  audio_output_devices->addItem(tr("Default"), "");

  // list all available audio output devices
  QList<QAudioDeviceInfo> devs = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
  bool found_preferred_device = false;
  for (int i=0;i<devs.size();i++) {
    audio_output_devices->addItem(devs.at(i).deviceName(), devs.at(i).deviceName());
    if (!found_preferred_device
        && devs.at(i).deviceName() == olive::config.preferred_audio_output) {
      audio_output_devices->setCurrentIndex(audio_output_devices->count()-1);
      found_preferred_device = true;
    }
  }

  audio_tab_layout->addWidget(audio_output_devices, row, 1);

  row++;

  // Audio -> Input Device

  audio_tab_layout->addWidget(new QLabel(tr("Input Device:")), row, 0);

  audio_input_devices = new QComboBox();
  audio_input_devices->addItem(tr("Default"), "");

  // list all available audio input devices
  devs = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
  found_preferred_device = false;
  for (int i=0;i<devs.size();i++) {
    audio_input_devices->addItem(devs.at(i).deviceName(), devs.at(i).deviceName());
    if (!found_preferred_device
        && devs.at(i).deviceName() == olive::config.preferred_audio_input) {
      audio_input_devices->setCurrentIndex(audio_input_devices->count()-1);
      found_preferred_device = true;
    }
  }

  audio_tab_layout->addWidget(audio_input_devices, row, 1);

  row++;

  // Audio -> Sample Rate

  audio_tab_layout->addWidget(new QLabel(tr("Sample Rate:")), row, 0);

  audio_sample_rate = new QComboBox();
  combobox_audio_sample_rates(audio_sample_rate);
  for (int i=0;i<audio_sample_rate->count();i++) {
    if (audio_sample_rate->itemData(i).toInt() == olive::config.audio_rate) {
      audio_sample_rate->setCurrentIndex(i);
      break;
    }
  }

  audio_tab_layout->addWidget(audio_sample_rate, row, 1);

  row++;

  // Audio -> Audio Recording
  audio_tab_layout->addWidget(new QLabel(tr("Audio Recording:"), this), row, 0);

  recordingComboBox = new QComboBox(general_tab);
  recordingComboBox->addItem(tr("Mono"));
  recordingComboBox->addItem(tr("Stereo"));
  recordingComboBox->setCurrentIndex(olive::config.recording_mode - 1);
  audio_tab_layout->addWidget(recordingComboBox, row, 1);

  row++;

  tabWidget->addTab(audio_tab, tr("Audio"));

  //
  // COLOR MANAGEMENT
  //

  QWidget* color_management_tab = new QWidget();

  QGridLayout* color_management_layout = new QGridLayout(color_management_tab);

  row = 0;

  // COLOR MANAGEMENT -> Enable Color Management
  enable_color_management = new QCheckBox(tr("Enable Color Management"));
  enable_color_management->setChecked(olive::config.enable_color_management);
  color_management_layout->addWidget(enable_color_management, row, 0);

  row++;

  QGroupBox* opencolorio_groupbox = new QGroupBox();
  QGridLayout* opencolorio_groupbox_layout = new QGridLayout(opencolorio_groupbox);

  // COLOR MANAGEMENT -> OpenColorIO Config File
  opencolorio_groupbox_layout->addWidget(new QLabel(tr("OpenColorIO Config File:")), 0, 0);

  ocio_config_file = new QLineEdit();
  ocio_config_file->setText(olive::config.ocio_config_path);
  connect(ocio_config_file, SIGNAL(textChanged(const QString &)), this, SLOT(update_ocio_config(const QString&)));
  opencolorio_groupbox_layout->addWidget(ocio_config_file, 0, 1, 1, 4);

  QPushButton* ocio_config_browse_btn = new QPushButton(tr("Browse"));
  connect(ocio_config_browse_btn, SIGNAL(clicked(bool)), this, SLOT(browse_ocio_config()));
  opencolorio_groupbox_layout->addWidget(ocio_config_browse_btn, 0, 5);

  // COLOR MANAGEMENT -> Default Input Color Space
  ocio_default_input = new QComboBox();
  opencolorio_groupbox_layout->addWidget(new QLabel(tr("Default Input Color Space:")), 1, 0);
  opencolorio_groupbox_layout->addWidget(ocio_default_input, 1, 1, 1, 5);

  // COLOR MANAGEMENT -> Display
  ocio_display = new QComboBox();
  connect(ocio_display, SIGNAL(currentIndexChanged(int)), this, SLOT(update_ocio_view_menu()));
  opencolorio_groupbox_layout->addWidget(new QLabel(tr("Display:")), 2, 0);
  opencolorio_groupbox_layout->addWidget(ocio_display, 2, 1);

  // COLOR MANAGEMENT -> View
  ocio_view = new QComboBox();
  opencolorio_groupbox_layout->addWidget(new QLabel(tr("View:")), 2, 2);
  opencolorio_groupbox_layout->addWidget(ocio_view, 2, 3);

  // COLOR MANAGEMENT -> Look
  ocio_look = new QComboBox();
  opencolorio_groupbox_layout->addWidget(new QLabel(tr("Look:")), 2, 4);
  opencolorio_groupbox_layout->addWidget(ocio_look, 2, 5);

  color_management_layout->addWidget(opencolorio_groupbox, row, 0);

  row++;

  // COLOR MANAGEMENT -> Bit Depth
  QGroupBox* bit_depth_groupbox = new QGroupBox(tr("Bit Depth"));
  QGridLayout* bit_depth_groupbox_layout = new QGridLayout(bit_depth_groupbox);

  // COLOR MANAGEMENT -> Bit Depth -> Playback
  playback_bit_depth = new QComboBox();
  for (int i=0;i<olive::pixel_formats.size();i++) {
    playback_bit_depth->addItem(olive::pixel_formats.at(i).name, i);
  }
  playback_bit_depth->setCurrentIndex(olive::config.playback_bit_depth);
  bit_depth_groupbox_layout->addWidget(new QLabel(tr("Playback (Offline):")), 0, 0);
  bit_depth_groupbox_layout->addWidget(playback_bit_depth, 0, 1);

  // COLOR MANAGEMENT -> Bit Depth -> Export
  export_bit_depth = new QComboBox();
  for (int i=0;i<olive::pixel_formats.size();i++) {
    export_bit_depth->addItem(olive::pixel_formats.at(i).name, i);
  }
  export_bit_depth->setCurrentIndex(olive::config.export_bit_depth);
  bit_depth_groupbox_layout->addWidget(new QLabel(tr("Export (Online):")), 0, 2);
  bit_depth_groupbox_layout->addWidget(export_bit_depth, 0, 3);

  color_management_layout->addWidget(bit_depth_groupbox, row, 0);



  //row++;

  populate_ocio_menus(OCIO::GetCurrentConfig());

  tabWidget->addTab(color_management_tab, tr("Color Management"));

  // Shortcuts
  QWidget* shortcut_tab = new QWidget(this);

  QVBoxLayout* shortcut_layout = new QVBoxLayout(shortcut_tab);

  QLineEdit* key_search_line = new QLineEdit(shortcut_tab);
  key_search_line->setPlaceholderText(tr("Search for action or shortcut"));
  connect(key_search_line, SIGNAL(textChanged(const QString &)), this, SLOT(refine_shortcut_list(const QString &)));

  shortcut_layout->addWidget(key_search_line);

  keyboard_tree = new QTreeWidget(shortcut_tab);
  QTreeWidgetItem* tree_header = keyboard_tree->headerItem();
  tree_header->setText(0, tr("Action"));
  tree_header->setText(1, tr("Shortcut"));
  shortcut_layout->addWidget(keyboard_tree);

  QHBoxLayout* reset_shortcut_layout = new QHBoxLayout(shortcut_tab);

  QPushButton* import_shortcut_button = new QPushButton(tr("Import"), shortcut_tab);
  reset_shortcut_layout->addWidget(import_shortcut_button);
  connect(import_shortcut_button, SIGNAL(clicked(bool)), this, SLOT(load_shortcut_file()));

  QPushButton* export_shortcut_button = new QPushButton(tr("Export"), shortcut_tab);
  reset_shortcut_layout->addWidget(export_shortcut_button);
  connect(export_shortcut_button, SIGNAL(clicked(bool)), this, SLOT(save_shortcut_file()));

  reset_shortcut_layout->addStretch();

  QPushButton* reset_selected_shortcut_button = new QPushButton(tr("Reset Selected"), shortcut_tab);
  reset_shortcut_layout->addWidget(reset_selected_shortcut_button);
  connect(reset_selected_shortcut_button, SIGNAL(clicked(bool)), this, SLOT(reset_default_shortcut()));

  QPushButton* reset_all_shortcut_button = new QPushButton(tr("Reset All"), shortcut_tab);
  reset_shortcut_layout->addWidget(reset_all_shortcut_button);
  connect(reset_all_shortcut_button, SIGNAL(clicked(bool)), this, SLOT(reset_all_shortcuts()));

  shortcut_layout->addLayout(reset_shortcut_layout);

  tabWidget->addTab(shortcut_tab, tr("Keyboard"));

  verticalLayout->addWidget(tabWidget);

  QDialogButtonBox* buttonBox = new QDialogButtonBox(this);
  buttonBox->setOrientation(Qt::Horizontal);
  buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

  verticalLayout->addWidget(buttonBox);

  connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
  connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
}
