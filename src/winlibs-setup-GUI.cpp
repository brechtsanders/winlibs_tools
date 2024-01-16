#include "winlibs-setup-GUI.h"
#include "gui-wizard.h"
#include <wx/msgdlg.h>

using namespace std;

//#define LOGERROR(s) { wxMessageBox((s), wxT(PROGRAM_NAME " error"), wxOK | wxICON_ERROR); }

IMPLEMENT_APP(wxWinlibsSetupGUIApp);

bool wxWinlibsSetupGUIApp::OnInit()
{
  WinLibsWizardGUI* wizard = new WinLibsWizardGUI(NULL);
  //wizard->SetIcon(wxICON(aaaa));
  wizard->Run();
  return false;
}
