#ifndef __INCLUDED_SIGNATURE2TEMPLATEGUI_H
#define __INCLUDED_SIGNATURE2TEMPLATEGUI_H

//#define wxUSE_UNICODE 0
//#undef _UNICODE
#include <wx/wizard.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/listbox.h>
#include <wx/textctrl.h>
#include <wx/radiobox.h>

class wxWizardPageStaticText : public wxWizardPageSimple
{
 private:
  DECLARE_EVENT_TABLE()
 protected:
  wxBoxSizer* sizer;
  wxStaticText* statictext;
  wxString txt;
 public:
  wxWizardPageStaticText (wxWizard* parent, wxString text);
  void SetText (wxString& text) { txt = text; }
  void AppendText (wxString text) { txt += text; }
 protected:
  virtual void OnSize (wxSizeEvent& event);
  virtual void OnWizardPageChanging (wxWizardEvent& event);
};

class wxWizardPageSelect : public wxWizardPageStaticText
{
 protected:
  static const long ID_LISTBOX;
 private:
  DECLARE_EVENT_TABLE()
 protected:
  wxListBox* listbox;
 public:
  wxWizardPageSelect (wxWizard* parent, wxString text);
  void HideSelectBox () { listbox->Hide(); }
  void Clear () { listbox->Clear(); }
  void AddItem (const char* itemtext) { listbox->Append(itemtext); }
  void SetValue (int value) { listbox->SetSelection(value); }
  int GetValue () { return listbox->GetSelection(); }
  wxString GetValueString () { int i = listbox->GetSelection(); return (i == wxNOT_FOUND ? wxT("") : listbox->GetString(i)); }
 protected:
  virtual void OnListboxDoubleClick (wxCommandEvent& event);
  virtual void OnWizardPageChanging (wxWizardEvent& event);
};

class wxWizardPageSelectDirectory : public wxWizardPageStaticText
{
 protected:
  static const long ID_BUTTON_BROWSE;
 protected:
  wxTextCtrl* directory;
  wxString dialogprompt;
  bool askcreate;
  bool allowempty;
 public:
  wxWizardPageSelectDirectory (wxWizard* parent, wxString text, wxString prompt = wxT(""));
  void AskCreate (bool ask_create = true) { askcreate = ask_create; }
  void AllowEmpty (bool allow_empty = true) { allowempty = allow_empty; }
  void SetValue (wxString value);
  wxString GetValue ();
 private:
  void OnListBoxSelect (wxCommandEvent& event);
  void OnClickBrowse (wxCommandEvent& event);
 protected:
  virtual void OnWizardPageChanging (wxWizardEvent& event);
};

class wxWizardRadioBox : public wxWizardPageStaticText
{
 protected:
  static const long ID_RADIOBOX;
 protected:
   wxRadioBox* radiobox;
 public:
  wxWizardRadioBox (wxWizard* parent, wxString text, wxString choice1, wxString choice2);
  void SetValue (int value) { radiobox->SetSelection(value); }
  int GetValue () { return radiobox->GetSelection(); }
};

class WinLibsWizardGUI : public wxWizard
{
 private:
  wxWizardPageStaticText* page_begin;
  wxWizardPageSelectDirectory* page_msyspath;
/*
  wxWizardPageSelectDirectory* page_dstdir;
  wxWizardRadioBox* page_searchtype;
*/
  wxWizardPageStaticText* page_done;
  wxTextCtrl* status;
 public:
  WinLibsWizardGUI (wxWindow* parent);
  virtual void OnProcessStatus (std::string msg, bool iserror);
  virtual bool Run ();
};

#endif //__INCLUDED_SIGNATURE2TEMPLATEGUI_H
