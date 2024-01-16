#ifndef __INCLUDED_WINLIBS_SETUP_GUI_H
#define __INCLUDED_WINLIBS_SETUP_GUI_H

#include <wx/msw/winundef.h>
#include <wx/app.h>

class wxWinlibsSetupGUIApp : public wxApp
{
 public:
  virtual bool OnInit();
};

#endif //__INCLUDED_WINLIBS_SETUP_GUI_H
