/*! @file */
/*
	Copyright (C) 2018-2022, Sakura Editor Organization

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
#define ADDTAIL_INTERVAL_MILLISEC 1000	// 結果出力の時間間隔
#define UIFILENAME_INTERVAL_MILLISEC 200	// Cancelダイアログのファイル名表示更新間隔

#define GREP_MATCH_POS		0
#define GREP_MATCH_LINE		1
#define GREP_NOT_MATCH_LINE	2

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
, m_uTaskId( 0 )
, m_uTaskIdDisp( 0 )
, m_bStop( false )
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

	// 最後のフォルダー区切り記号を削除する
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
	hWndTarget = nullptr;	//out引数をクリアする

	constexpr auto& szTargetPrefix = L":HWND:";
	constexpr auto cchTargetPrefix = _countof(szTargetPrefix) - 1;
	if( 0 != wcsncmp(pszFile, szTargetPrefix, cchTargetPrefix) ){
		return 0; // ハンドルGrepではない
	}
	if( 0 >= ::swscanf_s(pszFile + cchTargetPrefix, L"%x", (size_t*)&hWndTarget) || !IsSakuraMainWindow(hWndTarget) ){
		return -1; // ハンドルを読み取れなかった、または、対象ウインドウハンドルが存在しない
	}
	if( pmemTitle ){
		const wchar_t* p = L"Window:[";
		pmemTitle->SetStringHoldBuffer(p, 8);
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
  @param[in] pcmGrepFolder 検索対象フォルダー

  @date 2008.12.07 nasukoji	ファイル名パターンのバッファオーバラン対策
  @date 2008.12.13 genta 検索パターンのバッファオーバラン対策
  @date 2012.10.13 novice 検索オプションをクラスごと代入
*/
DWORD CGrepAgent::DoGrep(
	CEditView*				pcViewDst,
	const CNativeW*			pcmGrepKey,
	const CNativeW*			pcmGrepReplace,
	const CNativeW*			pcmGrepFile,
	const CNativeW*			pcmGrepFolder,
	const SSearchOption&	sSearchOption,
	SGrepOption&			sGrepOption
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
	CNativeW	cmemMessage;
	int			nWork;

	// バッファサイズの調整
	cmemMessage.AllocStringBuffer( 4000 );

	pcViewDst->m_bDoing_UndoRedo		= true;
	
	// タイマー値リセット
	m_dwTickAddTail =
	m_dwTickUICheck =
	m_dwTickUIFileName = ::GetTickCount() - 100 * 1000;
	
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
	if( sGrepOption.bGrepReplace ){
		if( sGrepOption.bGrepPaste ){
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
	GetEditWnd().m_cDlgGrep.m_strText = pcmGrepKey->GetStringPtr();
	GetEditWnd().m_cDlgGrep.m_bSetText = true;
	GetEditWnd().m_cDlgGrepReplace.m_strText = pcmGrepKey->GetStringPtr();
	if( sGrepOption.bGrepReplace ){
		GetEditWnd().m_cDlgGrepReplace.m_strText2 = pcmGrepReplace->GetStringPtr();
	}
	GetEditWnd().m_cDlgGrepReplace.m_bSetText = true;
	hwndCancel = cDlgCancel.DoModeless( G_AppInstance(), pcViewDst->m_hwndParent, IDD_GREPRUNNING );

	::SetDlgItemInt( hwndCancel, IDC_STATIC_HITCOUNT, 0, FALSE );
	::DlgItem_SetText( hwndCancel, IDC_STATIC_CURFILE, L" " );	// 2002/09/09 Moca add
	::CheckDlgButton( hwndCancel, IDC_CHECK_REALTIMEVIEW, GetDllShareData().m_Common.m_sSearch.m_bGrepRealTimeView );	// 2003.06.23 Moca

	//	2008.12.13 genta パターンが長すぎる場合は登録しない
	//	(正規表現が途中で途切れると困るので)
	//	2011.12.10 Moca 表示の際に...に切り捨てられるので登録するように
	wcsncpy_s( CAppMode::getInstance()->m_szGrepKey, _countof(CAppMode::getInstance()->m_szGrepKey), pcmGrepKey->GetStringPtr(), _TRUNCATE );
	this->m_bGrepMode = true;

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

	if( sGrepOption.bGrepReplace ){
		cmemMessage.AppendString( LS(STR_GREP_REPLACE_TO) );
		if( sGrepOption.bGrepPaste ){
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

	cmemMessage.AppendString( LS( STR_GREP_SEARCH_FOLDER ) );	//L"フォルダー   "
	{
		// フォルダーリストから末尾のバックスラッシュを削ったパスリストを作る
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

	cmemMessage.AppendString(LS(STR_GREP_EXCLUDE_FOLDER));	//L"除外フォルダー   "
	{
		// 除外フォルダーの解析済みリストを取得する
		auto excludeFolders = cGrepEnumKeys.GetExcludeFolders();
		std::wstring strPatterns = FormatPathList( excludeFolders );
		cmemMessage.AppendString( strPatterns.c_str(), strPatterns.length() );
	}
	cmemMessage.AppendString(L"\r\n");

	const wchar_t*	pszWork;
	if( sGrepOption.bGrepSubFolder ){
		pszWork = LS( STR_GREP_SUBFOLDER_YES );	//L"    (サブフォルダーも検索)\r\n"
	}else{
		pszWork = LS( STR_GREP_SUBFOLDER_NO );	//L"    (サブフォルダーを検索しない)\r\n"
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
		if( sGrepOption.nGrepOutputLineType == GREP_MATCH_LINE ){
			/* 該当行 */
			pszWork = LS( STR_GREP_SHOW_MATCH_LINE );	//L"    (一致した行を出力)\r\n"
		}else if( sGrepOption.nGrepOutputLineType == GREP_NOT_MATCH_LINE ){
			// 否該当行
			pszWork = LS( STR_GREP_SHOW_MATCH_NOHITLINE );	//L"    (一致しなかった行を出力)\r\n"
		}else{
			if( sGrepOption.bGrepReplace && sSearchOption.bRegularExp && !sGrepOption.bGrepPaste ){
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
	cmemMessage.Clear();
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
	
	// スレッド毎 obj 生成
	UINT uMaxThreadNum = hWndTarget ? 1 : std::thread::hardware_concurrency();
	
	CSearchStringPattern pattern;
	m_cRegexp.reserve( uMaxThreadNum );
	m_cUnicodeBuffer.reserve( uMaxThreadNum );
	if( sGrepOption.bGrepReplace ) m_cOutBuffer.reserve( uMaxThreadNum );
	
	for( UINT u = 0; u < uMaxThreadNum; ++u ){
		// パターンコンパイル
		m_cRegexp.emplace_back();
		bool bError = !pattern.SetPattern(
			pcViewDst->GetHwnd(), pcmGrepKey->GetStringPtr(), pcmGrepKey->GetStringLength(),
			sSearchOption, &m_cRegexp[ u ]
		);
		if( bError ){
			this->m_bGrepRunning = false;
			pcViewDst->m_bDoing_UndoRedo = false;
			pcViewDst->SetUndoBuffer();
			return 0;
		}
		
		m_cUnicodeBuffer.emplace_back();
		m_cUnicodeBuffer[ u ].AllocStringBuffer( 4000 );
		
		if( sGrepOption.bGrepReplace ){
			m_cOutBuffer.emplace_back();
			m_cOutBuffer[ u ].AllocStringBuffer( 4000 );
		}
	}
	
	// Grepオプションまとめ
	if( sGrepOption.bGrepReplace ){
		// Grep否定行はGrep置換では無効
		if( sGrepOption.nGrepOutputLineType == GREP_NOT_MATCH_LINE ){
			sGrepOption.nGrepOutputLineType = GREP_MATCH_LINE; // 行単位
		}
	}

	tGrepArg Arg = {
		// window
		pcViewDst,
		&cDlgCancel,					//!< [in] Cancelダイアログへのポインタ
		
		// grep 情報
		pcmGrepKey->GetStringPtr(),		//!< [in] 検索キー
		cmemReplace,					//!< [in] 置換後文字列
		cGrepEnumKeys,					//!< [in] 検索対象ファイルパターン
		cGrepExceptAbsFiles,			//!< [in] 除外ファイル絶対パス
		cGrepExceptAbsFolders,			//!< [in] 除外フォルダー絶対パス
		sSearchOption,					//!< [in] 検索オプション
		sGrepOption,					//!< [in] Grepオプション
		nullptr,						//!< [in] 正規表現コンパイルデータ。既にコンパイルされている必要がある
		
		&nHitCount,						//!< [i/o] ヒット数の合計
		
		// buffer
		&cmemMessage,					//!< [i/o] Grep結果文字列
		nullptr,						//!< [i/o] ファイルオーブンバッファ
		nullptr							//!< [o] 置換後ファイルバッファ
	};
	
	int nTreeRet = 0;
	if( hWndTarget ){
		Arg.pRegexp			= &m_cRegexp[ 0 ];
		Arg.pcUnicodeBuffer	= &m_cUnicodeBuffer[ 0 ];
		if( sGrepOption.bGrepReplace ){
			Arg.pcOutBuffer		= &m_cOutBuffer[ 0 ];
		}
		
		nTreeRet = DoGrepReplaceFile( &Arg, hWndTarget, szWindowName, szWindowPath );
	}else{
		// ワーカースレッド起動
		StartWorkerThread( uMaxThreadNum, &Arg );
		
		for( int nPath = 0; nPath < (int)vPaths.size(); nPath++ ){
			std::wstring sPath = ChopYen( vPaths[nPath] );
			nTreeRet = DoGrepTree( &Arg, 0, sPath.c_str());
			if( nTreeRet == -1 ) break;
		}
		
		// grep 終了待ち
		if( nTreeRet >= 0 ) while( m_uTaskIdDisp < m_uTaskId ){
			{
				std::unique_lock<std::mutex> lock(m_Mutex);
				m_Condition.wait_for( lock, std::chrono::milliseconds( UICHECK_INTERVAL_MILLISEC ), [this]{ return m_Result[ m_uTaskIdDisp ]; });
			}
			
			nTreeRet = UpdateResult( &Arg );
			if( nTreeRet < 0 ) break;
		}
		
		// ワーカースレッド終了まち
		Join();
		
		for( UINT u = 0; u < m_Result.size(); ++u ) delete m_Result[ u ];
	}
	
	// スレッド毎 object 破棄
	{
		std::vector<std::thread>	Workers			= std::move( m_Workers );
		std::queue<CGrepTask>		Tasks			= std::move( m_Tasks );
		std::vector<CGrepResult*>	Result			= std::move( m_Result );
		std::vector<CBregexp>		cRegexp			= std::move( m_cRegexp );
		std::vector<CNativeW>		cUnicodeBuffer	= std::move( m_cUnicodeBuffer );
		std::vector<CNativeW>		cOutBuffer		= std::move( m_cOutBuffer );
	}
	
	if( -1 == nTreeRet && sGrepOption.bGrepHeader ){
		cmemMessage.AppendString( LS( STR_GREP_SUSPENDED ));	//L"中断しました。\r\n"
	}
	if( sGrepOption.bGrepHeader ){
		WCHAR szBuffer[128];
		if( sGrepOption.bGrepReplace ){
			auto_sprintf( szBuffer, LS(STR_GREP_REPLACE_COUNT), nHitCount );
		}else{
			auto_sprintf( szBuffer, LS( STR_GREP_MATCH_COUNT ), nHitCount );
		}
		cmemMessage.AppendString( szBuffer );
#if defined(_DEBUG) && defined(TIME_MEASURE)
		auto_sprintf( szBuffer, LS(STR_GREP_TIMER), cRunningTimer.Read() );
		cmemMessage.AppendString( szBuffer );
#endif
	}
	AddTail( pcViewDst, cmemMessage, sGrepOption.bGrepStdout );
	cmemMessage.Clear();
	
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

	if( !sGrepOption.bGrepCurFolder ){
		// 現行フォルダーを検索したフォルダーに変更
		if( 0 < vPaths.size() ){
			::SetCurrentDirectory( vPaths[0].c_str() );
		}
	}

	return nHitCount;
}

/*! @brief Grep実行

	@date 2001.06.27 genta	正規表現ライブラリの差し替え
	@date 2003.06.23 Moca   サブフォルダー→ファイルだったのをファイル→サブフォルダーの順に変更
	@date 2003.06.23 Moca   ファイル名から""を取り除くように
	@date 2003.03.27 みく   除外ファイル指定の導入と重複検索防止の追加．
		大部分が変更されたため，個別の変更点記入は無し．
*/
int CGrepAgent::DoGrepTree(
	tGrepArg				*pArg,
	int						nNest,				//!< [in] ネストレベル
	const WCHAR*			pszPath				//!< [in] 検索対象パス，末尾の \ 無し
)
{
	int			i;
	int			count;
	LPCWSTR		lpFileName;
	int			nWork = 0;
	int			nHitCountOld = -100;
	CGrepEnumOptions cGrepEnumOptions;
	CGrepEnumFilterFiles cGrepEnumFilterFiles;
	cGrepEnumFilterFiles.Enumerates( pszPath, pArg->cGrepEnumKeys, cGrepEnumOptions, pArg->cGrepExceptAbsFiles );

	/*
	 * カレントフォルダーのファイルを探索する。
	 */
	count = cGrepEnumFilterFiles.GetCount();
	for( i = 0; i < count; i++ ){
		lpFileName = cGrepEnumFilterFiles.GetFileName( i );
		
		// ファイル内の検索タスクをキューに詰む
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Result.emplace_back();
			m_Tasks.emplace( m_uTaskId, lpFileName, pszPath );
		}
		//MYTRACE( L"++task %d queued.\n", m_uTaskId );
		m_Condition.notify_one();
		++m_uTaskId;
		
		// Grep 結果更新
		if( UpdateResult( pArg ) < 0 ) return -1;
	}

	/*
	 * サブフォルダーを検索する。
	 */
	if( pArg->sGrepOption.bGrepSubFolder ){
		CGrepEnumOptions cGrepEnumOptionsDir;
		CGrepEnumFilterFolders cGrepEnumFilterFolders;
		cGrepEnumFilterFolders.Enumerates( pszPath, pArg->cGrepEnumKeys, cGrepEnumOptionsDir, pArg->cGrepExceptAbsFolders );

		count = cGrepEnumFilterFolders.GetCount();
		for( i = 0; i < count; i++ ){
			// Grep 結果更新
			if( UpdateResult( pArg )) return -1;
			
			lpFileName = cGrepEnumFilterFolders.GetFileName( i );

			//フォルダー名を作成する。
			// 2010.08.01 キャンセルでメモリリークしてました
			std::wstring currentPath  = pszPath;
			currentPath += L"\\";
			currentPath += lpFileName;

			if( DoGrepTree( pArg, nNest + 1, currentPath.c_str()) < 0 ) return -1;
		}
	}

	return 0;
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
	const WCHAR*	pszCodeName,
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

	if( 2 == sGrepOption.nGrepOutputStyle ){
		if( !bOutFileName ){
			const WCHAR* pszDispFilePath = pszFullPath;
			cmemMessage.AppendString( L"■\"" );
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
	CWriteData(int& hit, LPCWSTR name_, ECodeType code_, bool bBom_, bool bOldSave_, CNativeW& message)
		:nHitCount(hit)
		,fileName(name_)
		,name(name_)
		,code(code_)
		,bBom(bBom_)
		,bOldSave(bOldSave_)
		,bufferSize(0)
		,out(NULL)
		,pcCodeBase(CCodeFactory::CreateCodeBase(code_,0))
		,memMessage(message)
	{
		name += L".skrnew";
	}
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
			::DeleteFile( name.c_str() );
		}
	}
private:
	int& nHitCount;
	LPCWSTR fileName;
	std::wstring name;
	ECodeType code;
	bool bBom;
	bool bOldSave;
	size_t bufferSize;
	std::deque<CNativeW> buffer;
	CBinaryOutputStream* out;
	std::unique_ptr<CCodeBase> pcCodeBase;
	CNativeW&	memMessage;
};

//****************************************************************************
// Grep worker スレッド

void CGrepAgent::GrepThread( UINT uId, tGrepArg *pArg ){
	tGrepArg Arg = {
		// window
		pArg->pcViewDst,
		pArg->pcDlgCancel,					//!< [in] Cancelダイアログへのポインタ
		
		// grep 情報
		pArg->pszKey,						//!< [in] 検索キー
		pArg->cmGrepReplace,				//!< [in] 置換後文字列
		pArg->cGrepEnumKeys,				//!< [in] 検索対象ファイルパターン
		pArg->cGrepExceptAbsFiles,			//!< [in] 除外ファイル絶対パス
		pArg->cGrepExceptAbsFolders,		//!< [in] 除外フォルダー絶対パス
		pArg->sSearchOption,				//!< [in] 検索オプション
		pArg->sGrepOption,					//!< [in] Grepオプション
		&m_cRegexp[ uId ],					//!< [in] 正規表現コンパイルデータ。既にコンパイルされている必要がある
		
		pArg->pnHitCount,					//!< [i/o] ヒット数の合計
		
		// buffer
		nullptr,							//!< [i/o] Grep結果文字列
		&m_cUnicodeBuffer[ uId ],			//!< [i/o] ファイルオーブンバッファ
		pArg->sGrepOption.bGrepReplace ? &m_cOutBuffer[ uId ] : nullptr		//!< [o] 置換後ファイルバッファ
	};
	
	for(;;){
		// task をキューから pop
		CGrepTask task = [this]{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Condition.wait(lock, [this]{ return !m_Tasks.empty() || m_bStop; });
			
			if(m_bStop) return CGrepTask();
			
			CGrepTask task = std::move(m_Tasks.front());
			m_Tasks.pop();
			
			return task;
		}();
		
		if( m_bStop ) break;
		
		//MYTRACE( L">>task %d started.\n", task.m_uTaskId );
		
		// msg buf 生成
		CGrepResult *pResult = new CGrepResult( task.m_strPathName, task.m_strFileName );
		
		Arg.pcmemMessage = &pResult->m_MsgBuf;
		
		/* ファイル内の検索 */
		pResult->m_iHitCnt = DoGrepReplaceFile(
			&Arg,
			NULL,
			pResult->m_strFileName.c_str(),
			pResult->m_strPathName.c_str()
		);
		
		//MYTRACE( L"<<task %d started.\n", task.m_uTaskId );
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Result[ task.m_uTaskId ] = pResult;
		}
		
		// メインスレッドに終了通知
		m_Condition.notify_all();
	}
}

//****************************************************************************
// Grep 結果表示 update

int CGrepAgent::UpdateResult( tGrepArg *pArg ){
	
	while( 1 ){
		DWORD dwNow = ::GetTickCount();
		
		// 表示更新
		if( (::GetTickCount() - m_dwTickAddTail) > ADDTAIL_INTERVAL_MILLISEC && pArg->pcmemMessage->GetStringLength()){
			AddTail( pArg->pcViewDst, *pArg->pcmemMessage, pArg->sGrepOption.bGrepStdout );
			pArg->pcmemMessage->Clear();
		}
		
		// UI チェック
		if( dwNow - m_dwTickUICheck > UICHECK_INTERVAL_MILLISEC ){
			m_dwTickUICheck = dwNow;
			/* 処理中のユーザー操作を可能にする */
			if( !::BlockingHook( pArg->pcDlgCancel->GetHwnd() ) ){
				return -1;
			}
			/* 中断ボタン押下チェック */
			if( pArg->pcDlgCancel->IsCanceled() ){
				return -1;
			}
			
			/* 表示設定をチェック */
			CEditWnd::getInstance()->SetDrawSwitchOfAllViews(
				0 != ::IsDlgButtonChecked( pArg->pcDlgCancel->GetHwnd(), IDC_CHECK_REALTIMEVIEW )
			);
		}
		
		// Grep 終了した?
		if( m_Result.size() <= m_uTaskIdDisp || !m_Result[ m_uTaskIdDisp ]) break;
		
		CNativeW &Msg = m_Result[ m_uTaskIdDisp ]->m_MsgBuf;
		
		// 結果を buf に追記
		if( 0 < Msg.GetStringLength()){
			*pArg->pcmemMessage += Msg;
		}
		
		*pArg->pnHitCount += m_Result[ m_uTaskIdDisp ]->m_iHitCnt;
		
		// 定期的に grep 中のファイル名表示を更新
		if( dwNow - m_dwTickUIFileName > UIFILENAME_INTERVAL_MILLISEC ){
			m_dwTickUIFileName = dwNow;
			::DlgItem_SetText( pArg->pcDlgCancel->GetHwnd(), IDC_STATIC_CURFILE, m_Result[ m_uTaskIdDisp ]->m_strFileName.c_str());
			::DlgItem_SetText( pArg->pcDlgCancel->GetHwnd(), IDC_STATIC_CURPATH, m_Result[ m_uTaskIdDisp ]->m_strPathName.c_str());
			::SetDlgItemInt(   pArg->pcDlgCancel->GetHwnd(), IDC_STATIC_HITCOUNT, *pArg->pnHitCount, FALSE );
		}
		
		// Result 解放
		delete m_Result[ m_uTaskIdDisp ];
		m_Result[ m_uTaskIdDisp ] = nullptr;
		
		++m_uTaskIdDisp;
	}

	return 0;
}

//****************************************************************************
/*!
	Grep検索・置換実行
*/
int CGrepAgent::DoGrepReplaceFile(
	tGrepArg				*pArg,
	HWND					hWndTarget,			//!< [in] 対象Windows(NULLでファイル)
	const WCHAR*			pszFile,
	const WCHAR*			pszFolder
)
{
	LONGLONG	nLine = 0;
	int			nHitCount = 0;
	ECodeType	nCharCode;
	BOOL		bOutFileName = FALSE;
	CEol		cEol;
	int			nEolCodeLen;
	int			nOldPercent = 0;
	int			nKeyLen = wcslen( pArg->pszKey );
	const WCHAR*	pszCodeName = L"";
	
	// full path 生成
	std::wstring strFullPath = pszFolder;
	if( strFullPath.size()) strFullPath += L'\\';
	strFullPath += pszFile;
	
	const STypeConfigMini* type = NULL;
	if( !CDocTypeManager().GetTypeConfigMini( CDocTypeManager().GetDocumentTypeOfPath( pszFile ), &type ) ){
		return -1;
	}
	CFileLoadOrWnd	cfl( type->m_encoding, hWndTarget );	// 2012/12/18 Uchi 検査するファイルのデフォルトの文字コードを取得する様に
	bool bBom;
	// ファイル名表示
	const WCHAR* pszDispFilePath = strFullPath.c_str();
	
	try{
		// ファイルを開く
		// FileCloseで明示的に閉じるが、閉じていないときはデストラクタで閉じる
		// 2003.06.10 Moca 文字コード判定処理もFileOpenで行う
		nCharCode = cfl.FileOpen( strFullPath.c_str(), true, pArg->sGrepOption.nGrepCharSet, GetDllShareData().m_Common.m_sFile.GetAutoMIMEdecode(), &bBom );
		std::unique_ptr<CWriteData> pOutput;
		
		if( pArg->sGrepOption.bGrepReplace ){
			pOutput.reset( new CWriteData( nHitCount, strFullPath.c_str(), nCharCode, bBom, pArg->sGrepOption.bGrepBackup, *pArg->pcmemMessage ));
		}
		
		WCHAR szCpName[100];
		if( CODE_AUTODETECT == pArg->sGrepOption.nGrepCharSet ){
			if( IsValidCodeType(nCharCode) ){
				wcscpy( szCpName, CCodeTypeName(nCharCode).Bracket() );
				pszCodeName = szCpName;
			}else{
				CCodePage::GetNameBracket(szCpName, nCharCode);
				pszCodeName = szCpName;
			}
		}
		
		int nOutputHitCount = 0;
		
		// 注意 : cfl.ReadLine が throw する可能性がある
		
		// next line callback 設定
		CGrepDocInfo GrepLineInfo( &cfl, &*pArg->pcUnicodeBuffer, &cEol, &nLine );
		pArg->pRegexp->SetNextLineCallback( GetNextLine, &GrepLineInfo );
		
		LONGLONG iMatchLinePrev = -1;
		
		// 非該当行表示時は，partial match を off
		UINT uMatchOpt = pArg->sGrepOption.nGrepOutputLineType == GREP_NOT_MATCH_LINE ? CBregexp::optNoPartialMatch : 0;
		
		while( !m_bStop && RESULT_FAILURE != ReadLine( &GrepLineInfo ))
		{
			const wchar_t*	pLine = pArg->pcUnicodeBuffer->GetStringPtr();
			int		nLineLen = pArg->pcUnicodeBuffer->GetStringLength();
			
			nEolCodeLen = cEol.GetLen();
			
			if( pArg->sGrepOption.bGrepReplace ) pArg->pcOutBuffer->SetString( L"", 0 );
			
			// replace 時の結果 2回目以降の表示
			bool bOutput = true;
			if( pArg->sGrepOption.bGrepOutputFileOnly && 1 <= nHitCount ){
				bOutput = false;
			}
			
			/* 正規表現検索 */
			int nIndex = 0;
			int nMatchNum = 0;
			
			LONGLONG iLineDisp = nLine;
			
			bool bRestartGrep = true; // search_buf に残っているデータが対象の場合 false
			
			// 行単位の処理
			while( !m_bStop && nIndex <= nLineLen ){
				
				// マッチ
				bool bMatch = bRestartGrep ?
					pArg->pRegexp->Match( pLine,   nLineLen, nIndex, uMatchOpt ) :
					pArg->pRegexp->Match( nullptr, 0,        nIndex, uMatchOpt );
				bRestartGrep = false;
				
				// cat した serch buf に切り替わっているかもしれないので，pLine nLineLen をそちらに更新
				pLine		= pArg->pRegexp->GetSubject();
				nLineLen	= pArg->pRegexp->GetSubjectLen();
				
				if( pArg->sGrepOption.nGrepOutputLineType == GREP_NOT_MATCH_LINE ) bMatch = !bMatch;
				
				if( !bMatch ) break;
				
				// 置換
				if( pArg->sGrepOption.bGrepReplace && !pArg->sGrepOption.bGrepPaste ){
					if( pArg->pRegexp->Replace( pArg->cmGrepReplace.GetStringPtr()) < 0 ) throw CError_Regex();
				}
				
				// log 表示用行，match 位置
				int iLogMatchIdx;
				int iLogMatchLen;
				int iLogMatchLineOffs;
				const wchar_t*	pMatchStr = pLine;
				
				pArg->pRegexp->GetMatchLine( &iLogMatchIdx, &iLogMatchLen, &iLogMatchLineOffs );
				
				if( pArg->sGrepOption.nGrepOutputLineType == GREP_MATCH_LINE ){
					iLogMatchIdx	= pArg->pRegexp->GetIndex();
				}else if( pArg->sGrepOption.nGrepOutputLineType == GREP_MATCH_POS ){
					iLogMatchIdx	= pArg->pRegexp->GetIndex();
					iLogMatchLen	= pArg->pRegexp->GetMatchLen();
					pMatchStr		= pLine + iLogMatchIdx;
				}
				
				++nHitCount;
				
				if( bOutput ){
					
					if(
						pArg->sGrepOption.nGrepOutputLineType == GREP_MATCH_POS ||
						iMatchLinePrev != iLineDisp + iLogMatchLineOffs
					){
						
						iMatchLinePrev = iLineDisp + iLogMatchLineOffs;
						
						OutputPathInfo(
							*pArg->pcmemMessage, pArg->sGrepOption,
							strFullPath.c_str(), pszCodeName,
							bOutFileName
						);
						
						/* Grep結果を、*pArg->pcmemMessageに格納する */
						SetGrepResult(
							*pArg->pcmemMessage, pszDispFilePath, pszCodeName,
							iMatchLinePrev,			// line
							iLogMatchIdx + 1,		// column
							pMatchStr,				// match str
							iLogMatchLen,			// match str len
							nEolCodeLen,			// eol len
							pArg->sGrepOption
						);
					}
					
					// 1行 (厳密には SerchBuf 内) の処理終了
					// - replace 時は，終了しない (置換を継続)
					// - 検索時は
					//   - ファイル検索時は終了
					//   - (非)該当モード時は，search buf に行 cat されていなければ終了
					if( !pArg->sGrepOption.bGrepReplace && (
						pArg->sGrepOption.bGrepOutputFileOnly ||
						( pArg->sGrepOption.nGrepOutputLineType != GREP_MATCH_POS && !pArg->pRegexp->IsSearchBufExists())
					)){
						break;
					}
				}
				
				// 置換結果文字列の生成
				if( pArg->sGrepOption.bGrepReplace ){
					pOutput->OutputHead();
					
					// hit 位置直前までをコピー
					pArg->pcOutBuffer->AppendString( pArg->pRegexp->GetSubject() + nIndex, pArg->pRegexp->GetIndex() - nIndex );
					
					// 置換後文字列をコピー
					if( pArg->sGrepOption.bGrepPaste ){
						// クリップボード
						pArg->pcOutBuffer->AppendNativeData( pArg->cmGrepReplace );
					}else{
						// regexp 置換結果
						pArg->pcOutBuffer->AppendString( pArg->pRegexp->GetReplacedString(), pArg->pRegexp->GetReplacedLen());
					}
				}
				
				// 次検索開始位置
				nIndex = pArg->pRegexp->GetIndex() + pArg->pRegexp->GetMatchLen();
				
				// 0 幅マッチの場合 1文字進める
				if( pArg->pRegexp->GetMatchLen() == 0 ) ++nIndex;
			}
			
			if( pArg->sGrepOption.bGrepReplace ){
				// 何も引っかからなかったので，nIndex 以降をコピー
				pArg->pcOutBuffer->AppendString( pLine + nIndex, nLineLen - nIndex );
				pOutput->AppendBuffer(*pArg->pcOutBuffer);
			}
			
			// ファイル検索の場合は、1つ見つかったら終了
			if( !pArg->sGrepOption.bGrepReplace && pArg->sGrepOption.bGrepOutputFileOnly && 1 <= nHitCount ){
				break;
			}
		}
		
		// ファイルを明示的に閉じるが、ここで閉じないときはデストラクタで閉じている
		cfl.FileClose();
		if( pArg->sGrepOption.bGrepReplace) pOutput->Close();
	} // try
	
	catch( const CError_Regex& ){
		pArg->pRegexp->ShowErrorMsg();
		return -1;	// ファイル探索も終了
	}
	catch( const CError_FileOpen& ){
		CNativeW str(LS(STR_GREP_ERR_FILEOPEN));
		str.Replace(L"%s", strFullPath.c_str());
		pArg->pcmemMessage->AppendNativeData( str );
		return 0;
	}
	catch( const CError_FileRead& ){
		CNativeW str(LS(STR_GREP_ERR_FILEREAD));
		str.Replace(L"%s", strFullPath.c_str());
		pArg->pcmemMessage->AppendNativeData( str );
	}
	catch( const CError_WriteFileOpen& ){
		std::wstring file = strFullPath.c_str();
		file += L".skrnew";
		CNativeW str(LS(STR_GREP_ERR_FILEWRITE));
		str.Replace(L"%s", file.c_str());
		pArg->pcmemMessage->AppendNativeData( str );
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
