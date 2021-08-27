/*! @file */
/*
	Copyright (C) 2018-2021, Sakura Editor Organization

	This software is provided 'as-is', without any express or implied
	warranty. In no event will the authors be held liable for any damages
	arising from the use of this software.

	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:

		1. The origin of this software must not be misrepresented;
		   you must not claim that you wrote the original software.
		   If you use this software in a product, an acknowledgment
		   in the product documentation would be appreciated but is
		   not required.

		2. Altered source versions must be plainly marked as such,
		   and must not be misrepresented as being the original software.

		3. This notice may not be removed or altered from any source
		   distribution.
*/
#include "StdAfx.h"
#include "CGrepAgent.h"
#include "CGrepEnumKeys.h"
#include "CGrepEnumFilterFiles.h"
#include "CGrepEnumFilterFolders.h"
#include "CSearchAgent.h"
#include "dlg/CDlgCancel.h"
#include "_main/CAppMode.h"
#include "_main/CMutex.h"
#include "env/CShareData.h"
#include "env/CSakuraEnvironment.h"
#include "COpeBlk.h"
#include "window/CEditWnd.h"
#include "charset/CCodeMediator.h"
#include "view/colors/CColorStrategy.h"
#include "charset/CCodeFactory.h"
#include "charset/CCodeBase.h"
#include "charset/CCodePage.h"
#include "io/CBinaryStream.h"
#include "util/window.h"
#include "util/module.h"
#include "util/string_ex2.h"
#include "debug/CRunningTimer.h"
#include <iterator>
#include <deque>
#include <memory>
#include "apiwrap/StdApi.h"
#include "apiwrap/StdControl.h"
#include "CSelectLang.h"
#include "sakura_rc.h"
#include "config/system_constants.h"
#include "String_define.h"

#define UICHECK_INTERVAL_MILLISEC 200	// UI確認の時間間隔
#define ADDTAIL_INTERVAL_MILLISEC 200	// 結果出力の時間間隔
#define UIFILENAME_INTERVAL_MILLISEC 200	// Cancelダイアログのファイル名表示更新間隔

/*!
 * 指定された文字列をタイプ別設定に従ってエスケープする
 */
inline CNativeW EscapeStringLiteral( const STypeConfig& type, const CNativeW& cmemString )
{
	CNativeW cmemWork2( cmemString );
	if( FALSE == type.m_ColorInfoArr[COLORIDX_WSTRING].m_bDisp ){
		// 2011.11.28 色指定が無効ならエスケープしない
	}else
	if( type.m_nStringType == STRING_LITERAL_CPP || type.m_nStringType == STRING_LITERAL_CSHARP
		|| type.m_nStringType == STRING_LITERAL_PYTHON ){	/* 文字列区切り記号エスケープ方法 */
		cmemWork2.Replace( L"\\", L"\\\\" );
		cmemWork2.Replace( L"\'", L"\\\'" );
		cmemWork2.Replace( L"\"", L"\\\"" );
	}else if( type.m_nStringType == STRING_LITERAL_PLSQL ){
		cmemWork2.Replace( L"\'", L"\'\'" );
		cmemWork2.Replace( L"\"", L"\"\"" );
	}
	return cmemWork2;
}

/*!
 * パスリストを文字列化する
 */
template<class ContainerType>
std::wstring FormatPathList( const ContainerType& containter )
{
	std::wstring strPatterns;
	bool firstItem = true;
	for( const auto& pattern : containter ){
		// パスリストは ':' で区切る(2つ目以降の前に付加する)
		if( firstItem ){
			firstItem = false;
		}else {
			strPatterns += L';';
		}

		// ';' を含むパス名は引用符で囲む
		if( std::wstring::npos != std::wstring_view( pattern ).find( L';' ) ){
			strPatterns += L'"';
			strPatterns += pattern;
			strPatterns += L'"';
		}else{
			strPatterns += pattern;
		}
	}
	return strPatterns;
}

CGrepAgent::CGrepAgent()
: m_bGrepMode( false )			/* Grepモードか */
, m_bGrepRunning( false )		/* Grep処理中 */
, m_dwTickAddTail( 0 )
, m_dwTickUICheck( 0 )
, m_dwTickUIFileName( 0 )
{
}

ECallbackResult CGrepAgent::OnBeforeClose()
{
	//GREP処理中は終了できない
	if( m_bGrepRunning ){
		// アクティブにする
		ActivateFrameWindow( CEditWnd::getInstance()->GetHwnd() );	//@@@ 2003.06.25 MIK
		TopInfoMessage(
			CEditWnd::getInstance()->GetHwnd(),
			LS(STR_GREP_RUNNINNG)
		);
		return CALLBACK_INTERRUPT;
	}
	return CALLBACK_CONTINUE;
}

void CGrepAgent::OnAfterSave(const SSaveInfo& sSaveInfo)
{
	// 名前を付けて保存から再ロードが除去された分の不足処理を追加（ANSI版との差異）	// 2009.08.12 ryoji
	m_bGrepMode = false;	// grepウィンドウは通常ウィンドウ化
	CAppMode::getInstance()->m_szGrepKey[0] = L'\0';
}

/*!
	@date 2014.03.09 novice 最後の\\を取り除くのをやめる(d:\\ -> d:になる)
*/
void CGrepAgent::CreateFolders( const WCHAR* pszPath, std::vector<std::wstring>& vPaths )
{
	std::wstring strPath( pszPath );
	const int nPathLen = static_cast<int>( strPath.length() );

	WCHAR* token;
	int nPathPos = 0;
	while( NULL != (token = my_strtok<WCHAR>( strPath.data(), nPathLen, &nPathPos, L";")) ){
		std::wstring strTemp( token );
		// パスに含まれる '"' を削除する
		strTemp.erase( std::remove( strTemp.begin(), strTemp.end(), L'"' ), strTemp.end() );
		/* ロングファイル名を取得する */
		WCHAR szTmp2[_MAX_PATH];
		if( ::GetLongFileName( strTemp.c_str(), szTmp2 ) ){
			vPaths.push_back( szTmp2 );
		}else{
			vPaths.emplace_back( strTemp );
		}
	}
}

/*! 最後の\\を取り除く
	@date 2014.03.09 novice 新規作成
*/
std::wstring CGrepAgent::ChopYen( const std::wstring& str )
{
	std::wstring dst = str;
	size_t nPathLen = dst.length();

	// 最後のフォルダ区切り記号を削除する
	// [A:\]などのルートであっても削除
	for(size_t i = 0; i < nPathLen; i++ ){
#ifdef _MBCS
		if( _IS_SJIS_1( (unsigned char)dst[i] ) && (i + 1 < nPathLen) && _IS_SJIS_2( (unsigned char)dst[i + 1] ) ){
			// SJIS読み飛ばし
			i++;
		} else
#endif
		if( L'\\' == dst[i] && i == nPathLen - 1 ){
			dst.resize( nPathLen - 1 );
			break;
		}
	}

	return dst;
}

void CGrepAgent::AddTail( CEditView* pcEditView, const CNativeW& cmem, bool bAddStdout )
{
	m_dwTickAddTail = ::GetTickCount();
	if( bAddStdout ){
		HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
		if( out && out != INVALID_HANDLE_VALUE ){
			CMemory cmemOut;
			std::unique_ptr<CCodeBase> pcCodeBase( CCodeFactory::CreateCodeBase(
					pcEditView->GetDocument()->GetDocumentEncoding(), 0) );
			pcCodeBase->UnicodeToCode( cmem, &cmemOut );
			DWORD dwWrite = 0;
			::WriteFile(out, cmemOut.GetRawPtr(), cmemOut.GetRawLength(), &dwWrite, NULL);
		}
	}else{
		pcEditView->GetCommander().Command_ADDTAIL( cmem.GetStringPtr(), cmem.GetStringLength() );
		pcEditView->GetCommander().Command_GOFILEEND( FALSE );
		if( !CEditWnd::getInstance()->UpdateTextWrap() )	// 折り返し方法関連の更新	// 2008.06.10 ryoji
			CEditWnd::getInstance()->RedrawAllViews( pcEditView );	//	他のペインの表示を更新
	}
}

int GetHwndTitle(HWND& hWndTarget, CNativeW* pmemTitle, WCHAR* pszWindowName, WCHAR* pszWindowPath, const WCHAR* pszFile)
{
	if( 0 != wcsncmp(L":HWND:", pszFile, 6) ){
		return 0; // ハンドルGrepではない
	}
#ifdef _WIN64
	_stscanf(pszFile + 6, L"%016I64x", &hWndTarget);
#else
	_stscanf(pszFile + 6, L"%08x", &hWndTarget);
#endif
	if( pmemTitle ){
		const wchar_t* p = L"Window:[";
		pmemTitle->SetStringHoldBuffer(p, 8);
	}
	if( !IsSakuraMainWindow(hWndTarget) ){
		return -1;
	}
	::SendMessageAny(hWndTarget, MYWM_GETFILEINFO, 0, 0);
	EditInfo* editInfo = &(GetDllShareData().m_sWorkBuffer.m_EditInfo_MYWM_GETFILEINFO);
	if( '\0' == editInfo->m_szPath[0] ){
		// Grepかアウトプットか無題
		WCHAR szTitle[_MAX_PATH];
		WCHAR szGrep[100];
		editInfo->m_bIsModified = false;
		const EditNode* node = CAppNodeManager::getInstance()->GetEditNode(hWndTarget);
		WCHAR* pszTagName = szTitle;
		if( editInfo->m_bIsGrep ){
			// Grepは検索キーとタグがぶつかることがあるので単に(Grep)と表示
			pszTagName = szGrep;
			wcsncpy_s(pszTagName, _countof(szGrep), L"(Grep)", _TRUNCATE);
		}
		CFileNameManager::getInstance()->GetMenuFullLabel_WinListNoEscape(szTitle, _countof(szTitle), editInfo, node->m_nId, -1, NULL );
#ifdef _WIN64
		auto_sprintf(pszWindowName, L":HWND:[%016I64x]%s", hWndTarget, pszTagName);
#else
		auto_sprintf(pszWindowName, L":HWND:[%08x]%s", hWndTarget, pszTagName);
#endif
		if( pmemTitle ){
			pmemTitle->AppendString(szTitle);
		}
		pszWindowPath[0] = L'\0';
	}else{
		SplitPath_FolderAndFile(editInfo->m_szPath, pszWindowPath, pszWindowName);
		if( pmemTitle ){
			pmemTitle->AppendString(pszWindowName);
		}
	}
	if( pmemTitle ){
		pmemTitle->AppendString(L"]");
	}
	return 1;
}


/*! Grep実行

  @param[in] pcmGrepKey 検索パターン
  @param[in] pcmGrepFile 検索対象ファイルパターン(!で除外指定))
  @param[in] pcmGrepFolder 検索対象フォルダ

  @date 2008.12.07 nasukoji	ファイル名パターンのバッファオーバラン対策
  @date 2008.12.13 genta 検索パターンのバッファオーバラン対策
  @date 2012.10.13 novice 検索オプションをクラスごと代入
*/
DWORD CGrepAgent::DoGrep(
	CEditView*				pcViewDst,
	bool					bGrepReplace,
	const CNativeW*			pcmGrepKey,
	const CNativeW*			pcmGrepReplace,
	const CNativeW*			pcmGrepFile,
	const CNativeW*			pcmGrepFolder,
	bool					bGrepCurFolder,
	BOOL					bGrepSubFolder,
	bool					bGrepStdout,
	bool					bGrepHeader,
	const SSearchOption&	sSearchOption,
	ECodeType				nGrepCharSet,	// 2002/09/21 Moca 文字コードセット選択
	int						nGrepOutputLineType,
	int						nGrepOutputStyle,
	bool					bGrepOutputFileOnly,
	bool					bGrepOutputBaseFolder,
	bool					bGrepSeparateFolder,
	bool					bGrepPaste,
	bool					bGrepBackup
)
{
	MY_RUNNINGTIMER( cRunningTimer, L"CEditView::DoGrep" );

	// 再入不可
	if( this->m_bGrepRunning ){
		assert_warning( false == this->m_bGrepRunning );
		return 0xffffffff;
	}

	this->m_bGrepRunning = true;

	int			nHitCount = 0;
	CDlgCancel	cDlgCancel;
	HWND		hwndCancel;
	//	Jun. 27, 2001 genta	正規表現ライブラリの差し替え
	CBregexp	cRegexp;
	CNativeW	cmemMessage;
	CNativeW	cUnicodeBuffer;
	CNativeW	cOutBuffer;
	int			nWork;
	SGrepOption	sGrepOption;

	/*
	|| バッファサイズの調整
	*/
	cmemMessage.AllocStringBuffer( 4000 );
	cUnicodeBuffer.AllocStringBuffer( 4000 );
	if( bGrepReplace ) cOutBuffer.AllocStringBuffer( 4000 );

	pcViewDst->m_bDoing_UndoRedo		= true;

	/* アンドゥバッファの処理 */
	if( NULL != pcViewDst->GetDocument()->m_cDocEditor.m_pcOpeBlk ){	/* 操作ブロック */
//@@@2002.2.2 YAZAKI NULLじゃないと進まないので、とりあえずコメント。＆NULLのときは、new COpeBlkする。
//		while( NULL != m_pcOpeBlk ){}
//		delete m_pcOpeBlk;
//		m_pcOpeBlk = NULL;
	}
	else {
		pcViewDst->GetDocument()->m_cDocEditor.m_pcOpeBlk = new COpeBlk;
		pcViewDst->GetDocument()->m_cDocEditor.m_nOpeBlkRedawCount = 0;
	}
	pcViewDst->GetDocument()->m_cDocEditor.m_pcOpeBlk->AddRef();

	pcViewDst->m_bCurSrchKeyMark = true;								/* 検索文字列のマーク */
	pcViewDst->m_strCurSearchKey = pcmGrepKey->GetStringPtr();				/* 検索文字列 */
	pcViewDst->m_sCurSearchOption = sSearchOption;						// 検索オプション
	pcViewDst->m_nCurSearchKeySequence = GetDllShareData().m_Common.m_sSearch.m_nSearchKeySequence;

	// 置換後文字列の準備
	CNativeW cmemReplace;
	if( bGrepReplace ){
		if( bGrepPaste ){
			// 矩形・ラインモード貼り付けは未サポート
			bool bColmnSelect;
			bool bLineSelect = false;
			if( !pcViewDst->MyGetClipboardData( cmemReplace, &bColmnSelect, GetDllShareData().m_Common.m_sEdit.m_bEnableLineModePaste? &bLineSelect: NULL ) ){
				this->m_bGrepRunning = false;
				pcViewDst->m_bDoing_UndoRedo = false;
				ErrorMessage( pcViewDst->m_hwndParent, LS(STR_DLGREPLC_CLIPBOARD) );
				return 0;
			}
			if( bLineSelect ){
				int len = cmemReplace.GetStringLength();
				if( cmemReplace[len - 1] != WCODE::CR && cmemReplace[len - 1] != WCODE::LF ){
					cmemReplace.AppendString(pcViewDst->GetDocument()->m_cDocEditor.GetNewLineCode().GetValue2());
				}
			}
			if( GetDllShareData().m_Common.m_sEdit.m_bConvertEOLPaste ){
				CLogicInt len = cmemReplace.GetStringLength();
				wchar_t	*pszConvertedText = new wchar_t[len * 2]; // 全文字\n→\r\n変換で最大の２倍になる
				CLogicInt nConvertedTextLen = pcViewDst->m_cCommander.ConvertEol(cmemReplace.GetStringPtr(), len, pszConvertedText);
				cmemReplace.SetString(pszConvertedText, nConvertedTextLen);
				delete [] pszConvertedText;
			}
		}else{
			cmemReplace = *pcmGrepReplace;
		}
	}
	/* 正規表現 */

	//	From Here Jun. 27 genta
	/*
		Grepを行うに当たって検索・画面色分け用正規表現バッファも
		初期化する．これはGrep検索結果の色分けを行うため．

		Note: ここで強調するのは最後の検索文字列であって
		Grep対象パターンではないことに注意
	*/
	if( !pcViewDst->m_sSearchPattern.SetPattern(pcViewDst->GetHwnd(), pcViewDst->m_strCurSearchKey.c_str(), pcViewDst->m_strCurSearchKey.size(),
			pcViewDst->m_sCurSearchOption, &pcViewDst->m_CurRegexp) ){
		this->m_bGrepRunning = false;
		pcViewDst->m_bDoing_UndoRedo = false;
		pcViewDst->SetUndoBuffer();
		return 0;
	}

	//2014.06.13 別ウィンドウで検索したとき用にGrepダイアログの検索キーを設定
	pcViewDst->m_pcEditWnd->m_cDlgGrep.m_strText = pcmGrepKey->GetStringPtr();
	pcViewDst->m_pcEditWnd->m_cDlgGrep.m_bSetText = true;
	pcViewDst->m_pcEditWnd->m_cDlgGrepReplace.m_strText = pcmGrepKey->GetStringPtr();
	if( bGrepReplace ){
		pcViewDst->m_pcEditWnd->m_cDlgGrepReplace.m_strText2 = pcmGrepReplace->GetStringPtr();
	}
	pcViewDst->m_pcEditWnd->m_cDlgGrepReplace.m_bSetText = true;
	hwndCancel = cDlgCancel.DoModeless( G_AppInstance(), pcViewDst->m_hwndParent, IDD_GREPRUNNING );

	::SetDlgItemInt( hwndCancel, IDC_STATIC_HITCOUNT, 0, FALSE );
	::DlgItem_SetText( hwndCancel, IDC_STATIC_CURFILE, L" " );	// 2002/09/09 Moca add
	::CheckDlgButton( hwndCancel, IDC_CHECK_REALTIMEVIEW, GetDllShareData().m_Common.m_sSearch.m_bGrepRealTimeView );	// 2003.06.23 Moca

	//	2008.12.13 genta パターンが長すぎる場合は登録しない
	//	(正規表現が途中で途切れると困るので)
	//	2011.12.10 Moca 表示の際に...に切り捨てられるので登録するように
	wcsncpy_s( CAppMode::getInstance()->m_szGrepKey, _countof(CAppMode::getInstance()->m_szGrepKey), pcmGrepKey->GetStringPtr(), _TRUNCATE );
	this->m_bGrepMode = true;

	//	2007.07.22 genta
	//	バージョン番号取得のため，処理を前の方へ移動した
	CSearchStringPattern pattern;
	{
		/* 検索パターンのコンパイル */
		bool bError = !pattern.SetPattern(
			pcViewDst->GetHwnd(), pcmGrepKey->GetStringPtr(), pcmGrepKey->GetStringLength(),
			sSearchOption, &cRegexp
		);
		if( bError ){
			this->m_bGrepRunning = false;
			pcViewDst->m_bDoing_UndoRedo = false;
			pcViewDst->SetUndoBuffer();
			return 0;
		}
	}
	
	// Grepオプションまとめ
	sGrepOption.bGrepSubFolder = FALSE != bGrepSubFolder;
	sGrepOption.bGrepStdout = bGrepStdout;
	sGrepOption.bGrepHeader = bGrepHeader;
	sGrepOption.nGrepCharSet = nGrepCharSet;
	sGrepOption.nGrepOutputLineType = nGrepOutputLineType;
	sGrepOption.nGrepOutputStyle = nGrepOutputStyle;
	sGrepOption.bGrepOutputFileOnly = bGrepOutputFileOnly;
	sGrepOption.bGrepOutputBaseFolder = bGrepOutputBaseFolder;
	sGrepOption.bGrepSeparateFolder = bGrepSeparateFolder;
	sGrepOption.bGrepReplace = bGrepReplace;
	sGrepOption.bGrepPaste = bGrepPaste;
	sGrepOption.bGrepBackup = bGrepBackup;
	if( sGrepOption.bGrepReplace ){
		// Grep否定行はGrep置換では無効
		if( sGrepOption.nGrepOutputLineType == 2 ){
			sGrepOption.nGrepOutputLineType = 1; // 行単位
		}
	}

//2002.02.08 Grepアイコンも大きいアイコンと小さいアイコンを別々にする。
	HICON	hIconBig, hIconSmall;
	//	Dec, 2, 2002 genta アイコン読み込み方法変更
	hIconBig   = GetAppIcon( G_AppInstance(), ICON_DEFAULT_GREP, FN_GREP_ICON, false );
	hIconSmall = GetAppIcon( G_AppInstance(), ICON_DEFAULT_GREP, FN_GREP_ICON, true );

	//	Sep. 10, 2002 genta
	//	CEditWndに新設した関数を使うように
	CEditWnd*	pCEditWnd = CEditWnd::getInstance();	//	Sep. 10, 2002 genta
	pCEditWnd->SetWindowIcon( hIconSmall, ICON_SMALL );
	pCEditWnd->SetWindowIcon( hIconBig, ICON_BIG );

	CGrepEnumKeys cGrepEnumKeys;
	{
		int nErrorNo = cGrepEnumKeys.SetFileKeys( pcmGrepFile->GetStringPtr() );
		if( nErrorNo != 0 ){
			this->m_bGrepRunning = false;
			pcViewDst->m_bDoing_UndoRedo = false;
			pcViewDst->SetUndoBuffer();

			const WCHAR* pszErrorMessage = LS(STR_GREP_ERR_ENUMKEYS0);
			if( nErrorNo == 1 ){
				pszErrorMessage = LS(STR_GREP_ERR_ENUMKEYS1);
			}
			else if( nErrorNo == 2 ){
				pszErrorMessage = LS(STR_GREP_ERR_ENUMKEYS2);
			}
			ErrorMessage( pcViewDst->m_hwndParent, L"%s", pszErrorMessage );
			return 0;
		}
	}

	// 出力対象ビューのタイプ別設定(grepout固定)
	const STypeConfig& type = pcViewDst->m_pcEditDoc->m_cDocType.GetDocumentAttribute();

	std::vector<std::wstring> vPaths;
	CreateFolders( pcmGrepFolder->GetStringPtr(), vPaths );

	nWork = pcmGrepKey->GetStringLength(); // 2003.06.10 Moca あらかじめ長さを計算しておく

	/* 最後にテキストを追加 */
	CNativeW	cmemWork;
	cmemMessage.AppendString( LS( STR_GREP_SEARCH_CONDITION ) );	//L"\r\n□検索条件  "
	if( 0 < nWork ){
		cmemMessage.AppendString( L"\"" );
		cmemMessage += EscapeStringLiteral(type, *pcmGrepKey);
		cmemMessage.AppendString( L"\"\r\n" );
	}else{
		cmemMessage.AppendString( LS( STR_GREP_SEARCH_FILE ) );	//L"「ファイル検索」\r\n"
	}

	if( bGrepReplace ){
		cmemMessage.AppendString( LS(STR_GREP_REPLACE_TO) );
		if( bGrepPaste ){
			cmemMessage.AppendString( LS(STR_GREP_PASTE_CLIPBOAD) );
		}else{
			cmemMessage.AppendString( L"\"" );
			cmemMessage += EscapeStringLiteral(type, cmemReplace);
			cmemMessage.AppendString( L"\"\r\n" );
		}
	}

	HWND hWndTarget = NULL;
	WCHAR szWindowName[_MAX_PATH];
	WCHAR szWindowPath[_MAX_PATH];
	{
		int nHwndRet = GetHwndTitle(hWndTarget, &cmemWork, szWindowName, szWindowPath, pcmGrepFile->GetStringPtr());
		if( -1 == nHwndRet ){
			cmemMessage.AppendString(L"HWND handle error.\n");
			if( sGrepOption.bGrepHeader ){
				AddTail(pcViewDst, cmemMessage, sGrepOption.bGrepStdout);
			}
			return 0;
		}else if( 0 == nHwndRet ){
			{
				// 解析済みのファイルパターン配列を取得する
				const auto& vecSearchFileKeys = cGrepEnumKeys.m_vecSearchFileKeys;
				std::wstring strPatterns = FormatPathList( vecSearchFileKeys );
				cmemWork.SetString( strPatterns.c_str(), strPatterns.length() );
			}
		}
	}
	cmemMessage.AppendString( LS( STR_GREP_SEARCH_TARGET ) );	//L"検索対象   "
	cmemMessage += cmemWork;
	cmemMessage.AppendString( L"\r\n" );

	cmemMessage.AppendString( LS( STR_GREP_SEARCH_FOLDER ) );	//L"フォルダ   "
	{
		// フォルダリストから末尾のバックスラッシュを削ったパスリストを作る
		std::list<std::wstring> folders;
		std::transform( vPaths.cbegin(), vPaths.cend(), std::back_inserter( folders ), []( const auto& path ) { return ChopYen( path ); } );
		std::wstring strPatterns = FormatPathList( folders );
		cmemMessage.AppendString( strPatterns.c_str(), strPatterns.length() );
	}
	cmemMessage.AppendString( L"\r\n" );

	cmemMessage.AppendString(LS(STR_GREP_EXCLUDE_FILE));	//L"除外ファイル   "
	{
		// 除外ファイルの解析済みリストを取得る
		auto excludeFiles = cGrepEnumKeys.GetExcludeFiles();
		std::wstring strPatterns = FormatPathList( excludeFiles );
		cmemMessage.AppendString( strPatterns.c_str(), strPatterns.length() );
	}
	cmemMessage.AppendString(L"\r\n");

	cmemMessage.AppendString(LS(STR_GREP_EXCLUDE_FOLDER));	//L"除外フォルダ   "
	{
		// 除外フォルダの解析済みリストを取得する
		auto excludeFolders = cGrepEnumKeys.GetExcludeFolders();
		std::wstring strPatterns = FormatPathList( excludeFolders );
		cmemMessage.AppendString( strPatterns.c_str(), strPatterns.length() );
	}
	cmemMessage.AppendString(L"\r\n");

	const wchar_t*	pszWork;
	if( sGrepOption.bGrepSubFolder ){
		pszWork = LS( STR_GREP_SUBFOLDER_YES );	//L"    (サブフォルダも検索)\r\n"
	}else{
		pszWork = LS( STR_GREP_SUBFOLDER_NO );	//L"    (サブフォルダを検索しない)\r\n"
	}
	cmemMessage.AppendString( pszWork );

	if( 0 < nWork ){ // 2003.06.10 Moca ファイル検索の場合は表示しない // 2004.09.26 条件誤り修正
		if( sSearchOption.bWordOnly ){
		/* 単語単位で探す */
			cmemMessage.AppendString( LS( STR_GREP_COMPLETE_WORD ) );	//L"    (単語単位で探す)\r\n"
		}

		if( sSearchOption.bLoHiCase ){
			pszWork = LS( STR_GREP_CASE_SENSITIVE );	//L"    (英大文字小文字を区別する)\r\n"
		}else{
			pszWork = LS( STR_GREP_IGNORE_CASE );	//L"    (英大文字小文字を区別しない)\r\n"
		}
		cmemMessage.AppendString( pszWork );

		if( sSearchOption.bRegularExp ){
			//	2007.07.22 genta : 正規表現ライブラリのバージョンも出力する
			cmemMessage.AppendString( LS( STR_GREP_REGEX_DLL ) );	//L"    (正規表現:"
			cmemMessage.AppendString( cRegexp.GetVersionW() );
			cmemMessage.AppendString( L")\r\n" );
		}
	}

	if( CODE_AUTODETECT == sGrepOption.nGrepCharSet ){
		cmemMessage.AppendString( LS( STR_GREP_CHARSET_AUTODETECT ) );	//L"    (文字コードセットの自動判別)\r\n"
	}else if(IsValidCodeOrCPType(sGrepOption.nGrepCharSet)){
		cmemMessage.AppendString( LS( STR_GREP_CHARSET ) );	//L"    (文字コードセット："
		WCHAR szCpName[100];
		CCodePage::GetNameNormal(szCpName, sGrepOption.nGrepCharSet);
		cmemMessage.AppendString( szCpName );
		cmemMessage.AppendString( L")\r\n" );
	}

	if( 0 < nWork ){ // 2003.06.10 Moca ファイル検索の場合は表示しない // 2004.09.26 条件誤り修正
		if( sGrepOption.nGrepOutputLineType == 1 ){
			/* 該当行 */
			pszWork = LS( STR_GREP_SHOW_MATCH_LINE );	//L"    (一致した行を出力)\r\n"
		}else if( sGrepOption.nGrepOutputLineType == 2 ){
			// 否該当行
			pszWork = LS( STR_GREP_SHOW_MATCH_NOHITLINE );	//L"    (一致しなかった行を出力)\r\n"
		}else{
			if( bGrepReplace && sSearchOption.bRegularExp && !bGrepPaste ){
				pszWork = LS(STR_GREP_SHOW_FIRST_LINE);
			}else{
				pszWork = LS( STR_GREP_SHOW_MATCH_AREA );
			}
		}
		cmemMessage.AppendString( pszWork );

		if( sGrepOption.bGrepOutputFileOnly ){
			pszWork = LS( STR_GREP_SHOW_FIRST_MATCH );	//L"    (ファイル毎最初のみ検索)\r\n"
			cmemMessage.AppendString( pszWork );
		}
	}

	cmemMessage.AppendString( L"\r\n\r\n" );
	nWork = cmemMessage.GetStringLength();
	pszWork = cmemMessage.GetStringPtr();
//@@@ 2002.01.03 YAZAKI Grep直後はカーソルをGrep直前の位置に動かす
	CLayoutInt tmp_PosY_Layout = pcViewDst->m_pcEditDoc->m_cLayoutMgr.GetLineCount();
	if( 0 < nWork && sGrepOption.bGrepHeader ){
		AddTail( pcViewDst, cmemMessage, sGrepOption.bGrepStdout );
	}
	cmemMessage._SetStringLength(0);
	pszWork = NULL;
	
	//	2007.07.22 genta バージョンを取得するために，
	//	正規表現の初期化を上へ移動

	/* 表示処理ON/OFF */
	// 2003.06.23 Moca 共通設定で変更できるように
	// 2008.06.08 ryoji 全ビューの表示ON/OFFを同期させる
//	SetDrawSwitch(false);
	if( !CEditWnd::getInstance()->UpdateTextWrap() )	// 折り返し方法関連の更新
		CEditWnd::getInstance()->RedrawAllViews( pcViewDst );	//	他のペインの表示を更新
	const bool bDrawSwitchOld = pcViewDst->SetDrawSwitch(0 != GetDllShareData().m_Common.m_sSearch.m_bGrepRealTimeView);

	CGrepEnumOptions cGrepEnumOptions;
	CGrepEnumFiles cGrepExceptAbsFiles;
	cGrepExceptAbsFiles.Enumerates(L"", cGrepEnumKeys.m_vecExceptAbsFileKeys, cGrepEnumOptions);
	CGrepEnumFolders cGrepExceptAbsFolders;
	cGrepExceptAbsFolders.Enumerates(L"", cGrepEnumKeys.m_vecExceptAbsFolderKeys, cGrepEnumOptions);

	int nGrepTreeResult = 0;

	if( hWndTarget ){
		for( HWND hwnd = hWndTarget; NULL != hwnd; hwnd = NULL ){
			bool bOutputBaseFolder = false;
			bool bOutputFolderName = false;
			// 複数ウィンドウループ予約
			auto nPathLen = wcsnlen_s(szWindowPath, _countof(szWindowPath));
			std::wstring currentFile = szWindowPath;
			if( currentFile.size() ){
				currentFile += L'\\';
				nPathLen += 1;
			}
			currentFile += szWindowName;
			int nHitCount = nGrepTreeResult;
			int nTreeRet = DoGrepReplaceFile(
				pcViewDst,
				&cDlgCancel,
				hwnd,
				pcmGrepKey->GetStringPtr(),
				cmemReplace,
				szWindowName,
				sSearchOption,
				sGrepOption,
				pattern,
				&cRegexp,
				&nHitCount,
				currentFile.c_str(),
				szWindowPath,
				(sGrepOption.bGrepSeparateFolder && sGrepOption.bGrepOutputBaseFolder ? L"" : szWindowPath),
				(sGrepOption.bGrepSeparateFolder ? szWindowName : currentFile.c_str() + nPathLen),
				bOutputBaseFolder,
				bOutputFolderName,
				cmemMessage,
				cUnicodeBuffer,
				cOutBuffer
			);
			if( nTreeRet == -1 ){
				nGrepTreeResult = -1;
				break;
			}
			nGrepTreeResult += nTreeRet;
		}
		if( 0 < cmemMessage.GetStringLength() ){
			AddTail( pcViewDst, cmemMessage, sGrepOption.bGrepStdout );
			pcViewDst->GetCommander().Command_GOFILEEND( false );
			if( !CEditWnd::getInstance()->UpdateTextWrap() )
				CEditWnd::getInstance()->RedrawAllViews( pcViewDst );
			cmemMessage.Clear();
		}
		nHitCount = nGrepTreeResult;
	}else{
		for( int nPath = 0; nPath < (int)vPaths.size(); nPath++ ){
			bool bOutputBaseFolder = false;
			std::wstring sPath = ChopYen( vPaths[nPath] );
			int nTreeRet = DoGrepTree(
				pcViewDst,
				&cDlgCancel,
				pcmGrepKey->GetStringPtr(),
				cmemReplace,
				cGrepEnumKeys,
				cGrepExceptAbsFiles,
				cGrepExceptAbsFolders,
				sPath.c_str(),
				sPath.c_str(),
				sSearchOption,
				sGrepOption,
				pattern,
				&cRegexp,
				0,
				bOutputBaseFolder,
				&nHitCount,
				cmemMessage,
				cUnicodeBuffer,
				cOutBuffer
			);
			if( nTreeRet == -1 ){
				nGrepTreeResult = -1;
				break;
			}
			nGrepTreeResult += nTreeRet;
		}
		if( 0 < cmemMessage.GetStringLength() ) {
			AddTail( pcViewDst, cmemMessage, sGrepOption.bGrepStdout );
			cmemMessage._SetStringLength(0);
		}
	}
	if( -1 == nGrepTreeResult && sGrepOption.bGrepHeader ){
		const wchar_t* p = LS( STR_GREP_SUSPENDED );	//L"中断しました。\r\n"
		CNativeW cmemSuspend;
		cmemSuspend.SetString( p );
		AddTail( pcViewDst, cmemSuspend, sGrepOption.bGrepStdout );
	}
	if( sGrepOption.bGrepHeader ){
		WCHAR szBuffer[128];
		if( bGrepReplace ){
			auto_sprintf( szBuffer, LS(STR_GREP_REPLACE_COUNT), nHitCount );
		}else{
			auto_sprintf( szBuffer, LS( STR_GREP_MATCH_COUNT ), nHitCount );
		}
		CNativeW cmemOutput;
		cmemOutput.SetString( szBuffer );
		AddTail( pcViewDst, cmemOutput, sGrepOption.bGrepStdout );
#if defined(_DEBUG) && defined(TIME_MEASURE)
		auto_sprintf( szBuffer, LS(STR_GREP_TIMER), cRunningTimer.Read() );
		cmemOutput.SetString( szBuffer );
		AddTail( pcViewDst, cmemOutput, sGrepOption.bGrepStdout );
#endif
	}
	pcViewDst->GetCaret().MoveCursor( CLayoutPoint(CLayoutInt(0), tmp_PosY_Layout), true );	//	カーソルをGrep直前の位置に戻す。

	cDlgCancel.CloseDialog( 0 );

	/* アクティブにする */
	ActivateFrameWindow( CEditWnd::getInstance()->GetHwnd() );

	/* アンドゥバッファの処理 */
	pcViewDst->SetUndoBuffer();

	//	Apr. 13, 2001 genta
	//	Grep実行後はファイルを変更無しの状態にする．
	pcViewDst->m_pcEditDoc->m_cDocEditor.SetModified(false,false);

	this->m_bGrepRunning = false;
	pcViewDst->m_bDoing_UndoRedo = false;

	/* 表示処理ON/OFF */
	pCEditWnd->SetDrawSwitchOfAllViews( bDrawSwitchOld );

	/* 再描画 */
	if( !pCEditWnd->UpdateTextWrap() )	// 折り返し方法関連の更新	// 2008.06.10 ryoji
		pCEditWnd->RedrawAllViews( NULL );

	if( !bGrepCurFolder ){
		// 現行フォルダを検索したフォルダに変更
		if( 0 < vPaths.size() ){
			::SetCurrentDirectory( vPaths[0].c_str() );
		}
	}

	return nHitCount;
}

/*! @brief Grep実行

	@date 2001.06.27 genta	正規表現ライブラリの差し替え
	@date 2003.06.23 Moca   サブフォルダ→ファイルだったのをファイル→サブフォルダの順に変更
	@date 2003.06.23 Moca   ファイル名から""を取り除くように
	@date 2003.03.27 みく   除外ファイル指定の導入と重複検索防止の追加．
		大部分が変更されたため，個別の変更点記入は無し．
*/
int CGrepAgent::DoGrepTree(
	CEditView*				pcViewDst,
	CDlgCancel*				pcDlgCancel,		//!< [in] Cancelダイアログへのポインタ
	const wchar_t*			pszKey,				//!< [in] 検索キー
	const CNativeW&			cmGrepReplace,
	CGrepEnumKeys&			cGrepEnumKeys,		//!< [in] 検索対象ファイルパターン
	CGrepEnumFiles&			cGrepExceptAbsFiles,	//!< [in] 除外ファイル絶対パス
	CGrepEnumFolders&		cGrepExceptAbsFolders,	//!< [in] 除外フォルダ絶対パス
	const WCHAR*			pszPath,			//!< [in] 検索対象パス
	const WCHAR*			pszBasePath,		//!< [in] 検索対象パス(ベースフォルダ)
	const SSearchOption&	sSearchOption,		//!< [in] 検索オプション
	const SGrepOption&		sGrepOption,		//!< [in] Grepオプション
	const CSearchStringPattern& pattern,		//!< [in] 検索パターン
	CBregexp*				pRegexp,			//!< [in] 正規表現コンパイルデータ。既にコンパイルされている必要がある
	int						nNest,				//!< [in] ネストレベル
	bool&					bOutputBaseFolder,	//!< [i/o] ベースフォルダ名出力
	int*					pnHitCount,			//!< [i/o] ヒット数の合計
	CNativeW&				cmemMessage,		//!< [i/o] Grep結果文字列
	CNativeW&				cUnicodeBuffer,
	CNativeW&				cOutBuffer
)
{
	int			i;
	int			count;
	LPCWSTR		lpFileName;
	int			nWork = 0;
	int			nHitCountOld = -100;
	bool		bOutputFolderName = false;
	int			nBasePathLen = wcslen(pszBasePath);
	CGrepEnumOptions cGrepEnumOptions;
	CGrepEnumFilterFiles cGrepEnumFilterFiles;
	cGrepEnumFilterFiles.Enumerates( pszPath, cGrepEnumKeys, cGrepEnumOptions, cGrepExceptAbsFiles );

	/*
	 * カレントフォルダのファイルを探索する。
	 */
	count = cGrepEnumFilterFiles.GetCount();
	for( i = 0; i < count; i++ ){
		lpFileName = cGrepEnumFilterFiles.GetFileName( i );

		DWORD dwNow = ::GetTickCount();
		if( dwNow - m_dwTickUICheck > UICHECK_INTERVAL_MILLISEC ){
			m_dwTickUICheck = dwNow;
			/* 処理中のユーザー操作を可能にする */
			if( !::BlockingHook( pcDlgCancel->GetHwnd() ) ){
				goto cancel_return;
			}
			/* 中断ボタン押下チェック */
			if( pcDlgCancel->IsCanceled() ){
				goto cancel_return;
			}

			/* 表示設定をチェック */
			CEditWnd::getInstance()->SetDrawSwitchOfAllViews(
				0 != ::IsDlgButtonChecked( pcDlgCancel->GetHwnd(), IDC_CHECK_REALTIMEVIEW )
			);
		}

		// 定期的に grep 中のファイル名表示を更新
		if( dwNow - m_dwTickUIFileName > UIFILENAME_INTERVAL_MILLISEC ){
			m_dwTickUIFileName = dwNow;
			::DlgItem_SetText( pcDlgCancel->GetHwnd(), IDC_STATIC_CURFILE, lpFileName );
		}

		std::wstring currentFile = pszPath;
		currentFile += L"\\";
		currentFile += lpFileName;
		int nBasePathLen2 = nBasePathLen + 1;
		if( (int)wcslen(pszPath) < nBasePathLen2 ){
			nBasePathLen2 = nBasePathLen;
		}

		/* ファイル内の検索 */
		int nRet = DoGrepReplaceFile(
			pcViewDst,
			pcDlgCancel,
			NULL,
			pszKey,
			cmGrepReplace,
			lpFileName,
			sSearchOption,
			sGrepOption,
			pattern,
			pRegexp,
			pnHitCount,
			currentFile.c_str(),
			pszBasePath,
			(sGrepOption.bGrepSeparateFolder && sGrepOption.bGrepOutputBaseFolder ? pszPath + nBasePathLen2 : pszPath),
			(sGrepOption.bGrepSeparateFolder ? lpFileName : currentFile.c_str() + nBasePathLen + 1),
			bOutputBaseFolder,
			bOutputFolderName,
			cmemMessage,
			cUnicodeBuffer,
			cOutBuffer
		);

		// 2003.06.23 Moca リアルタイム表示のときは早めに表示
		if( pcViewDst->GetDrawSwitch() ){
			if( LTEXT('\0') != pszKey[0] ){
				// データ検索のときファイルの合計が最大10MBを超えたら表示
				nWork += ( cGrepEnumFilterFiles.GetFileSizeLow( i ) + 1023 ) / 1024;
			}
			if( 10000 < nWork ){
				nHitCountOld = -100; // 即表示
			}
		}
		/* 結果出力 */
		if( 0 < cmemMessage.GetStringLength() &&
		   (*pnHitCount - nHitCountOld) >= 10 &&
		   (::GetTickCount() - m_dwTickAddTail) > ADDTAIL_INTERVAL_MILLISEC
		){
			AddTail( pcViewDst, cmemMessage, sGrepOption.bGrepStdout );
			cmemMessage._SetStringLength(0);
			nWork = 0;
			nHitCountOld = *pnHitCount;
		}
		if( -1 == nRet ){
			goto cancel_return;
		}
	}

	/*
	 * サブフォルダを検索する。
	 */
	if( sGrepOption.bGrepSubFolder ){
		CGrepEnumOptions cGrepEnumOptionsDir;
		CGrepEnumFilterFolders cGrepEnumFilterFolders;
		cGrepEnumFilterFolders.Enumerates( pszPath, cGrepEnumKeys, cGrepEnumOptionsDir, cGrepExceptAbsFolders );

		count = cGrepEnumFilterFolders.GetCount();
		for( i = 0; i < count; i++ ){
			lpFileName = cGrepEnumFilterFolders.GetFileName( i );

			DWORD dwNow = ::GetTickCount();
			if( dwNow - m_dwTickUICheck > UICHECK_INTERVAL_MILLISEC ) {
				m_dwTickUICheck = dwNow;
				//サブフォルダの探索を再帰呼び出し。
				/* 処理中のユーザー操作を可能にする */
				if( !::BlockingHook( pcDlgCancel->GetHwnd() ) ){
					goto cancel_return;
				}
				/* 中断ボタン押下チェック */
				if( pcDlgCancel->IsCanceled() ){
					goto cancel_return;
				}
				/* 表示設定をチェック */
				CEditWnd::getInstance()->SetDrawSwitchOfAllViews(
					0 != ::IsDlgButtonChecked( pcDlgCancel->GetHwnd(), IDC_CHECK_REALTIMEVIEW )
				);
			}

			//フォルダ名を作成する。
			// 2010.08.01 キャンセルでメモリーリークしてました
			std::wstring currentPath  = pszPath;
			currentPath += L"\\";
			currentPath += lpFileName;

			int nGrepTreeResult = DoGrepTree(
				pcViewDst,
				pcDlgCancel,
				pszKey,
				cmGrepReplace,
				cGrepEnumKeys,
				cGrepExceptAbsFiles,
				cGrepExceptAbsFolders,
				currentPath.c_str(),
				pszBasePath,
				sSearchOption,
				sGrepOption,
				pattern,
				pRegexp,
				nNest + 1,
				bOutputBaseFolder,
				pnHitCount,
				cmemMessage,
				cUnicodeBuffer,
				cOutBuffer
			);
			if( -1 == nGrepTreeResult ){
				goto cancel_return;
			}
			::DlgItem_SetText( pcDlgCancel->GetHwnd(), IDC_STATIC_CURPATH, pszPath );	//@@@ 2002.01.10 add サブフォルダから戻ってきたら...
		}
	}

	::DlgItem_SetText( pcDlgCancel->GetHwnd(), IDC_STATIC_CURFILE, LTEXT(" ") );	// 2002/09/09 Moca add

	return 0;

cancel_return:;
	/* 結果出力 */
	if( 0 < cmemMessage.GetStringLength() ){
		AddTail( pcViewDst, cmemMessage, sGrepOption.bGrepStdout );
		cmemMessage._SetStringLength(0);
	}

	return -1;
}

/*!	@brief マッチした行番号と桁番号をGrep結果に出力する為に文字列化
	auto_sprintf 関数を 書式文字列 "(%I64d,%d)" で実行するのと同等の処理結果を生成
	高速化の為に自前実装に置き換え
	@return 出力先文字列
*/
template <size_t nCapacity>
static inline
wchar_t* lineColumnToString(
	wchar_t (&strWork)[nCapacity],	/*!< [out] 出力先 */
	LONGLONG	nLine,				/*!< [in] マッチした行番号(1～) */
	int			nColumn				/*!< [in] マッチした桁番号(1～) */
)
{
	// int2dec_destBufferSufficientLength 関数の
	// 戻り値から -1 しているのは終端0文字の分を削っている為
	constexpr size_t requiredMinimumCapacity =
		1		// (
		+ int2dec_destBufferSufficientLength<LONGLONG>() - 1	// I64d
		+ 1		// ,
		+ int2dec_destBufferSufficientLength<int32_t>() - 1	// %d
		+ 1		// )
		+ 1		// \0 終端0文字の分
	;
	static_assert(nCapacity >= requiredMinimumCapacity, "nCapacity not enough.");
	wchar_t* p = strWork;
	*p++ = L'(';
	p += int2dec(nLine, p);
	*p++ = L',';
	p += int2dec(nColumn, p);
	*p++ = L')';
	*p = '\0';
#ifdef _DEBUG
	// Debug 版に限って両方実行して、両者が一致することを確認
	wchar_t strWork2[requiredMinimumCapacity];
	::auto_sprintf( strWork2, L"(%I64d,%d)", nLine, nColumn );
	assert(wcscmp(strWork, strWork2) == 0);
#endif
	return strWork;
}

/*!	@brief Grep結果を構築する

	pWorkは充分なメモリ領域を持っているコト
	@date 2002/08/29 Moca バイナリーデータに対応 pnWorkLen 追加
	@date 2013.11.05 Moca cmemMessageに直接追加するように
*/
void CGrepAgent::SetGrepResult(
	/* データ格納先 */
	CNativeW& cmemMessage,
	/* マッチしたファイルの情報 */
	const WCHAR*		pszFilePath,	/*!< [in] フルパス or 相対パス*/
	const WCHAR*		pszCodeName,	/*!< [in] 文字コード情報．" [SJIS]"とか */
	/* マッチした行の情報 */
	LONGLONG	nLine,				/*!< [in] マッチした行番号(1～) */
	int			nColumn,			/*!< [in] マッチした桁番号(1～) */
	const wchar_t*	pMatchData,		/*!< [in] マッチした文字列 */
	int			nMatchLen,			/*!< [in] マッチした文字列の長さ */
	int			nEolCodeLen,		/*!< [in] EOLの長さ */
	/* マッチした文字列の情報 */
	/* オプション */
	const SGrepOption&	sGrepOption
)
{
	CNativeW cmemBuf(L"");
	wchar_t strWork[64];
	int k;
	bool bEOL = true;
	int nMaxOutStr = 0;

	/* ノーマル */
	if( 1 == sGrepOption.nGrepOutputStyle ){
		if( sGrepOption.bGrepOutputBaseFolder || sGrepOption.bGrepSeparateFolder ){
			cmemBuf.AppendString( L"・" );
		}
		cmemBuf.AppendString( pszFilePath );
		cmemBuf.AppendString( lineColumnToString(strWork, nLine, nColumn) );
		cmemBuf.AppendString( pszCodeName );
		cmemBuf.AppendString( L": " );
		nMaxOutStr = 2000; // 2003.06.10 Moca 最大長変更
	}
	/* WZ風 */
	else if( 2 == sGrepOption.nGrepOutputStyle ){
		::auto_sprintf( strWork, L"・(%6I64d,%-5d): ", nLine, nColumn );
		cmemBuf.AppendString( strWork );
		nMaxOutStr = 2500; // 2003.06.10 Moca 最大長変更
	}
	// 結果のみ
	else if( 3 == sGrepOption.nGrepOutputStyle ){
		nMaxOutStr = 2500;
	}

	k = nMatchLen;
	if( nMaxOutStr < k ){
		k = nMaxOutStr; // 2003.06.10 Moca 最大長変更
	}
	// 該当部分に改行を含む場合はその改行コードをそのまま利用する(次の行に空行を作らない)
	// 2003.06.10 Moca k==0のときにバッファアンダーランしないように
	if( 0 < k && WCODE::IsLineDelimiter(pMatchData[ k - 1 ], GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol) ){
		bEOL = false;
	}

	cmemMessage.AllocStringBuffer( cmemMessage.GetStringLength() + cmemBuf.GetStringLength() + 2 );
	cmemMessage.AppendNativeData( cmemBuf );
	cmemMessage.AppendString( pMatchData, k );
	if( bEOL ){
		cmemMessage.AppendString( L"\r\n", 2 );
	}
}

static void OutputPathInfo(
	CNativeW&		cmemMessage,
	SGrepOption		sGrepOption,
	const WCHAR*	pszFullPath,
	const WCHAR*	pszBaseFolder,
	const WCHAR*	pszFolder,
	const WCHAR*	pszRelPath,
	const WCHAR*	pszCodeName,
	bool&			bOutputBaseFolder,
	bool&			bOutputFolderName,
	BOOL&			bOutFileName
)
{
	{
		// バッファを2^n 分確保する
		int n = 1024;
		int size = cmemMessage.GetStringLength() + 300;
		while( n < size ){
			n *= 2;
		}
		cmemMessage.AllocStringBuffer( n );
	}
	if( 3 == sGrepOption.nGrepOutputStyle ){
		return;
	}

	if( !bOutputBaseFolder && sGrepOption.bGrepOutputBaseFolder ){
		if( !sGrepOption.bGrepSeparateFolder && 1 == sGrepOption.nGrepOutputStyle ){
			cmemMessage.AppendString( L"■\"" );
		}else{
			cmemMessage.AppendString( L"◎\"" );
		}
		cmemMessage.AppendString( pszBaseFolder );
		cmemMessage.AppendString( L"\"\r\n" );
		bOutputBaseFolder = true;
	}
	if( !bOutputFolderName && sGrepOption.bGrepSeparateFolder ){
		if( pszFolder[0] ){
			cmemMessage.AppendString( L"■\"" );
			cmemMessage.AppendString( pszFolder );
			cmemMessage.AppendString( L"\"\r\n" );
		}else{
			cmemMessage.AppendString( L"■\r\n" );
		}
		bOutputFolderName = true;
	}
	if( 2 == sGrepOption.nGrepOutputStyle ){
		if( !bOutFileName ){
			const WCHAR* pszDispFilePath = ( sGrepOption.bGrepSeparateFolder || sGrepOption.bGrepOutputBaseFolder ) ? pszRelPath : pszFullPath;
			if( sGrepOption.bGrepSeparateFolder ){
				cmemMessage.AppendString( L"◆\"" );
			}else{
				cmemMessage.AppendString( L"■\"" );
			}
			cmemMessage.AppendString( pszDispFilePath );
			cmemMessage.AppendString( L"\"" );
			cmemMessage.AppendString( pszCodeName );
			cmemMessage.AppendString( L"\r\n" );
			bOutFileName = TRUE;
		}
	}
}

class CError_WriteFileOpen
{
public:
	virtual ~CError_WriteFileOpen(){}
};

class CWriteData{
public:
	CWriteData(int& hit, LPCWSTR name, ECodeType code_, bool bBom_, bool bOldSave_, CNativeW& message)
		:nHitCount(hit)
		,fileName(name)
		,code(code_)
		,bBom(bBom_)
		,bOldSave(bOldSave_)
		,bufferSize(0)
		,out(NULL)
		,pcCodeBase(CCodeFactory::CreateCodeBase(code_,0))
		,memMessage(message)
		{}
	void AppendBuffer(const CNativeW& strLine)
	{
		if( !out ){
			bufferSize += strLine.GetStringLength();
			buffer.push_back(strLine);
			// 10MB 以上だったら出力してしまう
			if( 0xa00000 <= bufferSize ){
				OutputHead();
			}
		}else{
			Output(strLine);
		}
	}
	void OutputHead()
	{
		if( !out ){
			std::wstring name = fileName;
			name += L".skrnew";
			try{
				out = new CBinaryOutputStream(name.c_str(), true);
			}catch( const CError_FileOpen& ){
				throw CError_WriteFileOpen();
			}
			if( bBom ){
				CMemory cBom;
				pcCodeBase->GetBom(&cBom);
				out->Write(cBom.GetRawPtr(), cBom.GetRawLength());
			}
			for(size_t i = 0; i < buffer.size(); i++){
				Output(buffer[i]);
			}
			buffer.clear();
			std::deque<CNativeW>().swap(buffer);
		}
	}
	void Output(const CNativeW& strLine)
	{
		CMemory dest;
		pcCodeBase->UnicodeToCode(strLine, &dest);
		// 場合によっては改行ごとではないので、JIS/UTF-7での出力が一定でない可能性あり
		out->Write(dest.GetRawPtr(), dest.GetRawLength());
	}
	void Close()
	{
		if( nHitCount && out ){
			out->Close();
			delete out;
			out = NULL;
			if( bOldSave ){
				std::wstring oldFile = fileName;
				oldFile += L".bak";
				if( fexist(oldFile.c_str()) ){
					if( FALSE == ::DeleteFile( oldFile.c_str() ) ){
						memMessage.AppendString( LS(STR_GREP_REP_ERR_DELETE) );
						memMessage.AppendStringF( L"[%s]\r\n", oldFile.c_str());
						return;
					}
				}
				if( FALSE == ::MoveFile( fileName, oldFile.c_str() ) ){
					memMessage.AppendString( LS(STR_GREP_REP_ERR_REPLACE) );
					memMessage.AppendStringF( L"[%s]\r\n", oldFile.c_str());
					return;
				}
			}else{
				if( FALSE == ::DeleteFile( fileName ) ){
					memMessage.AppendString( LS(STR_GREP_REP_ERR_DELETE) );
					memMessage.AppendStringF( L"[%s]\r\n", fileName );
					return;
				}
			}
			std::wstring name(fileName);
			name += L".skrnew";
			if( FALSE == ::MoveFile( name.c_str(), fileName ) ){
				memMessage.AppendString( LS(STR_GREP_REP_ERR_REPLACE) );
				memMessage.AppendStringF( L"[%s]\r\n", fileName );
				return;
			}
		}
		return;
	}
	~CWriteData()
	{
		if( out ){
			out->Close();
			delete out;
			out = NULL;
			std::wstring name(fileName);
			name += L".skrnew";
			::DeleteFile( name.c_str() );
		}
	}
private:
	int& nHitCount;
	LPCWSTR fileName;
	ECodeType code;
	bool bBom;
	bool bOldSave;
	size_t bufferSize;
	std::deque<CNativeW> buffer;
	CBinaryOutputStream* out;
	std::unique_ptr<CCodeBase> pcCodeBase;
	CNativeW&	memMessage;
};

/*!
	Grep検索・置換実行
*/
int CGrepAgent::DoGrepReplaceFile(
	CEditView*				pcViewDst,
	CDlgCancel*				pcDlgCancel,
	HWND					hWndTarget,			//!< [in] 対象Windows(NULLでファイル)
	const wchar_t*			pszKey,
	const CNativeW&			cmGrepReplace,
	const WCHAR*			pszFile,
	const SSearchOption&	sSearchOption,
	const SGrepOption&		sGrepOption,
	const CSearchStringPattern& pattern,
	CBregexp*				pRegexp,		//	Jun. 27, 2001 genta	正規表現ライブラリの差し替え
	int*					pnHitCount,
	const WCHAR*			pszFullPath,
	const WCHAR*			pszBaseFolder,
	const WCHAR*			pszFolder,
	const WCHAR*			pszRelPath,
	bool&					bOutputBaseFolder,
	bool&					bOutputFolderName,
	CNativeW&				cmemMessage,
	CNativeW&				cUnicodeBuffer,
	CNativeW&				cOutBuffer
)
{
	LONGLONG	nLine = 0;
	int			nHitCount = 0;
	ECodeType	nCharCode;
	BOOL		bOutFileName = FALSE;
	CEol		cEol;
	int			nEolCodeLen;
	int			nOldPercent = 0;
	int			nKeyLen = wcslen( pszKey );
	const WCHAR*	pszCodeName = L"";

	const STypeConfigMini* type = NULL;
	if( !CDocTypeManager().GetTypeConfigMini( CDocTypeManager().GetDocumentTypeOfPath( pszFile ), &type ) ){
		return -1;
	}
	CFileLoadOrWnd	cfl( type->m_encoding, hWndTarget );	// 2012/12/18 Uchi 検査するファイルのデフォルトの文字コードを取得する様に
	bool bBom;
	// ファイル名表示
	const WCHAR* pszDispFilePath = ( sGrepOption.bGrepSeparateFolder || sGrepOption.bGrepOutputBaseFolder ) ? pszRelPath : pszFullPath;
	
	try{
		// ファイルを開く
		// FileCloseで明示的に閉じるが、閉じていないときはデストラクタで閉じる
		// 2003.06.10 Moca 文字コード判定処理もFileOpenで行う
		nCharCode = cfl.FileOpen( pszFullPath, true, sGrepOption.nGrepCharSet, GetDllShareData().m_Common.m_sFile.GetAutoMIMEdecode(), &bBom );
		std::unique_ptr<CWriteData> pOutput;
		
		if( sGrepOption.bGrepReplace ){
			pOutput.reset( new CWriteData( nHitCount, pszFullPath, nCharCode, bBom, sGrepOption.bGrepBackup, cmemMessage ));
		}
		
		WCHAR szCpName[100];
		if( CODE_AUTODETECT == sGrepOption.nGrepCharSet ){
			if( IsValidCodeType(nCharCode) ){
				wcscpy( szCpName, CCodeTypeName(nCharCode).Bracket() );
				pszCodeName = szCpName;
			}else{
				CCodePage::GetNameBracket(szCpName, nCharCode);
				pszCodeName = szCpName;
			}
		}
		
		DWORD dwNow = ::GetTickCount();
		if ( dwNow - m_dwTickUICheck > UICHECK_INTERVAL_MILLISEC ) {
			m_dwTickUICheck = dwNow;
			/* 処理中のユーザー操作を可能にする */
			if( !::BlockingHook( pcDlgCancel->GetHwnd() ) ){
				return -1;
			}
			/* 中断ボタン押下チェック */
			if( pcDlgCancel->IsCanceled() ){
				return -1;
			}
		}
		int nOutputHitCount = 0;
		
		// 注意 : cfl.ReadLine が throw する可能性がある
		
		// next line callback 設定
		CGrepDocInfo GrepLineInfo( &cfl, &cUnicodeBuffer, &cEol, &nLine );
		pRegexp->SetNextLineCallback( GetNextLine, &GrepLineInfo );
		
		LONGLONG iMatchLinePrev = -1;
		
		while( RESULT_FAILURE != ReadLine( &GrepLineInfo ))
		{
			const wchar_t*	pLine = cUnicodeBuffer.GetStringPtr();
			int		nLineLen = cUnicodeBuffer.GetStringLength();
			
			nEolCodeLen = cEol.GetLen();
			
			DWORD dwNow = ::GetTickCount();
			if( dwNow - m_dwTickUICheck > UICHECK_INTERVAL_MILLISEC ){
				m_dwTickUICheck = dwNow;
				/* 処理中のユーザー操作を可能にする */
				if( !::BlockingHook( pcDlgCancel->GetHwnd() ) ){
					return -1;
				}
				/* 中断ボタン押下チェック */
				if( pcDlgCancel->IsCanceled() ){
					return -1;
				}
				//	2003.06.23 Moca 表示設定をチェック
				CEditWnd::getInstance()->SetDrawSwitchOfAllViews(
					0 != ::IsDlgButtonChecked( pcDlgCancel->GetHwnd(), IDC_CHECK_REALTIMEVIEW )
				);
				// 2002/08/30 Moca 進行状態を表示する(5MB以上)
				if( 5000000 < cfl.GetFileSize() ){
					int nPercent = cfl.GetPercent();
					if( 5 <= nPercent - nOldPercent ){
						nOldPercent = nPercent;
						WCHAR szWork[10];
						::auto_sprintf( szWork, L" (%3d%%)", nPercent );
						std::wstring str;
						str = str + pszFile + szWork;
						::DlgItem_SetText( pcDlgCancel->GetHwnd(), IDC_STATIC_CURFILE, str.c_str() );
					}
				}else{
					::DlgItem_SetText( pcDlgCancel->GetHwnd(), IDC_STATIC_CURFILE, pszFile );
				}
				::SetDlgItemInt( pcDlgCancel->GetHwnd(), IDC_STATIC_HITCOUNT, *pnHitCount, FALSE );
				::DlgItem_SetText( pcDlgCancel->GetHwnd(), IDC_STATIC_CURPATH, pszFolder );
			}
			int nHitOldLine = nHitCount;
			int nHitCountOldLine = *pnHitCount;
			
			if( sGrepOption.bGrepReplace ) cOutBuffer.SetString( L"", 0 );
			
			// replace 時の結果 2回目以降の表示
			bool bOutput = true;
			if( sGrepOption.bGrepOutputFileOnly && 1 <= nHitCount ){
				bOutput = false;
			}
			
			/* 正規表現検索 */
			int nIndex = 0;
			int nMatchNum = 0;
			
			LONGLONG iLineDisp = nLine;
			
			bool bRestartGrep = true; // search_buf に残っているデータが対象の場合 false
			
			// 行単位の処理
			while( nIndex <= nLineLen ){
				
				// マッチ
				bool bMatch = bRestartGrep ?
					pRegexp->Match( pLine, nLineLen, nIndex ) :
					pRegexp->Match( nullptr, 0, nIndex );
				bRestartGrep = false;
				
				if( sGrepOption.nGrepOutputLineType == 2/*非該当行*/ ) bMatch = !bMatch;
				if( !bMatch ) break;
				
				// 置換
				if( sGrepOption.bGrepReplace && !sGrepOption.bGrepPaste ){
					if( pRegexp->Replace( cmGrepReplace.GetStringPtr()) < 0 ) throw CError_Regex();
				}
				
				// cat した serch buf に切り替わっているかもしれないので，pLine nLineLen をそちらに更新
				pLine		= pRegexp->GetSubject();
				nLineLen	= pRegexp->GetSubjectLen();
				
				// log 表示用行，match 位置
				int iLogMatchIdx;
				int iLogMatchLen;
				int iLogMatchLineOffs;
				
				pRegexp->GetMatchLine( &iLogMatchIdx, &iLogMatchLen, &iLogMatchLineOffs );
				
				if( sGrepOption.nGrepOutputLineType == 0/*該当部分*/ ){
					iLogMatchIdx = pRegexp->GetIndex();
					iLogMatchLen = pRegexp->GetMatchLen();
				}
				
				++nHitCount;
				++(*pnHitCount);
				
				if( bOutput ){
					
					if(
						sGrepOption.nGrepOutputLineType == 0 ||
						 iMatchLinePrev != iLineDisp + iLogMatchLineOffs
					){
						
						iMatchLinePrev = iLineDisp + iLogMatchLineOffs;
						
						OutputPathInfo(
							cmemMessage, sGrepOption,
							pszFullPath, pszBaseFolder, pszFolder, pszRelPath, pszCodeName,
							bOutputBaseFolder, bOutputFolderName, bOutFileName
						);
						
						/* Grep結果を、cmemMessageに格納する */
						SetGrepResult(
							cmemMessage, pszDispFilePath, pszCodeName,
							iMatchLinePrev,			// line
							iLogMatchIdx + 1,		// column
							pLine + iLogMatchIdx,	// match str
							iLogMatchLen,			// match str len
							nEolCodeLen,			// eol len
							sGrepOption
						);
					}
					
					// 
					if( sGrepOption.nGrepOutputLineType != 0 || sGrepOption.bGrepOutputFileOnly ){
						if( sGrepOption.bGrepReplace ){
							bOutput = false;	// replace 時は，出力のみ抑制
						}else{
							break;				// grep 時は行処理終了
						}
					}
				}
				
				// 置換結果文字列の生成
				if( sGrepOption.bGrepReplace ){
					pOutput->OutputHead();
					
					// hit 位置直前までをコピー
					cOutBuffer.AppendString( pRegexp->GetSubject() + nIndex, pRegexp->GetIndex() - nIndex );
					
					// 置換後文字列をコピー
					if( sGrepOption.bGrepPaste ){
						// クリップボード
						cOutBuffer.AppendNativeData( cmGrepReplace );
					}else{
						// regexp 置換結果
						cOutBuffer.AppendString( pRegexp->GetReplacedString(), pRegexp->GetReplacedLen());
					}
				}
				
				// 次検索開始位置
				nIndex = pRegexp->GetIndex() + pRegexp->GetMatchLen();
				
				// 0 幅マッチの場合 1文字進める
				if( pRegexp->GetMatchLen() == 0 ) ++nIndex;
			}
			
			if( sGrepOption.bGrepReplace ){
				// 何も引っかからなかったので，nIndex 以降をコピー
				cOutBuffer.AppendString( pLine + nIndex, nLineLen - nIndex );
				pOutput->AppendBuffer(cOutBuffer);
			}
			
			if( 0 < cmemMessage.GetStringLength() &&
			   (nHitCount - nOutputHitCount >= 10) &&
			   (::GetTickCount() - m_dwTickAddTail > ADDTAIL_INTERVAL_MILLISEC)
			){
				nOutputHitCount = nHitCount;
				AddTail( pcViewDst, cmemMessage, sGrepOption.bGrepStdout );
				cmemMessage._SetStringLength(0);
			}
			
			// ファイル検索の場合は、1つ見つかったら終了
			if( !sGrepOption.bGrepReplace && sGrepOption.bGrepOutputFileOnly && 1 <= nHitCount ){
				break;
			}
		}
		
		// ファイルを明示的に閉じるが、ここで閉じないときはデストラクタで閉じている
		cfl.FileClose();
		if( sGrepOption.bGrepReplace) pOutput->Close();
	} // try
	
	catch( const CError_Regex& ){
		pRegexp->ShowErrorMsg();
		return -1;	// ファイル探索も終了
	}
	catch( const CError_FileOpen& ){
		CNativeW str(LS(STR_GREP_ERR_FILEOPEN));
		str.Replace(L"%s", pszFullPath);
		cmemMessage.AppendNativeData( str );
		return 0;
	}
	catch( const CError_FileRead& ){
		CNativeW str(LS(STR_GREP_ERR_FILEREAD));
		str.Replace(L"%s", pszFullPath);
		cmemMessage.AppendNativeData( str );
	}
	catch( const CError_WriteFileOpen& ){
		std::wstring file = pszFullPath;
		file += L".skrnew";
		CNativeW str(LS(STR_GREP_ERR_FILEWRITE));
		str.Replace(L"%s", file.c_str());
		cmemMessage.AppendNativeData( str );
	} // 例外処理終わり

	return nHitCount;
}

// 次行取得
EConvertResult CGrepAgent::ReadLine( CGrepDocInfo *pInfo ){
	++*pInfo->m_piLine;
	
	// unget 後の get
	if( pInfo->m_bUnget ){
		pInfo->m_bUnget = false;
		return pInfo->m_cPrevResult;
	}
	
	return pInfo->m_cPrevResult = pInfo->m_pFileLoad->ReadLine( pInfo->m_pLineBuf, pInfo->m_pEol );
}

int CGrepAgent::GetNextLine( const wchar_t **ppNextLine, void *m_pParam ){
	
	CGrepDocInfo *pInfo = static_cast<CGrepDocInfo *>( m_pParam );
	
	// unget
	if( !ppNextLine ){
		assert( !pInfo->m_bUnget );	// 2回以上の unget 不可
		--*pInfo->m_piLine;
		pInfo->m_bUnget = true;
		return 0;
	}
	
	// 次行なし
	if( ReadLine( pInfo ) == RESULT_FAILURE ){
		return 0;
	}
	
	// 次行取得
	*ppNextLine = pInfo->m_pLineBuf->GetStringPtr();
	return pInfo->m_pLineBuf->GetStringLength();
}
