
// RemoteManDlg.cpp : 实现文件
//

#include "stdafx.h"
#include "RemoteMan.h"
#include "RemoteManDlg.h"
#include "afxdialogex.h"
#include "Aes.h"
#include "CodeConverter.h"
#include "FileDialogEx.h"

#pragma comment(lib, "Crypt32.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

char const InitTabSqlStr[] = "\
BEGIN TRANSACTION;\r\n\
CREATE TABLE %sConfigTab(\r\n\
id INTEGER primary key not null,\r\n\
GroupLastSelId int,\r\n\
Password char(66),\r\n\
ParentShowHost boolean,\r\n\
RadminPath char(256),\r\n\
SSHPath char(256),\r\n\
MstscConsole boolean,\r\n\
MstscUseDrive boolean,\r\n\
MstscLocalDrive char(24),\r\n\
MstscRemoteAudio boolean,\r\n\
MstscColor int,\r\n\
MstscWinpos int,\r\n\
MstscDeskImg boolean,\r\n\
MstscFontSmooth boolean,\r\n\
MstscThemes boolean,\r\n\
RadminCtrlMode int,\r\n\
RadminFullScreen boolean,\r\n\
RadminColor int\r\n\
);\r\n\
\r\n\
CREATE TABLE %sGroupTab(\r\n\
id INTEGER primary key AUTOINCREMENT,\r\n\
Name char(64) not null,\r\n\
ParentId int not null\r\n\
);\r\n\
\r\n\
CREATE TABLE %sHostTab(\r\n\
id INTEGER primary key AUTOINCREMENT,\r\n\
Name char(64) not null,\r\n\
ParentId int  not null,\r\n\
CtrlMode int  not null,\r\n\
HostAddress char(64) not null,\r\n\
HostPort int not null,\r\n\
Account char(20) not null,\r\n\
Password char(66) not null,\r\n\
HostReadme char(256)\r\n\
);\r\n\
insert into %sConfigTab values(0, 0,'',true,'','',true,true,'',false,0,0,false, false, true, 0, true, 1);\r\n\
COMMIT;\r\n\
";


//密码66字节说明：密码最大长度为32字节(16-31字节都是要扩充到32字节的)，使用ASCII存储=64字节
// 用于应用程序“关于”菜单项的 CAboutDlg 对话框

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// 对话框数据
	enum { IDD = IDD_ABOUTBOX };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

// 实现
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(CAboutDlg::IDD)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

char *CryptRDPPassword(char const *Password, char *OutPassword)
{
	DATA_BLOB DataIn ;
	DATA_BLOB DataOut ;
	// mstsc.exe中使用的是unicode,所以必须做宽字符转换
	DWORD n=0;
	BYTE wPassword[64];
	while (*Password)
	{
		wPassword[n++]=*Password++;
		wPassword[n++]=0;
	}
	wPassword[n]=0;

	DataIn . pbData = wPassword ;
	DataIn . cbData = n ;
	if ( CryptProtectData ( &DataIn , L"psw" , // A description string
		//to be included with the
		// encrypted data.
		NULL , // Optional entropy not used.
		NULL , // Reserved.
		NULL , // Pass NULL for the
		// prompt structure.
		CRYPTPROTECT_UI_FORBIDDEN,
		&DataOut ) )
	{
		int len=0;
		for (n=0; n<DataOut.cbData; n++)
		{
			len+=sprintf_s(OutPassword+len,512-len,"%02X",DataOut.pbData[n]);
		}
		return OutPassword;
	}
	return "";
}

// CRemoteManDlg 对话框
static int ReadGroupCallBack(void* para, int n_column, char** column_value, char** column_name)
{
	if (column_value[1]!=NULL)
	{
		CArray<GROUP_STRUCT ,GROUP_STRUCT&> *pGroupArray=(CArray<GROUP_STRUCT ,GROUP_STRUCT&>*)para;
		GROUP_STRUCT g;
		g.Id=atoi(column_value[0]);
		strcpy_s(g.Name,sizeof(g.Name),column_value[1]);
		pGroupArray->Add(g);
	}
	return 0;
}

void CRemoteManDlg::EnumTreeData(HTREEITEM hItem, int ParentNode)
{
	char sqlstr[64];
	CArray<GROUP_STRUCT ,GROUP_STRUCT&>GroupArray;
	GroupArray.SetSize(0,20);
	sprintf_s(sqlstr,sizeof(sqlstr),"select * from GroupTab where ParentId=%d;",ParentNode);
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, ReadGroupCallBack, &GroupArray, NULL);
	for (int i=0; i<GroupArray.GetSize();i++)
	{
		GROUP_STRUCT g=GroupArray[i];
		HTREEITEM hNewItem=m_Tree.InsertItem(g.Name,0,1,hItem);
		m_Tree.SetItemData(hNewItem,g.Id);
		EnumTreeData(hNewItem,g.Id);
		if (g.Id==SysConfig.GroupLastSelId) m_Tree.SelectItem(hNewItem);
	}
}

static int ReadConfigCallback(void* para, int n_column, char** column_value, char** column_name)
{
	CONFIG_STRUCT *pConfig = (CONFIG_STRUCT*)para;

	for (int i=0; i<n_column; i++)
	{
		if (column_value[i]==NULL) column_value[i]="";
		if (strcmp(column_name[i],"GroupLastSelId")==0)
			pConfig->GroupLastSelId =atoi(column_value[i]);
		else if (strcmp(column_name[i],"Password")==0)
			strcpy_s(pConfig->SysPassword,sizeof(pConfig->SysPassword), column_value[i]);
		else if (strcmp(column_name[i],"ParentShowHost")==0)
			pConfig->ParentShowHost=column_value[i][0]!='0' && column_value[i][0]!=0;
		else if (strcmp(column_name[i],"RadminPath")==0)
			strcpy_s(pConfig->RadminPath,sizeof(pConfig->RadminPath),column_value[i]);
		else if (strcmp(column_name[i],"SSHPath")==0)
			strcpy_s(pConfig->SSHPath,sizeof(pConfig->SSHPath),column_value[i]);
		else if (strcmp(column_name[i],"MstscConsole")==0)
			pConfig->MstscConsole=column_value[i][0]!='0' && column_value[i][0]!=0;
		else if (strcmp(column_name[i],"MstscUseDrive")==0)
			pConfig->MstscUseDrive=column_value[i][0]!='0' && column_value[i][0]!=0;
		else if (strcmp(column_name[i],"MstscLocalDrive")==0)
			strcpy_s(pConfig->MstscLocalDrive,sizeof(pConfig->MstscLocalDrive),column_value[i]);
		else if (strcmp(column_name[i],"MstscRemoteAudio")==0)
			pConfig->MstscRemoteAudio =column_value[i][0]!='0' && column_value[i][0]!=0;
		else if (strcmp(column_name[i],"MstscColor")==0)
			pConfig->MstscColor =atoi(column_value[i]);
		else if (strcmp(column_name[i],"MstscWinpos")==0)
			pConfig->MstscWinpos =atoi(column_value[i]);
		else if (strcmp(column_name[i],"MstscDeskImg")==0)
			pConfig->MstscDeskImg =column_value[i][0]!='0' && column_value[i][0]!=0;
		else if (strcmp(column_name[i],"MstscFontSmooth")==0)
			pConfig->MstscFontSmooth =column_value[i][0]!='0' && column_value[i][0]!=0;
		else if (strcmp(column_name[i],"MstscThemes")==0)
			pConfig->MstscThemes =column_value[i][0]!='0' && column_value[i][0]!=0;
		else if (strcmp(column_name[i],"RadminCtrlMode")==0)
			pConfig->RadminCtrlMode =atoi(column_value[i]);
		else if (strcmp(column_name[i],"RadminFullScreen")==0)
			pConfig->RadminFullScreen =column_value[i][0]!='0' && column_value[i][0]!=0;
		else if (strcmp(column_name[i],"RadminColor")==0)
			pConfig->RadminColor =atoi(column_value[i]);
	}

	return 0;
}

static int ReadIntCallback(void* para, int n_column, char** column_value, char** column_name)
{
	*(int*)para = atoi(column_value[0]);
	return 0;
}

bool CRemoteManDlg::OpenUserDb(char const *DbPath)
{
	int TabCnt=0;
	int rc = sqlite3_open(DbPath,&m_pDB);
	if (rc) return false;
	//检查配置表是否存在
	char const *sqlstr = "select count(type) from sqlite_master where tbl_name='ConfigTab';";
	TRACE("%s\r\n",sqlstr);
	rc = sqlite3_exec(m_pDB, sqlstr, ReadIntCallback, &TabCnt, NULL);
	//表不存在时创建表格
	if (TabCnt==0)
	{
		char sqlstr[1536];
		rc=sprintf_s(sqlstr,sizeof(sqlstr),InitTabSqlStr,"","","","");		//插入4个主数据库的名称
		TRACE("%s\r\n",sqlstr);
		rc=sqlite3_exec(m_pDB,sqlstr,NULL,NULL,NULL);
	}
	return rc==0;
}

CRemoteManDlg::CRemoteManDlg(CWnd* pParent /*=NULL*/)
	: CDialogEx(CRemoteManDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	char Path[MAX_PATH];
	GetModuleFileName(NULL,Path,MAX_PATH);
	char *p=strrchr(Path,'\\');
	if (p)
	{
		p[1]=0;
		strcat_s(Path,sizeof(Path),"User.db");
	}
	else
		strcpy_s(Path,sizeof(Path),"User.db");

	if (!OpenUserDb(CodeConverter::AsciiToUtf8(Path).c_str()))
	{
		char str[200];
		sprintf_s(str,sizeof(str),"打开数据库失败：%s",sqlite3_errmsg(m_pDB));
		AfxMessageBox(str);
		exit(0);
	}
	//读取参数
	memset(&SysConfig,0,sizeof(CONFIG_STRUCT));
	char const *sqlstr="select * from ConfigTab where id=0;";
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, ReadConfigCallback, &SysConfig, NULL);

	m_nListDragIndex=-1; 
	m_pDragImage=NULL;
}

CRemoteManDlg::~CRemoteManDlg()
{
	//更新最后打开的分组
	char sqlstr[128];
	int n=sprintf_s(sqlstr,sizeof(sqlstr),"update ConfigTab set GroupLastSelId=%d where id=0;",SysConfig.GroupLastSelId);
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);

	sqlite3_close(m_pDB); 
}

void CRemoteManDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_List);
	DDX_Control(pDX, IDC_TREE1, m_Tree);
}

BEGIN_MESSAGE_MAP(CRemoteManDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDOK, &CRemoteManDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDC_TOOLER_SET, &CRemoteManDlg::OnToolbarClickedSysSet)
	ON_BN_CLICKED(ID_MENU_ADDGROUP, &CRemoteManDlg::OnMenuClickedAddGroup)
	ON_BN_CLICKED(ID_MENU_DELGROUP, &CRemoteManDlg::OnMenuClickedDelGroup)
	ON_BN_CLICKED(ID_MENU_ADDHOST, &CRemoteManDlg::OnMenuClickedAddHost)
	ON_BN_CLICKED(ID_MENU_EDITHOST, &CRemoteManDlg::OnMenuClickedEditHost)
	ON_BN_CLICKED(ID_MENU_DELHOST, &CRemoteManDlg::OnMenuClickedDelHost)
	ON_BN_CLICKED(ID_MENU_CONNENT, &CRemoteManDlg::OnMenuClickedConnentHost)
	ON_BN_CLICKED(ID_MENU_RENAMEGROUP, &CRemoteManDlg::OnMenuClickedRenameGroup)
	ON_BN_CLICKED(ID_MENU_EXPORTGROUP,&CRemoteManDlg::OnMenuClickedExportGroup)
	ON_BN_CLICKED(ID_MENU_IMPORTGROUP,&CRemoteManDlg::OnMenuClickedImportGroup)
	ON_BN_CLICKED(IDC_TOOLER_OPENRADMIN, &CRemoteManDlg::OnToolbarClickedOpenRadmin)
	ON_BN_CLICKED(IDC_TOOLER_OPENMSTSC, &CRemoteManDlg::OnToolbarClickedOpenMstsc)
	ON_BN_CLICKED(IDC_TOOLER_OPENSSH, &CRemoteManDlg::OnToolbarClickedOpenSSH)
	ON_BN_CLICKED(IDC_CHECK_MST_CONSOLE, &CRemoteManDlg::OnBnClickedCheckMstConsole)
	ON_BN_CLICKED(IDC_CHECK_MST_DRIVE, &CRemoteManDlg::OnBnClickedCheckMstDrive)
	ON_BN_CLICKED(IDC_CHECK_MST_AUDIO, &CRemoteManDlg::OnBnClickedCheckMstAudio)
	ON_BN_CLICKED(IDC_CHECK_RADMIN_FULLSCREEN, &CRemoteManDlg::OnBnClickedCheckRadminFullscreen)
	ON_CBN_SELCHANGE(IDC_COMBO_MST_WINPOS, &CRemoteManDlg::OnCbnSelchangeComboMstWinpos)
	ON_CBN_SELCHANGE(IDC_COMBO_RADMIN_CTRLMODE, &CRemoteManDlg::OnCbnSelchangeComboRadminCtrlmode)
	ON_MESSAGE(WM_ADDHOST_MESSAGE, &CRemoteManDlg::OnAddHostMessage)
	ON_MESSAGE(WM_MODIFY_PASSWORD_MESSAGE, &CRemoteManDlg::OnModifyPasswordMessage)
	ON_NOTIFY(NM_RCLICK, IDC_TREE1, &CRemoteManDlg::OnNMRClickTree1)
	ON_NOTIFY(NM_RCLICK, IDC_LIST1, &CRemoteManDlg::OnNMRClickList1)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST1, &CRemoteManDlg::OnNMDblclkList1)
	ON_NOTIFY(TVN_SELCHANGED, IDC_TREE1, &CRemoteManDlg::OnTvnSelchangedTree1)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST1, &CRemoteManDlg::OnLvnItemchangedList1)
	ON_NOTIFY(LVN_BEGINDRAG, IDC_LIST1, &CRemoteManDlg::OnLvnBegindragList1)
	ON_NOTIFY(TVN_ENDLABELEDIT, IDC_TREE1, &CRemoteManDlg::OnTvnEndlabeleditTree1)
	ON_COMMAND_RANGE(ID_MENU_FULLCTRL,ID_MENU_CLOSEHOST,&CRemoteManDlg::OnMenuClickedRadminCtrl)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
END_MESSAGE_MAP()

//TVN_ENDLABELEDIT 删除这行会不能设置断点，不信你试试


// CRemoteManDlg 消息处理程序
void CRemoteManDlg::InitToolBar(void)
{
	m_ToolbarImageList.Create(32,32,ILC_COLOR24|ILC_MASK,1,1);
	m_ToolbarImageList.SetBkColor(RGB(255,255,255));
	m_ToolbarImageList.Add(AfxGetApp()->LoadIcon(IDI_ADDNODE));
	m_ToolbarImageList.Add(AfxGetApp()->LoadIcon(IDI_DELNODE));
	m_ToolbarImageList.Add(AfxGetApp()->LoadIcon(IDI_PC));
	m_ToolbarImageList.Add(AfxGetApp()->LoadIcon(IDI_EDIT));
	m_ToolbarImageList.Add(AfxGetApp()->LoadIcon(IDI_DEL));
	m_ToolbarImageList.Add(AfxGetApp()->LoadIcon(IDI_OPEN));
	m_ToolbarImageList.Add(AfxGetApp()->LoadIcon(IDR_MAINFRAME));
	m_ToolbarImageList.Add(AfxGetApp()->LoadIcon(IDI_RADMIN));
	m_ToolbarImageList.Add(AfxGetApp()->LoadIcon(IDI_SSH));
	m_ToolbarImageList.Add(AfxGetApp()->LoadIcon(IDI_SET));

	UINT array[13]={ID_MENU_ADDGROUP,ID_MENU_DELGROUP,ID_SEPARATOR,
					ID_MENU_ADDHOST,ID_MENU_EDITHOST,ID_MENU_DELHOST,ID_SEPARATOR,
					ID_MENU_CONNENT,IDC_TOOLER_OPENMSTSC,IDC_TOOLER_OPENRADMIN,IDC_TOOLER_OPENSSH,ID_SEPARATOR,
					IDC_TOOLER_SET};
	m_ToolBar.Create(this);
	m_ToolBar.SetButtons(array,13);
	m_ToolBar.SetButtonText(0,"添加分组");
	m_ToolBar.SetButtonText(1,"删除分组");
	m_ToolBar.SetButtonText(3,"添加主机");
	m_ToolBar.SetButtonText(4,"编辑主机");
	m_ToolBar.SetButtonText(5,"删除主机");
	m_ToolBar.SetButtonText(7,"连接");
	m_ToolBar.SetButtonText(8,"远程桌面");
	m_ToolBar.SetButtonText(9,"Radmin");
	m_ToolBar.SetButtonText(10,"SecureCRT");
	m_ToolBar.SetButtonText(12,"设置");

	m_ToolBar.GetToolBarCtrl().SetImageList(&m_ToolbarImageList);
	m_ToolBar.SetSizes(CSize(72,56),CSize(20,34));

	RepositionBars(AFX_IDW_CONTROLBAR_FIRST,AFX_IDW_CONTROLBAR_LAST,0);
}

BOOL CRemoteManDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 将“关于...”菜单项添加到系统菜单中。

	// IDM_ABOUTBOX 必须在系统命令范围内。
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// 设置此对话框的图标。当应用程序主窗口不是对话框时，框架将自动
	//  执行此操作
	SetIcon(m_hIcon, TRUE);			// 设置大图标
	SetIcon(m_hIcon, FALSE);		// 设置小图标

	// TODO: 在此添加额外的初始化代码
	HICON hico=(HICON)LoadImage(AfxGetApp()->m_hInstance,MAKEINTRESOURCE(IDI_MSTSC),IMAGE_ICON,64,64,LR_DEFAULTCOLOR);
	((CStatic*)GetDlgItem(IDC_STATIC_PIC))->SetIcon(hico);

	InitToolBar();

	m_ImageList.Create(24,24,ILC_COLOR24|ILC_MASK,1,1);
	m_ImageList.SetBkColor(RGB(255,255,255));
	m_ImageList.Add(AfxGetApp()->LoadIcon(IDI_NODE_CLOSE));
	m_ImageList.Add(AfxGetApp()->LoadIcon(IDI_NODE_OPEN));
	m_ImageList.Add(AfxGetApp()->LoadIcon(IDR_MAINFRAME));
	m_ImageList.Add(AfxGetApp()->LoadIcon(IDI_RADMIN));
	m_ImageList.Add(AfxGetApp()->LoadIcon(IDI_SSH));
	m_Tree.SetImageList(&m_ImageList,LVSIL_NORMAL);

	m_List.SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
	m_List.SetImageList(&m_ImageList,LVSIL_SMALL);
	m_List.InsertColumn(0,"类型",LVCFMT_LEFT,80);
	m_List.InsertColumn(1,"服务器名称",LVCFMT_LEFT,168);
	m_List.InsertColumn(2,"域名",LVCFMT_LEFT,145);
	m_List.InsertColumn(3,"端口",LVCFMT_LEFT,64);
	m_List.InsertColumn(4,"账户",LVCFMT_LEFT,110);

	((CButton*)GetDlgItem(IDC_CHECK_MST_CONSOLE))->SetCheck(SysConfig.MstscConsole);
	((CButton*)GetDlgItem(IDC_CHECK_MST_DRIVE))->SetCheck(SysConfig.MstscUseDrive);
	((CButton*)GetDlgItem(IDC_CHECK_MST_AUDIO))->SetCheck(SysConfig.MstscRemoteAudio);
	((CComboBox*)GetDlgItem(IDC_COMBO_MST_WINPOS))->SetCurSel(SysConfig.MstscWinpos);
	((CButton*)GetDlgItem(IDC_CHECK_RADMIN_FULLSCREEN))->SetCheck(SysConfig.RadminFullScreen);
	((CComboBox*)GetDlgItem(IDC_COMBO_RADMIN_CTRLMODE))->SetCurSel(SysConfig.RadminCtrlMode);

	//读取分组
	EnumTreeData(TVI_ROOT,0);
	if (m_Tree.GetCount()==0)
		SetDlgItemText(IDC_EDIT_README,"请先添加分组.");

	return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}

void CRemoteManDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// 如果向对话框添加最小化按钮，则需要下面的代码
//  来绘制该图标。对于使用文档/视图模型的 MFC 应用程序，
//  这将由框架自动完成。

void CRemoteManDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 用于绘制的设备上下文

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 使图标在工作区矩形中居中
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 绘制图标
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

//当用户拖动最小化窗口时系统调用此函数取得光标
//显示。
HCURSOR CRemoteManDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}



void CRemoteManDlg::OnBnClickedOk()
{
	// TODO: 在此添加控件通知处理程序代码
//	CDialogEx::OnOK();
} 



HBRUSH CRemoteManDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);

	// TODO:  在此更改 DC 的任何特性 
	return hbr;
}


void CRemoteManDlg::OnToolbarClickedSysSet(void)
{
	CSysSetDlg Dlg(SysConfig.ParentShowHost, SysConfig.MstscLocalDrive, SysConfig.MstscColor, SysConfig.MstscDeskImg,
		SysConfig.MstscFontSmooth, SysConfig.MstscThemes, SysConfig.RadminColor, SysConfig.RadminPath, SysConfig.SSHPath);
	if (Dlg.DoModal()==IDOK)
	{
		SysConfig.ParentShowHost=Dlg.m_ParentShowHost!=0;
		strcpy_s(SysConfig.RadminPath,sizeof(SysConfig.RadminPath),Dlg.m_RadminPath);
		strcpy_s(SysConfig.SSHPath,sizeof(SysConfig.SSHPath),Dlg.m_SshPath);
		strcpy_s(SysConfig.MstscLocalDrive,sizeof(SysConfig.MstscLocalDrive),Dlg.m_MstDriveStr);
		SysConfig.MstscColor=Dlg.m_MstColor;
		SysConfig.MstscDeskImg=Dlg.m_MstShowDeskImg!=0;
		SysConfig.MstscFontSmooth=Dlg.m_MstFontSmooth!=0;
		SysConfig.MstscThemes=Dlg.m_MstThemes!=0;
		SysConfig.RadminColor=Dlg.m_RadminColor;
		
		char sqlstr[1024];
		int n=sprintf_s(sqlstr,sizeof(sqlstr),"update ConfigTab set ParentShowHost=%s,RadminPath='%s',SSHPath='%s',MstscLocalDrive='%s',"
											  "MstscColor=%d,MstscDeskImg=%s,MstscFontSmooth=%s,MstscThemes=%s,RadminColor=%d where id=0;",
			SysConfig.ParentShowHost ? "true":"false",
			SysConfig.RadminPath,
			SysConfig.SSHPath,
			SysConfig.MstscLocalDrive,
			SysConfig.MstscColor,
			SysConfig.MstscDeskImg ? "true":"false",
			SysConfig.MstscFontSmooth ? "true":"false",
			SysConfig.MstscThemes ? "true":"false",
			SysConfig.RadminColor
			);
		TRACE("%s\r\n",sqlstr);
		int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	}
}

void CRemoteManDlg::OnMenuClickedAddGroup(void)
{
	CString GroupName;
	HTREEITEM hItem = m_Tree.GetSelectedItem();
	if (hItem!=NULL)
		GroupName=m_Tree.GetItemText(hItem);

	CAddGroupDlg dlg(GroupName);
	if (dlg.DoModal()==IDOK)
	{
		int ParentId=0;
		if (dlg.m_AddRoot)
			hItem=TVI_ROOT;
		else
			ParentId=m_Tree.GetItemData(hItem);
		//先判断这个父分组下是否有这个名称的分组存在
		int GroupCnt=0;
		char sqlstr[128];
		sprintf_s(sqlstr,sizeof(sqlstr),"select count() from GroupTab where ParentId=%d and Name='%s';",ParentId,dlg.m_GroupName);
		TRACE("%s\r\n",sqlstr);		
		int rc = sqlite3_exec(m_pDB, sqlstr, ReadIntCallback, &GroupCnt, NULL);
		if (GroupCnt>0)
		{
			MessageBox("该分组名已经存在","错误",MB_ICONERROR);
			return;
		}
		//添加到数据库
		sprintf_s(sqlstr,sizeof(sqlstr),"insert into GroupTab values(NULL,'%s',%d);",dlg.m_GroupName,ParentId);
		TRACE("%s\r\n",sqlstr);
		rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
		//读取这条数据的ID
		sprintf_s(sqlstr,sizeof(sqlstr),"select id from GroupTab where ParentId=%d and Name='%s';",ParentId,dlg.m_GroupName);
		TRACE("%s\r\n",sqlstr);
		int Id=0;
		rc = sqlite3_exec(m_pDB, sqlstr, ReadIntCallback, &Id, NULL);
		hItem=m_Tree.InsertItem(dlg.m_GroupName,0,1,hItem);
		m_Tree.SetItemData(hItem,Id);
		m_Tree.SelectItem(hItem);
	}
}

void CRemoteManDlg::EnumChildGroupId(HTREEITEM hItem,CArray<int ,int>&GroupArray)
{
	HTREEITEM hChildItem;
	GroupArray.Add(m_Tree.GetItemData(hItem));
	//查找子分组及兄弟分组
	hChildItem=m_Tree.GetChildItem(hItem);
	while (hChildItem!=0)
	{
		if (hChildItem==0) return;
		EnumChildGroupId(hChildItem,GroupArray);
		hChildItem=m_Tree.GetNextSiblingItem(hChildItem);
	}
}

void CRemoteManDlg::OnMenuClickedDelGroup(void)
{
	HTREEITEM hItem=m_Tree.GetSelectedItem();
	if (hItem==0) return;
	if (MessageBox("删除分组将同时删除子分组及对应主机，确认删除？","注意",MB_OKCANCEL|MB_ICONWARNING)!=IDOK) return;
	CArray<int ,int>GroupArray;
	GroupArray.SetSize(0,20);
	//枚举组ID
	EnumChildGroupId(hItem,GroupArray);
	//删除组
	int sqlstrlen=25+GroupArray.GetSize()*23;
	char *sqlstr=new char[sqlstrlen];
	int len=sprintf_s(sqlstr,sqlstrlen,"delete from GroupTab where id=%d",GroupArray[0]);
	for (int i=1; i<GroupArray.GetSize(); i++)
	{
		len+=sprintf_s(sqlstr+len,sqlstrlen-len," or id=%d",GroupArray[i]);
	}
	sqlstr[len]=';';
	sqlstr[len+1]=0;
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	//删除主机
	len=sprintf_s(sqlstr,sqlstrlen,"delete from HostTab where ParentId=%d",GroupArray[0]);
	for (int i=1; i<GroupArray.GetSize(); i++)
	{
		len+=sprintf_s(sqlstr+len,sqlstrlen-len," or ParentId=%d",GroupArray[i]);
	}
	sqlstr[len]=';';
	sqlstr[len+1]=0;
	TRACE("%s\r\n",sqlstr);
	rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	delete sqlstr;
	//删除树控件节点和列表框
	SetDlgItemText(IDC_EDIT_README,"");
	m_List.DeleteAllItems();
	m_Tree.DeleteItem(hItem);
}

afx_msg LRESULT CRemoteManDlg::OnAddHostMessage(WPARAM wParam, LPARAM lParam)
{
	HOST_STRUCT *pHost = (HOST_STRUCT*)wParam;
	int ParentId=m_Tree.GetItemData(HTREEITEM(lParam));

	char sqlstr[512],str[68];
	//查看该主机名是否存在
	int HostCnt=0;
	sprintf_s(sqlstr,sizeof(sqlstr),"select count() from HostTab where ParentId=%d and Name='%s' and CtrlMode=%d;",
		ParentId, pHost->Name, pHost->CtrlMode);
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, ReadIntCallback, &HostCnt, NULL);
	if (HostCnt>0)
	{
		MessageBox("该主机名已存在.","错误",MB_ICONERROR);
		return 1;
	}
	//添加到数据库
	sprintf_s(sqlstr,sizeof(sqlstr),"insert into HostTab values(NULL,'%s',%d,%d,'%s',%d,'%s','%s','%s');", 	pHost->Name, ParentId, 
		pHost->CtrlMode, pHost->HostAddress, pHost->HostPort, pHost->Account, 
		AesEnCodeToStr(pHost->Password,strlen(pHost->Password),str,AES_KEY), pHost->ReadMe);
	TRACE("%s\r\n",sqlstr);
	rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	//读取ID
	int Id=0;
	sprintf_s(sqlstr,sizeof(sqlstr),"select id from HostTab where ParentId=%d and Name='%s';",ParentId,pHost->Name);
	TRACE("%s\r\n",sqlstr);
	rc = sqlite3_exec(m_pDB, sqlstr, ReadIntCallback, &Id, NULL);
	//添加到列表
	ListAddHost(pHost,Id);

	return 0;
}

void CRemoteManDlg::OnMenuClickedAddHost(void)
{
	HTREEITEM hItem=m_Tree.GetSelectedItem();
	if (hItem==NULL || m_Tree.GetCount()==0)
	{
		MessageBox("请先添加分组或选择分组.","错误",MB_ICONERROR);
		return;
	}
	CAddHostDlg Dlg(NULL, hItem);
	Dlg.DoModal();
}

void CRemoteManDlg::ListAddHost(HOST_STRUCT const * pHost, int Id)
{
	char str[12];
	int n=m_List.GetItemCount();
	m_List.InsertItem(n, CTRL_MODE[pHost->CtrlMode],2+pHost->CtrlMode);
	m_List.SetItemText(n, 1, pHost->Name);
	m_List.SetItemText(n, 2, pHost->HostAddress);
	sprintf_s(str,sizeof(str),"%d",pHost->HostPort);
	m_List.SetItemText(n, 3, str);
	m_List.SetItemText(n, 4, pHost->Account);
	m_List.SetItemData(n,Id);
}

static int ReadHostCallback(void* para, int n_column, char** column_value, char** column_name)
{
	CArray<HOST_STRUCT,HOST_STRUCT&>*pHostArray=(CArray<HOST_STRUCT,HOST_STRUCT&>*)para;

	HOST_STRUCT Host;
	memset(&Host,0,sizeof(Host));
	Host.Id=atoi(column_value[0]);										//Id
	strcpy_s(Host.Name,sizeof(Host.Name),column_value[1]);				//主机名称
	Host.CtrlMode=atoi(column_value[3]);								//控制类型
	strcpy_s(Host.HostAddress,sizeof(Host.HostAddress),column_value[4]);//主机地址
	Host.HostPort=atoi(column_value[5]);								//端口
	strcpy_s(Host.Account,sizeof(Host.Account),column_value[6]);		//帐号
	if (column_value[7]!=NULL)											//密码
		strcpy_s(Host.Password,sizeof(Host.Password),column_value[7]);
	if (column_value[8]!=NULL)											//说明
		strcpy_s(Host.ReadMe,sizeof(Host.ReadMe),column_value[8]);
	pHostArray->Add(Host);
	return 0;
}

void CRemoteManDlg::OnMenuClickedEditHost(void)
{
	if (m_List.GetSelectedCount()!=1) return;
	int n=m_List.GetSelectionMark();
	int Id=m_List.GetItemData(n);

	char sqlstr[512],str[68];
	CArray<HOST_STRUCT,HOST_STRUCT&>HostArray;
	sprintf_s(sqlstr,sizeof(sqlstr),"select * from HostTab where Id=%d;",Id);
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, ReadHostCallback, &HostArray, NULL);
	if (HostArray.GetSize()!=1) return;
	HOST_STRUCT Host=HostArray[0];

	CAddHostDlg Dlg(&Host, 0);
	if (Dlg.DoModal()!=IDOK) return;
	
	//更新数据库
	if (Dlg.IsPasswordChange)
	{
		sprintf_s(sqlstr,sizeof(sqlstr),"update HostTab set Name='%s',CtrlMode=%d,HostAddress='%s',HostPort=%d,Account='%s',"
			"Password='%s',HostReadme='%s' where id=%d;",
			Dlg.m_Host.Name, Dlg.m_Host.CtrlMode, Dlg.m_Host.HostAddress, Dlg.m_Host.HostPort, Dlg.m_Host.Account, 
			AesEnCodeToStr(Dlg.m_Host.Password,strlen(Dlg.m_Host.Password),str,AES_KEY), Dlg.m_Host.ReadMe,Id);
	}
	else
	{
		sprintf_s(sqlstr,sizeof(sqlstr),"update HostTab set Name='%s',CtrlMode=%d,HostAddress='%s',HostPort=%d,Account='%s',"
			"HostReadme='%s' where id=%d;",
			Dlg.m_Host.Name, Dlg.m_Host.CtrlMode, Dlg.m_Host.HostAddress, Dlg.m_Host.HostPort, 
			Dlg.m_Host.Account, Dlg.m_Host.ReadMe,Id);
	}
	TRACE("%s\r\n",sqlstr);
	rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	//更新列表框
	m_List.SetItem(n,0,LVIF_IMAGE,NULL,2+Dlg.m_Host.CtrlMode,0,0,0);
	m_List.SetItemText(n,0,CTRL_MODE[Dlg.m_Host.CtrlMode]);
	m_List.SetItemText(n,1,Dlg.m_Host.Name);
	m_List.SetItemText(n,2,Dlg.m_Host.HostAddress);
	sprintf_s(sqlstr,sizeof(sqlstr),"%d",Dlg.m_Host.HostPort);
	m_List.SetItemText(n,3,sqlstr);
	m_List.SetItemText(n,4,Dlg.m_Host.Account);
	SetDlgItemText(IDC_EDIT_README,Dlg.m_Host.ReadMe);
	
	m_List.SetFocus();
}


void CRemoteManDlg::OnMenuClickedDelHost(void)
{
	int Cnt=m_List.GetSelectedCount();
	if (Cnt==0) return;
	int *Sels=new int[Cnt];
	int *Ids=new int[Cnt];
	//列出选择的项和ID
	POSITION  pos=m_List.GetFirstSelectedItemPosition();
	for (int i=0; pos!=NULL && i<Cnt; i++)
	{
		Sels[i]=m_List.GetNextSelectedItem(pos);
		Ids[i]=m_List.GetItemData(Sels[i]);
	}
	//从后开始删除列表
	for (int i=Cnt-1; i>=0; i--)
		m_List.DeleteItem(Sels[i]);
	//删除数据库
	int sqlstrlen=30+Cnt*17;
	char *sqlstr=new char[sqlstrlen];
	int len=sprintf_s(sqlstr,sqlstrlen,"delete from HostTab where ");
	for (int i=0; i<Cnt; i++)
		len+=sprintf_s(sqlstr+len,sqlstrlen-len, i==0 ? "id=%d":" or id=%d",Ids[i]);
	sqlstr[len]=';';
	sqlstr[len+1]=0;
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	//
	delete Sels;
	delete Ids;
	delete sqlstr;
}


void CRemoteManDlg::OnToolbarClickedOpenMstsc(void)
{
	char szBuffer[MAX_PATH];
	SHGetSpecialFolderPath(NULL, szBuffer, CSIDL_SYSTEM, FALSE);
	strcat_s(szBuffer,MAX_PATH,"\\Mstsc.exe");
	WinExec(szBuffer,WM_SHOWWINDOW);
}


void CRemoteManDlg::OnToolbarClickedOpenRadmin(void)
{
	char const *RadminPath="radmin.exe";			//当路径为空时使用同目录下的radmin.exe
	if (SysConfig.RadminPath[0]!=0) RadminPath=SysConfig.RadminPath;
	//查看文件是否存在
	CFileStatus fstatus;
	if (strstr(RadminPath,".exe")==NULL || !CFile::GetStatus(RadminPath,fstatus))
	{
//		AfxMessageBox("Radmin路径设置错误");
		return;
	}
	WinExec(RadminPath,WM_SHOWWINDOW);
}


void CRemoteManDlg::OnToolbarClickedOpenSSH(void)
{
	if (strstr(SysConfig.SSHPath,".exe"))
		WinExec(SysConfig.SSHPath,WM_SHOWWINDOW);
}


void CRemoteManDlg::MstscConnent(HOST_STRUCT const *pHost, CONFIG_STRUCT const *pConfig)
{
	char RdpStr[1536],str[512];
	//连接模式
	int len=sprintf_s(RdpStr,sizeof(RdpStr),"screen mode id:i:%d\r\n",pConfig->MstscWinpos==0?2:1);
	//宽高
	int Width=1024,Height=768;
	if (pConfig->MstscWinpos!=0)
	{
		((CComboBox*)GetDlgItem(IDC_COMBO_MST_WINPOS))->GetLBText(pConfig->MstscWinpos,str);
		char *p=strchr(str,' ');
		*p=0;
		p+=4;
		Width=atoi(str);
		Height=atoi(p);
	}
	len+=sprintf_s(RdpStr+len,sizeof(RdpStr)-len,"desktopwidth:i:%d\r\ndesktopheight:i:%d\r\n",Width,Height);
	//颜色位数
	len+=sprintf_s(RdpStr+len,sizeof(RdpStr)-len,"session bpp:i:%d\r\n",pConfig->MstscColor==0 ? 16:pConfig->MstscColor==1 ? 24:32);
	//默认数据
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"winposstr:s:0,1,0,0,1024,768\r\n");
	len+=sizeof("winposstr:s:0,1,0,0,1024,768\r\n")-1;
	//远程服务器地址
	if (pHost->HostPort==3389)
		len+=sprintf_s(RdpStr+len,sizeof(RdpStr)-len,"full address:s:%s\r\n",pHost->HostAddress);
	else
		len+=sprintf_s(RdpStr+len,sizeof(RdpStr)-len,"full address:s:%s:%d\r\n",pHost->HostAddress,pHost->HostPort);
	//将数据传输到客户端计算机时是否对数据进行压缩
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"compression:i:1\r\n");
	len+=sizeof("compression:i:1\r\n")-1;
	//确定何时将 Windows 键组合应用到桌面连接的远程会话
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"keyboardhook:i:2\r\n");
	len+=sizeof("keyboardhook:i:2\r\n")-1;
	//声音设置
	len+=sprintf_s(RdpStr+len,sizeof(RdpStr)-len,"audiomode:i:%d\r\n",pConfig->MstscRemoteAudio ? 0:2);
	//剪贴板 
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"redirectclipboard:i:1\r\n");
	len+=sizeof("redirectclipboard:i:1\r\n")-1;
	//PnP即插即用设备
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"devicestoredirect:s:\r\n");
	len+=sizeof("devicestoredirect:s:\r\n")-1;
	//磁盘驱动器
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"drivestoredirect:s:");
	len+=sizeof("drivestoredirect:s:")-1;
	if (pConfig->MstscUseDrive)
	{
		char volume[]="C:\\";
		for (int i=0; pConfig->MstscLocalDrive[i]>='A' && pConfig->MstscLocalDrive[i]<='Z'; i++)
		{
			volume[0]=pConfig->MstscLocalDrive[i];
			GetVolumeInformation(volume,str,16,NULL,NULL,NULL,NULL,0);
			len+=sprintf_s(RdpStr+len,sizeof(RdpStr)-len,"%s (%c:);",str,pConfig->MstscLocalDrive[i]);
		}
	}
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"\r\n");
	len+=2;
	//是否自动连接打印机
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"redirectprinters:i:0\r\n");
	len+=sizeof("redirectprinters:i:0\r\n")-1;
	//是否自动连接COM串行口
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"redirectcomports:i:0\r\n");
	len+=sizeof("redirectcomports:i:0\r\n")-1;
	//是否自动连接智能卡
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"redirectsmartcards:i:0\r\n");
	len+=sizeof("redirectsmartcards:i:0\r\n")-1;
	//全屏模式时是否显示连接栏
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"displayconnectionbar:i:1\r\n");
	len+=sizeof("displayconnectionbar:i:1\r\n")-1;
	//在断开连接后是否自动尝试重新连接
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"autoreconnection enabled:i:1\r\n");
	len+=sizeof("autoreconnection enabled:i:1\r\n")-1;
	//用户名和域
	len+=sprintf_s(RdpStr+len,sizeof(RdpStr)-len,"username:s:%s\r\n",pHost->Account);
	//指定要在远程会话中作为 shell（而不是资源管理器）自动启动的程序
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"alternate shell:s:\r\n");
	len+=sizeof("alternate shell:s:\r\n")-1;
	////RDP进行连接时自动启动的应用程序所在的文件夹位置程
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"shell working directory:s:\r\n");
	len+=sizeof("shell working directory:s:\r\n")-1;
	//RDP密码加密数据
	if (pHost->Password[0]!=0)
		len+=sprintf_s(RdpStr+len,sizeof(RdpStr)-len,"password 51:b:%s\r\n",CryptRDPPassword(pHost->Password,str));
	//是否禁止显示桌面背景
	len+=sprintf_s(RdpStr+len,sizeof(RdpStr)-len,"disable wallpaper:i:%d\r\n",pConfig->MstscDeskImg?0:1);
	//是否禁止主题
	len+=sprintf_s(RdpStr+len,sizeof(RdpStr)-len,"disable themes:i:%d\r\n",pConfig->MstscThemes?0:1);
	//是否允许字体平滑
	len+=sprintf_s(RdpStr+len,sizeof(RdpStr)-len,"allow font smoothing:i:%d\r\n",pConfig->MstscFontSmooth?1:0);
	//将文件夹拖到新位置时是否显示文件夹内容
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"disable full window drag:i:1\r\n");
	len+=sizeof("disable full window drag:i:1\r\n")-1;
	//禁用菜单动画
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"disable menu anims:i:1\r\n");
	len+=sizeof("disable menu anims:i:1\r\n")-1;
	//禁用光标设置
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"disable cursor setting:i:0\r\n");
	len+=sizeof("disable cursor setting:i:0\r\n")-1;
	//是否将位图缓存在本地计算机上
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"bitmapcachepersistenable:i:1\r\n");
	len+=sizeof("bitmapcachepersistenable:i:1\r\n")-1;
	//定义服务器身份验证级别设置
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"authentication level:i:0\r\n");
	len+=sizeof("authentication level:i:0\r\n")-1;
	//?
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"prompt for credentials:i:0\r\n");
	len+=sizeof("prompt for credentials:i:0\r\n")-1;
	//确定是否保存用户的凭据并将其用于 RD 网关和远程计算机
	strcpy_s(RdpStr+len,sizeof(RdpStr)-len,"promptcredentialonce:i:1\r\n");
	len+=sizeof("promptcredentialonce:i:1\r\n")-1;

	//存储到文件,借用str变量
	GetTempPath(sizeof(str),str);
	strcat_s(str,sizeof(str),"rdp_tmp");
//	sprintf_s(str,sizeof(str),"c:\\%s_RDP.rdp",pHost->HostAddress);
	CFile file;
	if (!file.Open(str,CFile::modeCreate|CFile::modeWrite|CFile::typeBinary)) return;
	file.Write(RdpStr,len);
	file.Close();

	//启动
	char szBuffer[MAX_PATH];
	SHGetSpecialFolderPath(NULL, szBuffer, CSIDL_SYSTEM, FALSE);
	sprintf_s(RdpStr,sizeof(RdpStr),"%s\\Mstsc.exe /%s %s", szBuffer,pConfig->MstscConsole?"console":"admin",str);	//命令行
	WinExec(RdpStr, WM_SHOWWINDOW);
	TRACE("Rdp文件长度=%d Rdp命令行:%s\r\n",len,RdpStr);
	Sleep(1000);
	DeleteFile(str);
}

void RadminConnent(HOST_STRUCT const *pHost, CONFIG_STRUCT const *pConfig, int CtrlMode)
{
	static const char MODE[][10]={"","/noinput","/file","/shutdown"};
	static const char COLOUR[][8]={"/8bpp","/16bpp","/24bpp"};
	char const *RadminPath="radmin.exe";			//当路径为空时使用同目录下的radmin.exe
	if (pConfig->RadminPath[0]!=0) RadminPath=pConfig->RadminPath;
	//查看文件是否存在
	CFileStatus fstatus;
	if (strstr(RadminPath,".exe")==NULL || !CFile::GetStatus(RadminPath,fstatus))
	{
		AfxMessageBox("Radmin路径设置错误");
		return;
	}

	char str1[100],str2[30];
	//启动Radmin连接服务器
	if (pHost->HostPort==4899)
		sprintf_s(str1,sizeof(str1),"/connect:%s %s %s",pHost->HostAddress,MODE[CtrlMode],COLOUR[pConfig->RadminColor]);
	else
		sprintf_s(str1,sizeof(str1),"/connect:%s:%d %s %s",pHost->HostAddress,pHost->HostPort,MODE[CtrlMode],COLOUR[pConfig->RadminColor]);
	if (pConfig->RadminFullScreen)
		strcat_s(str1,sizeof(str1)," /fullscreen");
	TRACE("%s\r\n",str1);
	ShellExecute(NULL,"open",RadminPath,str1,NULL,SW_SHOW);
	//窗口名称
	sprintf_s(str1,sizeof(str1),"Radmin 安全性：%s",pHost->HostAddress);	//Radmin2.2的窗口名称
	sprintf_s(str2,sizeof(str2),"Radmin 安全性: %s",pHost->HostAddress);	//Radmin3.4的窗口名称
	//查找Radmin启动窗口
	HWND hWnd;
	clock_t t2,t1=clock();
	for (t2=t1;t2-t1<8000;t2=clock())
	{
		Sleep(1);
		hWnd=FindWindow(NULL,str1);
		if (hWnd!=NULL)
			break;
		hWnd=FindWindow(NULL,str2);
		if (hWnd!=NULL)
			break;
	}
	if (hWnd==NULL)	return;
	//填写信息并发送
	HWND UserWnd=::GetDlgItem(hWnd,0x7ff);
	HWND PasswordWnd=::GetDlgItem(hWnd,0x800);
	if (UserWnd!=NULL)
		::SendMessage(UserWnd,WM_SETTEXT,0,(LPARAM)pHost->Account);
	if (PasswordWnd!=NULL)
		::SendMessage(PasswordWnd,WM_SETTEXT,0,(LPARAM)pHost->Password);
	::PostMessage(hWnd,WM_COMMAND,0x78,0);
}

void SSHConnent(HOST_STRUCT const *pHost, CONFIG_STRUCT const *pConfig)
{
	//查看文件是否存在
	CFileStatus fstatus;
	if (strstr(pConfig->SSHPath,".exe")==NULL || !CFile::GetStatus(pConfig->SSHPath,fstatus))
	{
		AfxMessageBox("SSH路径设置错误");
		return;
	}
	//连接
	char str[128];
	if (pHost->HostPort==22)
	{
		sprintf_s(str,sizeof(str),"%s /ssh2 %s@%s /PASSWORD %s",
			pConfig->SSHPath,pHost->Account,pHost->HostAddress,pHost->Password);
	}
	else
	{
		sprintf_s(str,sizeof(str),"%s /ssh2 %s@%s /P %d /PASSWORD %s",
			pConfig->SSHPath,pHost->Account,pHost->HostAddress,pHost->HostPort,pHost->Password);
	}
	WinExec(str,WM_SHOWWINDOW);
}

void CRemoteManDlg::ConnentHost(int RadminCtrlMode)
{
	if (m_List.GetSelectedCount()!=1) return;
	int n=m_List.GetSelectionMark();
	//获取主机信息
	char sqlstr[128];
	CArray<HOST_STRUCT,HOST_STRUCT&>HostArray;
	sprintf_s(sqlstr,sizeof(sqlstr),"select * from HostTab where Id=%d;",m_List.GetItemData(n));
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, ReadHostCallback, &HostArray, NULL);
	if (HostArray.GetSize()!=1) return;
	HOST_STRUCT Host=HostArray[0];
	if (Host.Password[0]!=0)
	{
		byte data[36];
		int len=StringToBytes(Host.Password,data);
		if (len>0)
		{
			len=AesDeCode(data,len,AES_KEY);
			if (len>0) strcpy_s(Host.Password,sizeof(Host.Password),(char*)data);
		}
	}
	//
	if (strcmp(CTRL_MODE[Host.CtrlMode],CTRL_MODE_RDP_NAME)==0)
		MstscConnent(&Host,&SysConfig);
	else if (strcmp(CTRL_MODE[Host.CtrlMode],CTRL_MODE_RADMIN_NAME)==0)
		RadminConnent(&Host,&SysConfig,RadminCtrlMode);
	else if (strcmp(CTRL_MODE[Host.CtrlMode],CTRL_MODE_SSH_NAME)==0)
		SSHConnent(&Host,&SysConfig);
	else if (strcmp(CTRL_MODE[Host.CtrlMode],CTRL_MODE_VNC_NAME)==0)
	{

	}
}

void CRemoteManDlg::OnMenuClickedConnentHost(void)
{
	ConnentHost(SysConfig.RadminCtrlMode);
}

void CRemoteManDlg::OnMenuClickedRadminCtrl(UINT Id)
{
	ConnentHost(Id-ID_MENU_FULLCTRL);
}

void CRemoteManDlg::OnMenuClickedRenameGroup(void)
{
	HTREEITEM nItem=m_Tree.GetSelectedItem();
	if (nItem==NULL)
		return;
	m_Tree.EditLabel(nItem);
}

void CRemoteManDlg::OnTvnEndlabeleditTree1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMTVDISPINFO pTVDispInfo = reinterpret_cast<LPNMTVDISPINFO>(pNMHDR);
	// TODO: 在此添加控件通知处理程序代码
	CString Name=pTVDispInfo->item.pszText;
	Name.Trim();
	if (Name.GetLength()==0 || Name.GetLength()>=64 || pTVDispInfo->item.mask==0)
	{
		*pResult = 0;
		return;
	}

	char sqlstr[128];
	int Id=m_Tree.GetItemData(pTVDispInfo->item.hItem);
	sprintf_s(sqlstr,sizeof(sqlstr),"update GroupTab set Name='%s' where Id=%d", Name, Id);
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);

	*pResult = 1;
}

void CRemoteManDlg::LoadHostList(HTREEITEM hItem)
{
	int rc;
	CArray<int ,int>GroupArray;
	GroupArray.SetSize(0,20);
	//枚举组ID
	if (SysConfig.ParentShowHost)
		EnumChildGroupId(hItem,GroupArray);
	else
		GroupArray.Add(m_Tree.GetItemData(hItem));
	//载入主机表
	int sqlstrlen=28+GroupArray.GetSize()*23,len;
	char *sqlstr=new char[sqlstrlen];
	len=sprintf_s(sqlstr,sqlstrlen,"select * from HostTab where ");
	for (int i=0; i<GroupArray.GetSize();i++)
		len+=sprintf_s(sqlstr+len,sqlstrlen-len,i==0 ? "ParentId=%d":" or ParentId=%d",GroupArray[i]);
	sqlstr[len++]=';';
	sqlstr[len]=0;
	CArray<HOST_STRUCT,HOST_STRUCT&>HostArray;
	HostArray.SetSize(0,20);
	TRACE("%s\r\n",sqlstr);
	rc = sqlite3_exec(m_pDB, sqlstr, ReadHostCallback, &HostArray, NULL);
	for (int i=0; i<HostArray.GetSize(); i++)
	{
		HOST_STRUCT Host=HostArray[i];
		ListAddHost(&Host,Host.Id);
	}
	delete sqlstr;
}

void CRemoteManDlg::OnTvnSelchangedTree1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	// TODO: 在此添加控件通知处理程序代码
	m_List.DeleteAllItems();
	SetDlgItemText(IDC_EDIT_README,"");
	SysConfig.GroupLastSelId=m_Tree.GetItemData(pNMTreeView->itemNew.hItem);
	LoadHostList(pNMTreeView->itemNew.hItem);
	*pResult = 0;
}

static int ReadStrCallback(void* para, int n_column, char** column_value, char** column_name)
{
	if (column_value[0]!=NULL)
		strcpy_s((char*)para,256,column_value[0]);
	return 0;
}

void CRemoteManDlg::OnLvnItemchangedList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// TODO: 在此添加控件通知处理程序代码
	if (pNMLV->uNewState==0) return;
	int n=m_List.GetSelectedCount();
	if (n!=1) 
	{
		SetDlgItemText(IDC_EDIT_README,"");
		return;
	}
	int Id=m_List.GetItemData(pNMLV->iItem);

	char sqlstr[64], Readme[256]={0};
	sprintf_s(sqlstr,sizeof(sqlstr),"select HostReadme from HostTab where id=%d;",Id);
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, ReadStrCallback, Readme, NULL);
	SetDlgItemText(IDC_EDIT_README,Readme);
	
	*pResult = 0;
}

afx_msg LRESULT CRemoteManDlg::OnModifyPasswordMessage(WPARAM wParam, LPARAM lParam)
{
	char const *Src=(char*)wParam;
	char const *New=(char*)lParam;
	char sqlstr[128];

	if (Src[0]!=0 || SysConfig.SysPassword[0]!=0)
	{
		if (strcmp(SysConfig.SysPassword,  AesEnCodeToStr(Src,strlen(Src),sqlstr,AES_KEY))!=0)
			return LRESULT("原密码错误.");
	}

	strcpy_s(SysConfig.SysPassword,sizeof(SysConfig.SysPassword),New[0]==0 ? "":AesEnCodeToStr(New,strlen(New),sqlstr,AES_KEY));
	int n=sprintf_s(sqlstr,sizeof(sqlstr),"update ConfigTab set Password='%s' where id=0;",SysConfig.SysPassword);
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);

	return LRESULT("密码修改成功.");
}

void CRemoteManDlg::OnBnClickedCheckMstConsole()
{
	// TODO: 在此添加控件通知处理程序代码
	SysConfig.MstscConsole=((CButton*)GetDlgItem(IDC_CHECK_MST_CONSOLE))->GetCheck()!=0;

	char sqlstr[128];
	int n=sprintf_s(sqlstr,sizeof(sqlstr),"update ConfigTab set MstscConsole=%s where id=0;",SysConfig.MstscConsole?"true":"false");
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
}

void CRemoteManDlg::OnBnClickedCheckMstDrive()
{
	// TODO: 在此添加控件通知处理程序代码
	SysConfig.MstscUseDrive=((CButton*)GetDlgItem(IDC_CHECK_MST_DRIVE))->GetCheck()!=0;

	char sqlstr[128];
	int n=sprintf_s(sqlstr,sizeof(sqlstr),"update ConfigTab set MstscUseDrive=%s where id=0;",SysConfig.MstscUseDrive?"true":"false");
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
}

void CRemoteManDlg::OnBnClickedCheckMstAudio()
{
	// TODO: 在此添加控件通知处理程序代码
	SysConfig.MstscRemoteAudio=((CButton*)GetDlgItem(IDC_CHECK_MST_AUDIO))->GetCheck()!=0;

	char sqlstr[128];
	int n=sprintf_s(sqlstr,sizeof(sqlstr),"update ConfigTab set MstscRemoteAudio=%s where id=0;",SysConfig.MstscRemoteAudio?"true":"false");
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
}

void CRemoteManDlg::OnCbnSelchangeComboMstWinpos()
{
	// TODO: 在此添加控件通知处理程序代码
	SysConfig.MstscWinpos=((CComboBox*)GetDlgItem(IDC_COMBO_MST_WINPOS))->GetCurSel();

	char sqlstr[128];
	int n=sprintf_s(sqlstr,sizeof(sqlstr),"update ConfigTab set MstscWinpos=%d where id=0;",SysConfig.MstscWinpos);
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
}

void CRemoteManDlg::OnBnClickedCheckRadminFullscreen()
{
	// TODO: 在此添加控件通知处理程序代码
	SysConfig.RadminFullScreen=((CButton*)GetDlgItem(IDC_CHECK_RADMIN_FULLSCREEN))->GetCheck()!=0;

	char sqlstr[128];
	int n=sprintf_s(sqlstr,sizeof(sqlstr),"update ConfigTab set RadminFullScreen=%s where id=0;",SysConfig.RadminFullScreen?"true":"false");
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
}


void CRemoteManDlg::OnCbnSelchangeComboRadminCtrlmode()
{
	// TODO: 在此添加控件通知处理程序代码
	SysConfig.RadminCtrlMode=((CComboBox*)GetDlgItem(IDC_COMBO_RADMIN_CTRLMODE))->GetCurSel();

	char sqlstr[128];
	int n=sprintf_s(sqlstr,sizeof(sqlstr),"update ConfigTab set RadminCtrlMode=%d where id=0;",SysConfig.RadminCtrlMode);
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
}


void CRemoteManDlg::OnNMRClickTree1(NMHDR *pNMHDR, LRESULT *pResult)
{
	// TODO: 在此添加控件通知处理程序代码
	CMenu Menu;
	Menu.LoadMenu(IDR_MENU_RCLICK);
	CMenu *pSubMenu=Menu.GetSubMenu(m_Tree.GetSelectedItem()==NULL ? 0:1);
	CPoint point;
	GetCursorPos(&point);
	pSubMenu->TrackPopupMenu(TPM_LEFTALIGN|TPM_LEFTBUTTON|TPM_VERTICAL,point.x,point.y,this);

	*pResult = 0;
}


void CRemoteManDlg::OnNMRClickList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	// TODO: 在此添加控件通知处理程序代码
	int Index=0;
	int SelCnt=m_List.GetSelectedCount();

	if (SelCnt==0)
		Index=2;
	else if (SelCnt==1)
	{
		char str[12];
		m_List.GetItemText(pNMItemActivate->iItem,0,str,10);
		Index=strcmp(str,CTRL_MODE_RADMIN_NAME)==0 ? 4:3;
	}
	else
		Index=5;

	CMenu Menu;
	Menu.LoadMenu(IDR_MENU_RCLICK);
	CMenu *pSubMenu=Menu.GetSubMenu(Index);
	CPoint point;
	GetCursorPos(&point);
	pSubMenu->TrackPopupMenu(TPM_LEFTALIGN|TPM_LEFTBUTTON|TPM_VERTICAL,point.x,point.y,this);

	*pResult = 0;
}

void CRemoteManDlg::OnNMDblclkList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	// TODO: 在此添加控件通知处理程序代码
	OnMenuClickedConnentHost();
	*pResult = 0;
}


void CRemoteManDlg::OnLvnBegindragList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// TODO: 在此添加控件通知处理程序代码
	m_nListDragIndex = pNMLV->iItem;
	POINT pt={8,8};
	m_pDragImage=m_List.CreateDragImage(m_nListDragIndex,&pt);
	if (m_pDragImage==NULL)
	{
		m_nListDragIndex = -1; 
		return ; 
	}
	m_pDragImage->BeginDrag(0,CPoint(8,8));
	m_pDragImage->DragEnter(GetDesktopWindow(), pNMLV->ptAction);
	SetCapture();
	*pResult = 0;
}


void CRemoteManDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	// TODO: 在此添加消息处理程序代码和/或调用默认值
	if (m_nListDragIndex!=-1)
	{     
		CPoint pt(point); 
		ClientToScreen(&pt); 
		m_pDragImage->DragMove(pt); 
		m_pDragImage->DragShowNolock(false); 
		m_pDragImage->DragShowNolock(true);
	} 
	CDialogEx::OnMouseMove(nFlags, point);
}


void CRemoteManDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	// TODO: 在此添加消息处理程序代码和/或调用默认值
	if (m_nListDragIndex==-1) 
	{
		CDialogEx::OnLButtonUp(nFlags, point);
		return;
	}
	//释放资源
	ReleaseCapture(); 
	m_pDragImage->DragLeave(GetDesktopWindow()); 
	m_pDragImage->EndDrag(); 
	delete m_pDragImage; 
	m_nListDragIndex=-1;
	//判断拖入处是否是树控件的标签处
	CPoint pt(point); 
	ClientToScreen(&pt); 
	CWnd *pWnd = WindowFromPoint(pt); 
	if (pWnd!=&m_Tree) return;
	m_Tree.ScreenToClient(&pt);
	HTREEITEM hItem=m_Tree.HitTest(pt, &nFlags); //用于获取拖入处的Item
	if (hItem == NULL)  return;

	//列出选择的项和ID
	int Cnt=m_List.GetSelectedCount();
	if (Cnt==0) return;
	int *Ids=new int[Cnt];
	POSITION pos=m_List.GetFirstSelectedItemPosition();
	for (int i=0; pos!=NULL && i<Cnt; i++)
	{
		int n=m_List.GetNextSelectedItem(pos);
		Ids[i]=m_List.GetItemData(n);
	}
	//更新数据库
	int ParentId = m_Tree.GetItemData(hItem);
	int sqlstrlen=44+17*Cnt;
	char *sqlstr=new char[44+17*Cnt];
	int len=sprintf_s(sqlstr,sqlstrlen,"update HostTab set ParentId=%d where ",ParentId);
	for (int i=0; i<Cnt; i++)
		len+=sprintf_s(sqlstr+len,sqlstrlen-len, i==0 ? "id=%d" : " or id=%d",Ids[i]);
	sqlstr[len++]=';';
	sqlstr[len]=0;
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	//
	delete Ids;
	delete sqlstr;
	//重新选择树hItem
	m_Tree.SelectItem(hItem);
}

void CRemoteManDlg::OnMenuClickedExportGroup(void)
{
	HTREEITEM hItem = m_Tree.GetSelectedItem();
	if (hItem==NULL) return;
	CFileDialog fdlg(FALSE,".db","ExportGroup",6,"*.db|*.db||");
	if (fdlg.DoModal()!=IDOK) return;
	//附加数据库
	char sqlstr[1536];
	sprintf_s(sqlstr,sizeof(sqlstr),"attach database '%s' as 'export';",
		CodeConverter::AsciiToUtf8((char const*)fdlg.GetPathName()).c_str());
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	if (rc!=0)
	{
		MessageBox("导出分组出错.","错误",MB_ICONERROR);
		return;
	}
	//添加表格
	rc=sprintf_s(sqlstr,sizeof(sqlstr),InitTabSqlStr,"export.","export.","export.","export.");	//要播放4个数据库名称
	TRACE("%s\r\n",sqlstr);
	rc=sqlite3_exec(m_pDB,sqlstr,NULL,NULL,NULL);
	if (rc!=0)
	{
		MessageBox("导出分组出错.","错误",MB_ICONERROR);
		return;
	}
	//枚举组ID	
	CArray<int ,int>GroupArray;
	GroupArray.SetSize(0,20);
	//枚举组ID
	EnumChildGroupId(hItem,GroupArray);
	//导出分组
	int sqlstrlen=58+23*GroupArray.GetSize(),len;
	char *sqlstr1=new char[sqlstrlen];
	len=sprintf_s(sqlstr1,sqlstrlen,"insert into export.GroupTab select * from GroupTab where ");
	for (int i=0; i<GroupArray.GetSize(); i++)
		len+=sprintf_s(sqlstr1+len,sqlstrlen-len,i==0?"id=%d":" or id=%d",GroupArray[i]);
	sqlstr1[len++]=';';
	sqlstr1[len]=0;
	TRACE("%s\r\n",sqlstr1);
	rc = sqlite3_exec(m_pDB, sqlstr1, NULL, NULL, NULL);
	//更改首分组父ID为0
	sprintf_s(sqlstr,sizeof(sqlstr),"update export.GroupTab set ParentId=0 where Id=%d;",GroupArray[0]);
	TRACE("%s\r\n",sqlstr);
	rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	//导出主机
	len=sprintf_s(sqlstr1,sqlstrlen,"insert into export.HostTab select * from HostTab where ");
	for (int i=0; i<GroupArray.GetSize(); i++)
		len+=sprintf_s(sqlstr1+len,sqlstrlen-len,i==0?"ParentId=%d":" or ParentId=%d",GroupArray[i]);
	sqlstr1[len++]=';';
	sqlstr1[len]=0;
	TRACE("%s\r\n",sqlstr1);
	rc = sqlite3_exec(m_pDB, sqlstr1, NULL, NULL, NULL);
	//分离数据库
	strcpy_s(sqlstr,sizeof(sqlstr),"detach database 'export';");
	TRACE("%s\r\n",sqlstr);
	rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	//
	delete sqlstr1;
}

/*
ImportParentId: 导入到主数据库的父分组ID
ExportParentId：导出的附加数据库的分组ID
*/
void CRemoteManDlg::ImportGroup(HTREEITEM hItem, int ExportParentId)
{
	int rc;
	char sqlstr[512];
	CArray<GROUP_STRUCT,GROUP_STRUCT&>GroupArray;
	GroupArray.SetSize(0,20);
	rc=sprintf_s(sqlstr,sizeof(sqlstr),"select * from export.GroupTab where ParentId=%d;",ExportParentId);
	TRACE("%s\r\n",sqlstr);
	rc = sqlite3_exec(m_pDB, sqlstr, ReadGroupCallBack, &GroupArray, NULL);
	//将该分组下的子分组添加到数据库和树控件，并扫描该子分组
	for (int i=0; i<GroupArray.GetSize(); i++)
	{
		GROUP_STRUCT g=GroupArray[i];
		//添加到数据库,先要检测此分组名是否存在
		int Id=0;
		HTREEITEM hChildItem=m_Tree.GetChildItem(hItem);
		while (hChildItem)
		{
			if (strcmp(m_Tree.GetItemText(hChildItem),g.Name)==0)
				break;
			hChildItem=m_Tree.GetNextSiblingItem(hChildItem);
		}
		//如果存在
		if (hChildItem!=NULL)
			Id=m_Tree.GetItemData(hChildItem);
		//没存在时，添加到数据库，并回读ID，添加到树控件
		else
		{
			Id=hItem==NULL || hItem==TVI_ROOT ? 0: m_Tree.GetItemData(hItem);		//这是要导入的父ID
			rc=sprintf_s(sqlstr,sizeof(sqlstr),"insert into GroupTab values(NULL,'%s',%d);",g.Name,Id);
			TRACE("%s\r\n",sqlstr);
			rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
			rc=sprintf_s(sqlstr,sizeof(sqlstr),"select Id from GroupTab where ParentId=%d and Name='%s';",Id,g.Name);
			TRACE("%s\r\n",sqlstr);
			rc = sqlite3_exec(m_pDB, sqlstr, ReadIntCallback, &Id, NULL);	//这是导入的子ID
			hChildItem=m_Tree.InsertItem(g.Name,0,1,hItem);
			m_Tree.SetItemData(hChildItem, Id);
		}
		//将该分组下的主机添加到数据库
		CArray<HOST_STRUCT,HOST_STRUCT&>HostArray;
		rc=sprintf_s(sqlstr,sizeof(sqlstr),"select * from export.HostTab where ParentId=%d;",g.Id);
		TRACE("%s\r\n",sqlstr);
		rc = sqlite3_exec(m_pDB, sqlstr, ReadHostCallback, &HostArray, NULL);
		for (int i=0; i<HostArray.GetSize(); i++)
		{
			HOST_STRUCT Host=HostArray[i];
			//查看该主机名是否存在
			int Cnt;
			rc=sprintf_s(sqlstr,sizeof(sqlstr),"select count() from HostTab where ParentId=%d and Name='%s' and CtrlMode=%d;",
				Id, Host.Name, Host.CtrlMode);
			TRACE("%s\r\n",sqlstr);
			rc = sqlite3_exec(m_pDB, sqlstr, ReadIntCallback, &Cnt, NULL);
			//添加到数据库
			if (Cnt==0)
			{
				rc=sprintf_s(sqlstr,sizeof(sqlstr),"insert into HostTab values(NULL,'%s',%d,%d,'%s',%d,'%s','%s','%s');",
					Host.Name, Id, Host.CtrlMode, Host.HostAddress, Host.HostPort, Host.Account, Host.Password, Host.ReadMe);
				TRACE("%s\r\n",sqlstr);
				rc = sqlite3_exec(m_pDB, sqlstr, ReadIntCallback, &Cnt, NULL);
			}
		}
		//枚举子分组
		ImportGroup(hChildItem,g.Id);
	}
}

void CRemoteManDlg::OnMenuClickedImportGroup(void)
{
	char sqlstr[512];
	//读取要导入的分组
	HTREEITEM hItem=m_Tree.GetSelectedItem();
	//选择导入的文件
	CFileDialogEx fdlg(TRUE,".db","",6,"*.db|*.db||");
	fdlg.m_ofn.lpstrTitle="选择导入的分组和数据";
	if (hItem!=NULL)
		fdlg.SetGroupName(m_Tree.GetItemText(hItem));
	if (fdlg.DoModal()!=IDOK) return;
	if (fdlg.m_GroupSel==0) hItem=TVI_ROOT;
	//附加数据库
	sprintf_s(sqlstr,sizeof(sqlstr),"attach database '%s' as 'export';",
		CodeConverter::AsciiToUtf8((char const*)fdlg.GetPathName()).c_str());
	TRACE("%s\r\n",sqlstr);
	int rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	if (rc!=0)
	{
		MessageBox("导入分组出错.","错误",MB_ICONERROR);
		return;
	}
	//导入数据
	ImportGroup(hItem,0);
	//分离数据库
	strcpy_s(sqlstr,sizeof(sqlstr),"detach database 'export';");
	TRACE("%s\r\n",sqlstr);
	rc = sqlite3_exec(m_pDB, sqlstr, NULL, NULL, NULL);
	//更新列表
	hItem=m_Tree.GetSelectedItem();
	if (hItem!=0)
	{
		m_List.DeleteAllItems();
		SetDlgItemText(IDC_EDIT_README,"");
		LoadHostList(hItem);
	}
}

