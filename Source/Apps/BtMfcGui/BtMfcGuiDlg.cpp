// BtMfcGuiDlg.cpp : implementation file
//

#include "stdafx.h"
#include "BtMfcGui.h"
#include "BtMfcGuiDlg.h"
#include "BlueTune.h"
#include "NptWin32MessageQueue.h"
#include ".\btmfcguidlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	enum { IDD = IDD_ABOUTBOX };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

// Implementation
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
END_MESSAGE_MAP()

class MfcPlayer : public BLT_Player
{
public:
    MfcPlayer(CBtMfcGuiDlg* dialog) : 
        BLT_Player(new NPT_Win32WindowMessageQueue()),
        m_Dialog(dialog), 
        m_Scrolling(false) {}
    ~MfcPlayer() { delete m_Queue;}

    // message handlers
    void OnStreamPositionNotification(BLT_StreamPosition& position);

    CBtMfcGuiDlg* m_Dialog;
    bool          m_Scrolling;
};


void 
MfcPlayer::OnStreamPositionNotification(BLT_StreamPosition& position)
{
    int range = m_Dialog->m_Slider.GetRangeMax()-m_Dialog->m_Slider.GetRangeMin();
    int pos = (position.offset*range)/position.range;
    m_Dialog->m_Slider.SetPos(pos);
}

// CBtMfcGuiDlg dialog



CBtMfcGuiDlg::CBtMfcGuiDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CBtMfcGuiDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    m_Player = NULL;
}

void CBtMfcGuiDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_TRACK_SLIDER, m_Slider);
}

BEGIN_MESSAGE_MAP(CBtMfcGuiDlg, CDialog)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	//}}AFX_MSG_MAP
    ON_BN_CLICKED(IDCANCEL, OnBnClickedCancel)
    ON_BN_CLICKED(IDC_OPEN_BUTTON, OnBnClickedOpenButton)
    ON_BN_CLICKED(IDC_PLAY_BUTTON, OnBnClickedPlayButton)
    ON_BN_CLICKED(IDC_PAUSE_BUTTON, OnBnClickedPauseButton)
    ON_BN_CLICKED(IDC_STOP_BUTTON, OnBnClickedStopButton)
    ON_WM_HSCROLL()
END_MESSAGE_MAP()


// CBtMfcGuiDlg message handlers

BOOL CBtMfcGuiDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

    // create the BlueTune player
    m_Player = new MfcPlayer(this);
    
	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	// TODO: Add extra initialization here
	m_Slider.SetRange(0, 500);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CBtMfcGuiDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CBtMfcGuiDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CBtMfcGuiDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CBtMfcGuiDlg::OnBnClickedCancel()
{
    // TODO: Add your control notification handler code here
    OnCancel();
    delete m_Player;
    m_Player = NULL;
}

void CBtMfcGuiDlg::OnBnClickedOpenButton()
{
    // open a file
	CFileDialog *dialog;

	dialog = new CFileDialog(TRUE, 
							"mp3",
							TEXT(""),
							OFN_FILEMUSTEXIST    | 
							OFN_HIDEREADONLY     |
							OFN_EXPLORER,
							TEXT("Audio Files|*.mpg;*.mp1;*.mp2;*.mp3;*.flac;*.ogg;*.wav;*.aif;*.aiff;*.mp4;*.m4a;*.wma|All Files|*.*||"));


	INT_PTR ret;
	ret = dialog->DoModal();
	if (ret == IDOK) {
        // a file was selected
		m_Player->SetInput(dialog->GetPathName());
	}

	delete dialog;
	
}

void CBtMfcGuiDlg::OnBnClickedPlayButton()
{
    m_Player->Play();
}

void CBtMfcGuiDlg::OnBnClickedPauseButton()
{
    m_Player->Pause();
}

void CBtMfcGuiDlg::OnBnClickedStopButton()
{
    m_Player->Stop();
}

void CBtMfcGuiDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (pScrollBar->m_hWnd == m_Slider.m_hWnd) {
        switch (nSBCode) {
          case SB_LINELEFT:
          case SB_LINERIGHT:
          case SB_PAGELEFT:
          case SB_PAGERIGHT:
            if (m_Player->m_Scrolling == FALSE) {
                m_Player->m_Scrolling = TRUE;
                //m_Player->Pause();
            }
            break;

          case SB_ENDSCROLL: 
            m_Player->SeekToPosition(m_Slider.GetPos(), m_Slider.GetRangeMax()-m_Slider.GetRangeMin());
            if (m_Player->m_Scrolling) {
                m_Player->m_Scrolling = FALSE; 
                //if (m_Player->m_State == XA_PLAYER_STATE_PLAYING) {
                //    m_Player->Play();
                //}
            }
            break;

          case SB_THUMBTRACK:
            if (m_Player->m_Scrolling == FALSE) {
                m_Player->m_Scrolling = TRUE;
                //m_Player->Pause();
            }
            break;

          case SB_THUMBPOSITION: 
            //m_Player->Seek(nPos, 400);
            break;
        }
    }

    CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}
