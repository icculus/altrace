/////////////////////////////////////////////////////////////////////////////
// Name:        messageboxex.h
// Purpose:     
// Author:      Julian Smart
// Modified by: 
// Created:     12/11/05 13:05:56
// RCS-ID:      
// Copyright:   (c) Julian Smart, Anthemion Software Ltd
// Licence:     
/////////////////////////////////////////////////////////////////////////////

#ifndef _MESSAGEBOXEX_H_
#define _MESSAGEBOXEX_H_

#if defined(__GNUG__) && !defined(__APPLE__)
#pragma interface "messageboxex.cpp"
#endif

/*!
 * Includes
 */

////@begin includes
#include "wx/valgen.h"
////@end includes

/*!
 * Forward declarations
 */

////@begin forward declarations
////@end forward declarations

/*!
 * Control identifiers
 */

////@begin control identifiers
#define ID_MESSAGEDIALOGEX 24900
#define SYMBOL_WXMESSAGEDIALOGEX_STYLE wxCAPTION|wxRESIZE_BORDER|wxSYSTEM_MENU|wxCLOSE_BOX
#define SYMBOL_WXMESSAGEDIALOGEX_TITLE _("Message")
#define SYMBOL_WXMESSAGEDIALOGEX_IDNAME ID_MESSAGEDIALOGEX
#define SYMBOL_WXMESSAGEDIALOGEX_SIZE wxSize(400, 300)
#define SYMBOL_WXMESSAGEDIALOGEX_POSITION wxDefaultPosition
#define ID_MESSAGEDIALOGEX_DISPLAY_NEXT_TIME 24901
////@end control identifiers

#define wxYES_TO_ALL        0x00100000
#define wxNO_TO_ALL         0x00200000
#define wxDISPLAY_NEXT_TIME 0x00400000

/*!
 * Compatibility
 */

#ifndef wxCLOSE_BOX
#define wxCLOSE_BOX 0x1000
#endif

/*!
 * wxMessageDialogEx class declaration
 */

class wxMessageDialogEx: public wxDialog
{    
    DECLARE_DYNAMIC_CLASS( wxMessageDialogEx )
    DECLARE_EVENT_TABLE()

public:
    /// Constructors
    wxMessageDialogEx( );
    wxMessageDialogEx( wxWindow* parent, const wxString& message, const wxString& caption = _("Message"), int style = wxOK, const wxPoint& pos = wxDefaultPosition );

    /// Creation
    bool Create( wxWindow* parent, const wxString& message, const wxString& caption = _("Message"), int style = wxOK, const wxPoint& pos = wxDefaultPosition );

    /// Creates the controls and sizers
    void CreateControls();

////@begin wxMessageDialogEx event handler declarations

    /// wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_YES
    void OnYesClick( wxCommandEvent& event );

    /// wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_YESTOALL
    void OnYestoallClick( wxCommandEvent& event );

    /// wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_NO
    void OnNoClick( wxCommandEvent& event );

    /// wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_NOTOALL
    void OnNotoallClick( wxCommandEvent& event );

    /// wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_OK
    void OnOkClick( wxCommandEvent& event );

    /// wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_CANCEL
    void OnCancelClick( wxCommandEvent& event );

////@end wxMessageDialogEx event handler declarations

////@begin wxMessageDialogEx member function declarations

    wxString GetMessage() const { return m_message ; }
    void SetMessage(wxString value) { m_message = value ; }

    int GetMessageDialogStyle() const { return m_messageDialogStyle ; }
    void SetMessageDialogStyle(int value) { m_messageDialogStyle = value ; }

    bool GetDisplayNextTime() const { return m_displayNextTime ; }
    void SetDisplayNextTime(bool value) { m_displayNextTime = value ; }

    /// Retrieves bitmap resources
    //wxBitmap GetBitmapResource( const wxString& name );

    /// Retrieves icon resources
    wxIcon GetIconResource( const wxString& name );
////@end wxMessageDialogEx member function declarations

    /// Should we show tooltips?
    static bool ShowToolTips();

////@begin wxMessageDialogEx member variables
    //wxStaticBitmap* m_staticBitmap;
    wxString m_message;
    int m_messageDialogStyle;
    bool m_displayNextTime;
////@end wxMessageDialogEx member variables
};

// Convenience dialog
int wxMessageBoxEx(const wxString& msg, const wxString& caption = _("Message"), int style = wxOK, wxWindow* parent = NULL);

#endif
    // _MESSAGEBOXEX_H_
