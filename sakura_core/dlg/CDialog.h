﻿/*!	@file
	@brief Dialog Box基底クラスヘッダファイル

	@author Norio Nakatani
*/
/*
	Copyright (C) 1998-2001, Norio Nakatani
	Copyright (C) 2002, YAZAKI
	Copyright (C) 2003, MIK
	Copyright (C) 2005, MIK
	Copyright (C) 2006, ryoji
	Copyright (C) 2011, nasukoji
	Copyright (C) 2012, Uchi

	This source code is designed for sakura editor.
	Please contact the copyright holder to use this code for other purpose.
*/


#ifndef _CDIALOG_H_
#define _CDIALOG_H_

class CDialog;

struct DLLSHAREDATA;
class CRecent;

enum EAnchorStyle
{
	ANCHOR_NONE              = 0,
	ANCHOR_LEFT              = 1,
	ANCHOR_RIGHT             = 2,
	ANCHOR_LEFT_RIGHT        = 3,
	ANCHOR_TOP               = 4,
	ANCHOR_TOP_LEFT          = 5,
	ANCHOR_TOP_RIGHT         = 6,
	ANCHOR_TOP_LEFT_RIGHT    = 7,
	ANCHOR_BOTTOM            = 8,
	ANCHOR_BOTTOM_LEFT       = 9,
	ANCHOR_BOTTOM_RIGHT      = 10,
	ANCHOR_BOTTOM_LEFT_RIGHT = 11,
	ANCHOR_TOP_BOTTOM        = 12,
	ANCHOR_TOP_BOTTOM_LEFT   = 13,
	ANCHOR_TOP_BOTTOM_RIGHT  = 14,
	ANCHOR_ALL               = 15
};

struct SAnchorList
{
	int id;
	EAnchorStyle anchor;
};

struct SComboBoxItemDeleter
{
	CRecent*	pRecent;
	HWND		hwndCombo;
	WNDPROC		pComboBoxWndProc;
	WNDPROC		pEditWndProc;
	WNDPROC		pListBoxWndProc;
	SComboBoxItemDeleter(): pRecent(NULL), hwndCombo(NULL), pComboBoxWndProc(NULL), pEditWndProc(NULL), pListBoxWndProc(NULL){}
};

/*-----------------------------------------------------------------------
クラスの宣言
-----------------------------------------------------------------------*/
/*!
	@brief ダイアログウィンドウを扱うクラス

	ダイアログボックスを作るときにはここから継承させる．

	@date 2002.2.17 YAZAKI CShareDataのインスタンスは、CProcessにひとつあるのみ。
*/
class CDialog
{
public:
	/*
	||  Constructors
	*/
	CDialog( bool bSizable = false, bool bCheckShareData = true );
	virtual ~CDialog();
	/*
	||  Attributes & Operations
	*/
	virtual INT_PTR DispatchEvent( HWND, UINT, WPARAM, LPARAM );	/* ダイアログのメッセージ処理 */
	INT_PTR DoModal(HINSTANCE hInstance, HWND hwndParent, int nDlgTemplete, LPARAM lParam);	/* モーダルダイアログの表示 */
	HWND DoModeless(HINSTANCE hInstance, HWND hwndParent, int nDlgTemplete, LPARAM lParam, int nCmdShow);	/* モードレスダイアログの表示 */
	HWND DoModeless(HINSTANCE hInstance, HWND hwndParent, LPCDLGTEMPLATE lpTemplate, LPARAM lParam, int nCmdShow);	/* モードレスダイアログの表示 */
	void CloseDialog(INT_PTR nModalRetVal);

	virtual BOOL OnInitDialog(HWND hwndDlg, WPARAM wParam, LPARAM lParam);
	virtual void SetDialogPosSize();
	virtual BOOL OnDestroy( void );
	virtual BOOL OnNotify( WPARAM wParam, LPARAM lParam ){return FALSE;}
	BOOL OnSize();
	virtual BOOL OnSize( WPARAM wParam, LPARAM lParam );
	virtual BOOL OnMove( WPARAM wParam, LPARAM lParam );
	virtual BOOL OnDrawItem( WPARAM wParam, LPARAM lParam ){return TRUE;}
	virtual BOOL OnTimer( WPARAM wParam ){return TRUE;}
	virtual BOOL OnKeyDown( WPARAM wParam, LPARAM lParam ){return TRUE;}
	virtual BOOL OnDeviceChange( WPARAM wParam, LPARAM lParam ){return TRUE;}
	virtual int GetData( void ){return 1;}/* ダイアログデータの取得 */
	virtual void SetData( void ){return;}/* ダイアログデータの設定 */
	virtual BOOL OnBnClicked(int wID);
	virtual BOOL OnStnClicked( int ){return FALSE;}
	virtual BOOL OnEnChange( HWND hwndCtl, int wID ){return FALSE;}
	virtual BOOL OnEnKillFocus( HWND hwndCtl, int wID ){return FALSE;}
	virtual BOOL OnLbnSelChange( HWND hwndCtl, int wID ){return FALSE;}
	virtual BOOL OnLbnDblclk( int wID ){return FALSE;}
	virtual BOOL OnCbnSelChange( HWND hwndCtl, int wID ){return FALSE;}
	virtual BOOL OnCbnEditChange( HWND hwndCtl, int wID ){return FALSE;} // @@2005.03.31 MIK タグジャンプDialog
	virtual BOOL OnCbnDropDown( HWND hwndCtl, int wID );
	static BOOL OnCbnDropDown( HWND hwndCtl, bool scrollBar );
//	virtual BOOL OnCbnCloseUp( HWND hwndCtl, int wID ){return FALSE;}
	virtual BOOL OnCbnSelEndOk( HWND hwndCtl, int wID );

	virtual BOOL OnKillFocus( WPARAM wParam, LPARAM lParam ){return FALSE;}
	virtual BOOL OnActivate( WPARAM wParam, LPARAM lParam ){return FALSE;}	//@@@ 2003.04.08 MIK
	virtual int OnVKeyToItem( WPARAM wParam, LPARAM lParam ){ return -1; }
	virtual LRESULT OnCharToItem( WPARAM wParam, LPARAM lParam ){ return -1; }
	virtual BOOL OnPopupHelp(WPARAM wPara, LPARAM lParam);	//@@@ 2002.01.18 add
	virtual BOOL OnContextMenu(WPARAM wPara, LPARAM lParam);	//@@@ 2002.01.18 add
	virtual LPVOID GetHelpIdTable(void);	//@@@ 2002.01.18 add

	void ResizeItem( HWND hTarget, const POINT& ptDlgDefalut, const POINT& ptDlgNew, const RECT& rcItemDefault, EAnchorStyle anchor, bool bUpdate = true);
	void GetItemClientRect( int wID, RECT& rc );
	static void SetComboBoxDeleter( HWND hwndCtl, SComboBoxItemDeleter* data );
public:

	static bool DirectoryUp(TCHAR* szDir);

public:
	HWND GetHwnd() const{ return m_hWnd; }
	//特殊インターフェース (使用は好ましくない)
	void _SetHwnd(HWND hwnd){ m_hWnd = hwnd; }

	//! 検索・置換ダイアログのオプション固定
	enum {
		SCH_CLR_WORD		= 1 << 0,	//!< 単語単位クリア
		SCH_SET_WORD		= 1 << 1,	//!< 単語単位セット
		SCH_CLR_CASE		= 1 << 2,	//!< 大文字小文字区別クリア
		SCH_SET_CASE		= 1 << 3,	//!< 大文字小文字区別セット
		SCH_CLR_REGEXP		= 1 << 4,	//!< 正規表現クリア
		SCH_SET_REGEXP		= 1 << 5,	//!< 正規表現セット
		SCH_CLR_MSG			= 1 << 6,	//!< メッセージクリア
		SCH_SET_MSG			= 1 << 7,	//!< メッセージセット
		SCH_CLR_CLOSEDLG	= 1 << 8,	//!< ダイアログを閉じるクリア
		SCH_SET_CLOSEDLG	= 1 << 9,	//!< ダイアログを閉じるセット
		SCH_CLR_LOOP		= 1 << 10,	//!< 上から検索クリア
		SCH_SET_LOOP		= 1 << 11,	//!< 上から検索セット
		SCH_CLR_CLIP		= 1 << 12,	//!< クリップボードから貼り付けクリア
		SCH_SET_CLIP		= 1 << 13,	//!< クリップボードから貼り付けセット
		
		SCH_BUTTON_MASK		= 0xF0000000,	//!< デフォルトボタンマスク
		SCH_PREV			= 0x10000000,	//!< ↑検索
		SCH_NEXT			= 0x20000000,	//!< ↓検索
		SCH_MARK			= 0x30000000,	//!< 該当行マーク
		SCH_REPLACE			= 0x40000000,	//!< 置換
		SCH_REPLACEA		= 0x50000000,	//!< すべて置換
		SCH_SETONLY			= 0x60000000,	//!< 検索条件のみセット(ボタンにはない)
	};

public:
	HINSTANCE		m_hInstance;	/* アプリケーションインスタンスのハンドル */
	HWND			m_hwndParent;	/* オーナーウィンドウのハンドル */
private:
	HWND			m_hWnd;			/* このダイアログのハンドル */
public:
	HWND			m_hwndSizeBox;
	LPARAM			m_lParam;
	BOOL			m_bModal;		/* モーダル ダイアログか */
	bool			m_bSizable;		// 可変ダイアログかどうか
	int				m_nShowCmd;		//	最大化/最小化
	int				m_nWidth;
	int				m_nHeight;
	int				m_xPos;
	int				m_yPos;
//	void*			m_pcEditView;
	DLLSHAREDATA*	m_pShareData;
	BOOL			m_bInited;
	HINSTANCE		m_hLangRsrcInstance;		// メッセージリソースDLLのインスタンスハンドル	// 2011.04.10 nasukoji

protected:
	void CreateSizeBox( void );
	BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	HWND GetItemHwnd(int nID){ return ::GetDlgItem( GetHwnd(), nID ); }

	// コントロールに画面のフォントを設定	2012/11/27 Uchi
	HFONT SetMainFont( HWND hTarget );
};



///////////////////////////////////////////////////////////////////////
#endif /* _CDIALOG_H_ */



