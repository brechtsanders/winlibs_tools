#include "gui-wizard.h"
//#include "winlibs-setup-GUI.h"
#include <wx/msgdlg.h>
//#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <wx/button.h>
//#include <wx/image.h>
#include <wx/cursor.h>

#define PROGRAM_NAME "winlibs configurator"

using namespace std;

////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(wxWizardPageStaticText, wxWizardPageSimple)
  EVT_SIZE(wxWizardPageStaticText::OnSize)
  EVT_WIZARD_PAGE_CHANGING(wxID_ANY, wxWizardPageStaticText::OnWizardPageChanging)
END_EVENT_TABLE()

wxWizardPageStaticText::wxWizardPageStaticText (wxWizard* parent, wxString text)
: wxWizardPageSimple(parent), txt(text)
{
  sizer = new wxBoxSizer(wxVERTICAL);
  statictext = new wxStaticText(this, wxID_ANY, txt);
  sizer->Add(statictext, 0, wxEXPAND | wxFIXED_MINSIZE);
  SetSizer(sizer);
  //sizer->SetSizeHints(this);
  //Layout();
}

void wxWizardPageStaticText::OnSize (wxSizeEvent& event)
{
  int sizex;
  int sizey;
  wxStaticText* oldstatictext = statictext;
  statictext = new wxStaticText(this, wxID_ANY, txt);
  GetClientSize(&sizex, &sizey);
  statictext->Wrap(sizex);
  sizer->Replace(oldstatictext, statictext);
  delete oldstatictext;
  //wxWizardPageSimple::OnSize(event);
  //wxWizardPageSimple::Refresh();/////
  wxWizardPageSimple::Layout();/////
}

void wxWizardPageStaticText::OnWizardPageChanging (wxWizardEvent& event)
{
}

////////////////////////////////////////////////////////////////////////

const long wxWizardPageSelect::ID_LISTBOX = wxNewId();

BEGIN_EVENT_TABLE(wxWizardPageSelect, wxWizardPageStaticText)
  EVT_LISTBOX_DCLICK(ID_LISTBOX, wxWizardPageSelect::OnListboxDoubleClick)
END_EVENT_TABLE()

wxWizardPageSelect::wxWizardPageSelect (wxWizard* parent, wxString text)
: wxWizardPageStaticText(parent, text)
{
  listbox = new wxListBox(this, ID_LISTBOX);
  sizer->Add(listbox, 10, wxEXPAND | wxTOP, 8);
  sizer->SetSizeHints(this);
}

void wxWizardPageSelect::OnListboxDoubleClick (wxCommandEvent& event)
{
  ((wxWizard*)GetParent())->ShowPage(GetNext(), true);
}

void wxWizardPageSelect::OnWizardPageChanging (wxWizardEvent& event)
{
  if (event.GetDirection() && listbox->IsShown() && listbox->GetSelection() == wxNOT_FOUND) {
    event.Veto();
    wxMessageBox(wxT("Please make a selection before continuing"), wxT("Nothing selected"), wxICON_WARNING | wxOK, this);
  }
}

////////////////////////////////////////////////////////////////////////

const long wxWizardPageSelectDirectory::ID_BUTTON_BROWSE = wxNewId();

wxWizardPageSelectDirectory::wxWizardPageSelectDirectory (wxWizard* parent, wxString text, wxString prompt)
: wxWizardPageStaticText(parent, text), dialogprompt(prompt), askcreate(false), allowempty(false)
{
  wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
  directory = new wxTextCtrl(this, wxID_ANY);
  hsizer->Add(directory, 10, wxEXPAND | wxALL /*|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL*/);
  hsizer->Add(new wxButton(this, ID_BUTTON_BROWSE, wxT("B&rowse")), 0, wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
  sizer->Add(hsizer, 0, wxEXPAND | wxTOP, 8);
  sizer->SetSizeHints(this);

  Connect(ID_BUTTON_BROWSE, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&wxWizardPageSelectDirectory::OnClickBrowse);
}

void wxWizardPageSelectDirectory::SetValue (wxString value)
{
  //listbox->SetStringSelection(value);
  //directory->SetValue(value);
}

wxString wxWizardPageSelectDirectory::GetValue ()
{
  return directory->GetValue();
}

void wxWizardPageSelectDirectory::OnClickBrowse (wxCommandEvent& event)
{
  wxString result;
  if (!(result = wxDirSelector(dialogprompt, directory->GetValue())).empty()) {
    directory->SetValue(result);
  }
}

void wxWizardPageSelectDirectory::OnWizardPageChanging (wxWizardEvent& event)
{
  if (event.GetDirection()) {
    if (directory->GetValue().empty()) {
      if (allowempty)
        return;
      event.Veto();
      wxMessageBox(wxT("Please select a directory before continuing"), wxT("No directory specified"), wxICON_WARNING | wxOK, this);
    } else if (!wxDirExists(directory->GetValue())) {
      if (askcreate) {
        if (wxMessageBox(wxT("This directory does not exist.\nWould you like to create it?"), wxT("Directory does not exist"), wxICON_QUESTION  | wxYES_NO, this) == wxYES) {
          if (wxMkdir(directory->GetValue()))
            return;
          event.Veto();
          wxMessageBox(wxT("Unable to create directory:\n") + directory->GetValue(), wxT("Error creating directory"), wxICON_ERROR | wxOK, this);
          return;
        }
      }
      event.Veto();
      wxMessageBox(wxT("Please select a valid directory before continuing"), wxT("Invalid directory"), wxICON_WARNING | wxOK, this);
    }
  }
}

////////////////////////////////////////////////////////////////////////

const long wxWizardRadioBox::ID_RADIOBOX = wxNewId();

wxWizardRadioBox::wxWizardRadioBox (wxWizard* parent, wxString text, wxString choice1, wxString choice2)
: wxWizardPageStaticText(parent, text)
{
  wxString choices[2] = {choice1, choice2};
  radiobox = new wxRadioBox(this, ID_RADIOBOX, wxT(""), wxDefaultPosition, wxDefaultSize, 2, choices, 1, wxRA_SPECIFY_COLS);
  sizer->Add(radiobox, 0, wxEXPAND | wxTOP, 8);
  sizer->SetSizeHints(this);
}

////////////////////////////////////////////////////////////////////////

WinLibsWizardGUI::WinLibsWizardGUI (wxWindow* parent)
: wxWizard(parent, wxID_ANY, wxT(PROGRAM_NAME), wxNullBitmap, wxDefaultPosition, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
  wxWizardPageSimple* lastpage;
  //warn before continuing
  page_begin = new wxWizardPageStaticText(this, wxT(
    "This wizard will prepare a working winlibs build environment.\n\n"
    "It will create folders and copy files, but will otherwise not modify your system.\n"
    "As a result there is no uninstall wizard. To uninstall just delete the folders created by this process.\n\n"
    "It is also possible to run this wizard multiple times to install different build environments on the same system (e.g. 32/64 bit or different versions)."
  ));
  lastpage = page_begin;

  //ask for source signature directory
  page_msyspath = new wxWizardPageSelectDirectory(this, wxT("Enter the location of MSYS2 on your system or leave empty to download and install MSYS2."));
  page_msyspath->AllowEmpty(true);
  wxWizardPageSimple::Chain(lastpage, page_msyspath);
  lastpage = page_msyspath;
/*
  //ask for source signature directory
  page_srcdir = new wxWizardPageSelectDirectory(this, wxT("Choose the source signature directory"), wxT("Choose the source signature directory"));
  {
    stringset signaturesdirs;
    get_office_signature_dirs(signaturesdirs);
    stringset::iterator it;
    for (it = signaturesdirs.begin(); it != signaturesdirs.end(); it++) {
      page_srcdir->AddItem(it->c_str());
    }
    if (signaturesdirs.size() == 1)
      page_srcdir->SetValue(signaturesdirs.begin()->c_str());
  }
  wxWizardPageSimple::Chain(page_begin, page_srcdir);

  //ask for destination template directory
  page_dstdir = new wxWizardPageSelectDirectory(this, wxT("Choose the destination template directory"), wxT("Choose the destination template directory"));
  page_dstdir->HideSelectBox();
  page_dstdir->SetValue(get_default_template_dir().c_str());
  page_dstdir->AskCreate();
  wxWizardPageSimple::Chain(page_srcdir, page_dstdir);

  //ask for search type (substring or whole words only)
  page_searchtype = new wxWizardRadioBox(this, wxT("Would you like to search for all matches (including parts of longer words) or whole words only?"), wxT("All matches"), wxT("Whole words only"));
  page_searchtype->SetValue(1);
  wxWizardPageSimple::Chain(page_dstdir, page_searchtype);

  //ask for replacement choices
  wxWizardPageReplaceChoices* page_choices = new wxWizardPageReplaceChoices(this, wxT("The next page(s) will allow you to choose which information you want to get from which values."));
  wxWizardPageSimple::Chain(page_searchtype, page_choices);

  wxWizardPageSimple* p = new wxWizardPageReplaceChoices(this);
  page_choices->SetNext(p);
*/

  //display ready to begin message
  page_done = new wxWizardPageStaticText(this, wxT("Finished.\n"));
  wxWizardPageSimple::Chain(lastpage, page_done);

  //status = new wxTextCtrl(page_done, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY  | wxHSCROLL);
  //page_done->GetSizer()->Add(status, 10, wxEXPAND | wxTOP, 8);
  //p->SetNext(page_done);
}

void WinLibsWizardGUI::OnProcessStatus (string msg, bool iserror)
{
  status->AppendText(wxString(msg.c_str()) + wxString("\n"));
}

bool WinLibsWizardGUI::Run ()
{
  //GetPageAreaSizer()->Add(page_begin);
  RunWizard(page_begin);
  return true;
}
