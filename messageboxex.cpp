/////////////////////////////////////////////////////////////////////////////
// Name:        messageboxex.cpp
// Purpose:     
// Author:      Julian Smart
// Modified by: 
// Created:     12/10/05 18:55:50
// RCS-ID:      
// Copyright:   (c) Julian Smart, Anthemion Software Ltd
// Licence:     
/////////////////////////////////////////////////////////////////////////////

#if defined(__GNUG__) && !defined(__APPLE__)
#pragma implementation "messageboxex.h"
#endif

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

////@begin includes
////@end includes

#include "messageboxex.h"

#if 0
#if defined(__WXMSW__)
#include "bitmaps/exclamation_msw.xpm"
#include "bitmaps/question_msw.xpm"
#include "bitmaps/info_msw.xpm"
#elif defined(__WXGTK__)
#include "bitmaps/exclamation_gtk.xpm"
#include "bitmaps/question_gtk.xpm"
#include "bitmaps/info_gtk.xpm"
#elif defined(__WXMAC__)
#include "bitmaps/exclamation_generic.xpm"
#include "bitmaps/question_generic.xpm"
#include "bitmaps/info_generic.xpm"
#endif
#endif

/*!
 * wxMessageDialogEx type definition
 */

IMPLEMENT_DYNAMIC_CLASS( wxMessageDialogEx, wxDialog )

/*!
 * wxMessageDialogEx event table definition
 */

BEGIN_EVENT_TABLE( wxMessageDialogEx, wxDialog )

////@begin wxMessageDialogEx event table entries
    EVT_BUTTON( wxID_YES, wxMessageDialogEx::OnYesClick )

    EVT_BUTTON( wxID_YESTOALL, wxMessageDialogEx::OnYestoallClick )

    EVT_BUTTON( wxID_NO, wxMessageDialogEx::OnNoClick )

    EVT_BUTTON( wxID_NOTOALL, wxMessageDialogEx::OnNotoallClick )

    EVT_BUTTON( wxID_OK, wxMessageDialogEx::OnOkClick )

    EVT_BUTTON( wxID_CANCEL, wxMessageDialogEx::OnCancelClick )

////@end wxMessageDialogEx event table entries

END_EVENT_TABLE()

/*!
 * wxMessageDialogEx constructors
 */

wxMessageDialogEx::wxMessageDialogEx( )
{
    m_messageDialogStyle = wxOK;
    m_displayNextTime = true;
    //m_staticBitmap = NULL;
}

wxMessageDialogEx::wxMessageDialogEx( wxWindow* parent, const wxString& message, const wxString& caption, int style, const wxPoint& pos )
{
    m_messageDialogStyle = wxOK;
    m_displayNextTime = true;
    //m_staticBitmap = NULL;

    Create(parent, message, caption, style, pos);
}

/*!
 * wxMessageBoxEx creator
 */

bool wxMessageDialogEx::Create( wxWindow* parent, const wxString& message, const wxString& caption, int style, const wxPoint& pos )
{
    m_messageDialogStyle = style;
    m_message = message;

    SetExtraStyle(GetExtraStyle()|wxWS_EX_BLOCK_EVENTS);
    wxDialog::Create( parent, wxID_ANY, caption, pos, wxDefaultSize, wxDEFAULT_DIALOG_STYLE );

    CreateControls();
    GetSizer()->Fit(this);
    GetSizer()->SetSizeHints(this);
    Centre();

    return true;
}

/*!
 * Control creation for wxMessageBoxEx
 */

void wxMessageDialogEx::CreateControls()
{    
//// @begin wxMessageDialogEx content construction
    wxMessageDialogEx* itemDialog1 = this;

    wxBoxSizer* itemBoxSizer2 = new wxBoxSizer(wxVERTICAL);
    itemDialog1->SetSizer(itemBoxSizer2);

    wxBoxSizer* itemBoxSizer3 = new wxBoxSizer(wxVERTICAL);
    itemBoxSizer2->Add(itemBoxSizer3, 0, wxALIGN_CENTER_HORIZONTAL|wxALL, 5);

    wxBoxSizer* itemBoxSizer4 = new wxBoxSizer(wxHORIZONTAL);
    itemBoxSizer3->Add(itemBoxSizer4, 0, wxGROW, 5);

#if 0
    wxBitmap m_staticBitmapBitmap;
    if (m_messageDialogStyle & wxICON_QUESTION)
        m_staticBitmapBitmap = itemDialog1->GetBitmapResource(wxT("question.xpm"));
    else if (m_messageDialogStyle & (wxICON_EXCLAMATION|wxICON_ERROR))
        m_staticBitmapBitmap = itemDialog1->GetBitmapResource(wxT("exclamation.xpm"));
    else
        m_staticBitmapBitmap = itemDialog1->GetBitmapResource(wxT("info.xpm"));

    m_staticBitmap = new wxStaticBitmap( itemDialog1, wxID_STATIC, m_staticBitmapBitmap, wxDefaultPosition, wxSize(32, 32), 0 );
    itemBoxSizer4->Add(m_staticBitmap, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    itemBoxSizer4->AddSpacer(10);
#endif

    wxStaticText* itemStaticText6 = new wxStaticText( itemDialog1, wxID_STATIC, m_message, wxDefaultPosition, wxDefaultSize, 0 );
    itemBoxSizer4->Add(itemStaticText6, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);

    itemBoxSizer3->AddSpacer(10);

    wxBoxSizer* itemBoxSizer7 = new wxBoxSizer(wxHORIZONTAL);
    itemBoxSizer3->Add(itemBoxSizer7, 0, wxALIGN_CENTER_HORIZONTAL, 5);

    if (m_messageDialogStyle & wxDISPLAY_NEXT_TIME)
    {
        wxCheckBox* displayNextTime = new wxCheckBox( itemDialog1, wxID_ANY, _("&Display next time"), wxDefaultPosition, wxDefaultSize, 0 );
        displayNextTime->SetValidator(wxGenericValidator(& m_displayNextTime));
        itemBoxSizer7->Add(displayNextTime, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
        itemBoxSizer7->AddStretchSpacer();
    }

    if (m_messageDialogStyle & wxYES)
    {
        wxButton* itemButton8 = new wxButton( itemDialog1, wxID_YES, _("&Yes"), wxDefaultPosition, wxDefaultSize, 0 );
        itemBoxSizer7->Add(itemButton8, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    }
    
    if (m_messageDialogStyle & wxYES_TO_ALL)
    {
        wxButton* itemButton9 = new wxButton( itemDialog1, wxID_YESTOALL, _("Yes to &All"), wxDefaultPosition, wxDefaultSize, 0 );
        itemBoxSizer7->Add(itemButton9, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    }
    
    if (m_messageDialogStyle & wxNO)
    {
        wxButton* itemButton10 = new wxButton( itemDialog1, wxID_NO, _("&No"), wxDefaultPosition, wxDefaultSize, 0 );
        itemBoxSizer7->Add(itemButton10, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    }
    
    if (m_messageDialogStyle & wxNO_TO_ALL)
    {
        wxButton* itemButton11 = new wxButton( itemDialog1, wxID_NOTOALL, _("No &to All"), wxDefaultPosition, wxDefaultSize, 0 );
        itemBoxSizer7->Add(itemButton11, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    }
    
    if (m_messageDialogStyle & wxOK)
    {
        wxButton* itemButton12 = new wxButton( itemDialog1, wxID_OK, _("&OK"), wxDefaultPosition, wxDefaultSize, 0 );
        itemBoxSizer7->Add(itemButton12, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    }
    
    if (m_messageDialogStyle & wxCANCEL)
    {
        wxButton* itemButton13 = new wxButton( itemDialog1, wxID_CANCEL, _("&Cancel"), wxDefaultPosition, wxDefaultSize, 0 );
        itemBoxSizer7->Add(itemButton13, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    }
    
//// @end wxMessageDialogEx content construction
}

/*!
 * Should we show tooltips?
 */

bool wxMessageDialogEx::ShowToolTips()
{
    return true;
}

#if 0
/*!
 * Get bitmap resources
 */

wxBitmap wxMessageDialogEx::GetBitmapResource( const wxString& name )
{
    // Bitmap retrieval
    wxUnusedVar(name);
    if (name == _T("info.xpm"))
    {
        wxBitmap bitmap(info_xpm);
        return bitmap;
    }
    else if (name == _T("question.xpm"))
    {
        wxBitmap bitmap(question_xpm);
        return bitmap;
    }
    else if (name == _T("exclamation.xpm"))
    {
        wxBitmap bitmap(exclamation_xpm);
        return bitmap;
    }
    return wxNullBitmap;
}
#endif

/*!
 * Get icon resources
 */

wxIcon wxMessageDialogEx::GetIconResource( const wxString& name )
{
    // Icon retrieval
////@begin wxMessageDialogEx icon retrieval
    wxUnusedVar(name);
    return wxNullIcon;
////@end wxMessageDialogEx icon retrieval
}
/*!
 * wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_YES
 */

void wxMessageDialogEx::OnYesClick( wxCommandEvent& WXUNUSED(event) )
{
    TransferDataFromWindow();
    EndModal(wxID_YES);
}


/*!
 * wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_YESTOALL
 */

void wxMessageDialogEx::OnYestoallClick( wxCommandEvent& WXUNUSED(event) )
{
    TransferDataFromWindow();
    EndModal(wxID_YESTOALL);
}


/*!
 * wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_NO
 */

void wxMessageDialogEx::OnNoClick( wxCommandEvent& WXUNUSED(event) )
{
    TransferDataFromWindow();
    EndModal(wxID_NO);
}


/*!
 * wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_NOTOALL
 */

void wxMessageDialogEx::OnNotoallClick( wxCommandEvent& WXUNUSED(event) )
{
    TransferDataFromWindow();
    EndModal(wxID_NOTOALL);
}


/*!
 * wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_OK
 */

void wxMessageDialogEx::OnOkClick( wxCommandEvent& WXUNUSED(event) )
{
    TransferDataFromWindow();
    EndModal(wxID_OK);
}


/*!
 * wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_CANCEL
 */

void wxMessageDialogEx::OnCancelClick( wxCommandEvent& WXUNUSED(event) )
{
    EndModal(wxID_CANCEL);
}

// Convenience dialog
int wxMessageBoxEx(const wxString& msg, const wxString& caption, int style, wxWindow* parent)
{
    wxMessageDialogEx dialog(parent, msg, caption, style);
    int id = dialog.ShowModal();
    if (id == wxID_YES)
        return wxYES;
    else if (id == wxID_NO)
        return wxNO;
    else if (id == wxID_YESTOALL)
        return wxYES_TO_ALL;
    else if (id == wxID_NOTOALL)
        return wxNO_TO_ALL;
    else if (id == wxID_OK)
        return wxOK;
    else if (id == wxID_CANCEL)
        return wxCANCEL;
    
    return wxCANCEL;
}
