/*! @file */
/*
	Copyright (C) 2008, kobake
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
#ifndef SAKURA_CGREPAGENT_97F2B632_71C8_4E4A_AC42_13A6098B248F_H_
#define SAKURA_CGREPAGENT_97F2B632_71C8_4E4A_AC42_13A6098B248F_H_
#pragma once

#include "doc/CDocListener.h"
#include "io/CFileLoad.h"
#include "config/system_constants.h"
#include "apiwrap/StdApi.h"
#include "_main/CMutex.h"
#include "env/CShareData.h"

class CDlgCancel;
class CEditView;
class CSearchStringPattern;
class CGrepEnumKeys;
class CGrepEnumFiles;
class CGrepEnumFolders;

struct SGrepOption{
	bool		bGrepReplace;			//!< Grep置換
	bool		bGrepSubFolder;			//!< サブフォルダーからも検索する
	bool		bGrepStdout;			//!< 標準出力モード
	bool		bGrepHeader;			//!< ヘッダー・フッター表示
	ECodeType	nGrepCharSet;			//!< 文字コードセット選択
	int			nGrepOutputLineType;	//!< 0:ヒット部分を出力, 1: ヒット行を出力, 2: 否ヒット行を出力
	int			nGrepOutputStyle;		//!< 出力形式 1: Normal, 2: WZ風(ファイル単位) 3: 結果のみ
	bool		bGrepOutputFileOnly;	//!< ファイル毎最初のみ検索
	bool		bGrepPaste;				//!< Grep置換：クリップボードから貼り付ける
	bool		bGrepBackup;			//!< Grep置換：バックアップ

	SGrepOption() :
		 bGrepReplace(false)
		,bGrepSubFolder(true)
		,bGrepStdout(false)
		,bGrepHeader(true)
		,nGrepCharSet(CODE_AUTODETECT)
		,nGrepOutputLineType(1)
		,nGrepOutputStyle(1)
		,bGrepOutputFileOnly(false)
		,bGrepPaste(false)
		,bGrepBackup(false)
	{}
};

typedef struct {
	// window
	CEditView*					pcViewDst;
	CDlgCancel*					pcDlgCancel;			//!< [in] Cancelダイアログへのポインタ
	
	// grep 情報
	const wchar_t*				pszKey;					//!< [in] 検索キー
	const CNativeW&				cmGrepReplace;			//!< [in] 置換後文字列
	CGrepEnumKeys&				cGrepEnumKeys;			//!< [in] 検索対象ファイルパターン
	CGrepEnumFiles&				cGrepExceptAbsFiles;	//!< [in] 除外ファイル絶対パス
	CGrepEnumFolders&			cGrepExceptAbsFolders;	//!< [in] 除外フォルダー絶対パス
	const SSearchOption&		sSearchOption;			//!< [in] 検索オプション
	const SGrepOption&			sGrepOption;			//!< [in] Grepオプション
	const CSearchStringPattern&	pattern;				//!< [in] 検索パターン
	CBregexp*					pRegexp;				//!< [in] 正規表現コンパイルデータ。既にコンパイルされている必要がある
	
	int*						pnHitCount;				//!< [i/o] ヒット数の合計
	
	// path
	const WCHAR*				pszBasePath;			//!< [in] 検索対象パス(ベースフォルダー)
	
	// buffer
	CNativeW&					cmemMessage;			//!< [i/o] Grep結果文字列
	CNativeW&					cUnicodeBuffer;			//!< [i/o] ファイルオーブンバッファ
	CNativeW&					cOutBuffer;				//!< [o] 置換後ファイルバッファ
} tGrepArg;

class CFileLoadOrWnd{
	CFileLoad m_cfl;
	HWND m_hWnd;
	int m_nLineCurrent;
	int m_nLineNum;
public:
	CFileLoadOrWnd(const SEncodingConfig& encode, HWND hWnd)
		: m_cfl(encode)
		, m_hWnd(hWnd)
		, m_nLineCurrent(0)
		, m_nLineNum(0)
	{
	}
	~CFileLoadOrWnd(){
	}
	ECodeType FileOpen(const WCHAR* pszFile, bool bBigFile, ECodeType charCode, int nFlag, bool* pbBomExist = NULL)
	{
		if( m_hWnd ){
			DWORD_PTR dwMsgResult = 0;
			if( 0 == ::SendMessageTimeout(m_hWnd, MYWM_GETLINECOUNT, 0, 0, SMTO_NORMAL, 10000, &dwMsgResult) ){
				// エラーかタイムアウト
				throw CError_FileOpen();
			}
			m_nLineCurrent = 0;
			m_nLineNum = (int)dwMsgResult;
			::SendMessageAny(m_hWnd, MYWM_GETFILEINFO, 0, 0);
			const EditInfo* editInfo = &GetDllShareData().m_sWorkBuffer.m_EditInfo_MYWM_GETFILEINFO;
			return editInfo->m_nCharCode;
		}
		return m_cfl.FileOpen(pszFile, bBigFile, charCode, nFlag, pbBomExist);
	}
	EConvertResult ReadLine(CNativeW* buffer, CEol* pcEol){
		if( m_hWnd ){
			const int max_size = (int)GetDllShareData().m_sWorkBuffer.GetWorkBufferCount<const WCHAR>();
			const WCHAR* pLineData = GetDllShareData().m_sWorkBuffer.GetWorkBuffer<const WCHAR>();
			buffer->SetStringHoldBuffer(L"", 0);
			if( m_nLineNum <= m_nLineCurrent ){
				return RESULT_FAILURE;
			}
			int nLineOffset = 0;
			int nLineLen = 0; //初回用仮値
			do{
				// m_sWorkBuffer#m_Workの排他制御。外部コマンド出力/TraceOut/Diffが対象
				LockGuard<CMutex> guard( CShareData::GetMutexShareWork() );
				{
					nLineLen = ::SendMessageAny(m_hWnd, MYWM_GETLINEDATA, m_nLineCurrent, nLineOffset);
					if( nLineLen == 0 ){ return RESULT_FAILURE; } // EOF => 正常終了
					if( nLineLen < 0 ){ return RESULT_FAILURE; } // 何かエラー
					buffer->AllocStringBuffer(max_size);
					buffer->AppendString(pLineData, t_min(nLineLen, max_size));
				}
				nLineOffset += max_size;
			}while(max_size < nLineLen);
			if( 0 < nLineLen ){
				if( 1 < nLineLen && (*buffer)[nLineLen - 2] == WCODE::CR &&
						(*buffer)[nLineLen - 1] == WCODE::LF){
					pcEol->SetType(EEolType::cr_and_lf);
				}else{
					pcEol->SetTypeByString(buffer->GetStringPtr() + nLineLen - 1, 1);
				}
			}
			m_nLineCurrent++;
			return RESULT_COMPLETE;
		}
		return m_cfl.ReadLine(buffer, pcEol);
	}
	LONGLONG GetFileSize(){
		if( m_hWnd ){
			return 0;
		}
		return m_cfl.GetFileSize();
	}
	int GetPercent(){
		if( m_hWnd ){
			return (int)(m_nLineCurrent * 100.0 / m_nLineNum);
		}
		return m_cfl.GetPercent();
	}
	
	void FileClose(){
		if( m_hWnd ){
			return;
		}
		m_cfl.FileClose();
	}
};

class CGrepDocInfo {
public:
	CGrepDocInfo(
		CFileLoadOrWnd	*pFileLoad,
		CNativeW		*pLineBuf,
		CEol			*pEol,
		LONGLONG		*piLine
	) : m_pFileLoad( pFileLoad ),
		m_pLineBuf( pLineBuf ),
		m_pEol( pEol ),
		m_piLine( piLine ),
		m_bUnget( false ){}
	
	CFileLoadOrWnd	*m_pFileLoad;
	CNativeW		*m_pLineBuf;
	CEol			*m_pEol;
	LONGLONG		*m_piLine;
	EConvertResult	m_cPrevResult;
	bool			m_bUnget;
};

//	Jun. 26, 2001 genta	正規表現ライブラリの差し替え
//	Mar. 28, 2004 genta DoGrepFileから不要な引数を削除
class CGrepAgent : public CDocListenerEx{
public:
	CGrepAgent();

	// イベント
	ECallbackResult OnBeforeClose() override;
	void OnAfterSave(const SSaveInfo& sSaveInfo) override;

	static void CreateFolders( const WCHAR* pszPath, std::vector<std::wstring>& vPaths );
	static std::wstring ChopYen( const std::wstring& str );
	void AddTail( CEditView* pcEditView, const CNativeW& cmem, bool bAddStdout );

	// Grep実行
	DWORD DoGrep(
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
		bool					bGrepOutputFileOnly,	//!< [in] ファイル毎最初のみ出力
		bool					bGrepOutputBaseFolder,	//!< [in] ベースフォルダー表示
		bool					bGrepSeparateFolder,	//!< [in] フォルダー毎に表示
		bool					bGrepPaste,
		bool					bGrepBackup
	);

private:
	class CError_Regex {};
	
	// Grep実行
	int DoGrepTree(
		tGrepArg				*pArg,
		int						nNest,				//!< [in] ネストレベル
		const WCHAR*			pszPath				//!< [in] 検索対象パス
	);

	int DoGrepReplaceFile(
		tGrepArg				*pArg,
		HWND					hWndTarget,			//!< [in] 対象Windows(NULLでファイル)
		const WCHAR*			pszFile,
		const WCHAR*			pszFullPath,
		const WCHAR*			pszFolder
	);

	// Grep結果をpszWorkに格納
	void SetGrepResult(
		// データ格納先
		CNativeW&		cmemMessage,
		// マッチしたファイルの情報
		const WCHAR*	pszFilePath,	//	フルパス or 相対パス
		const WCHAR*	pszCodeName,	//	文字コード情報"[SJIS]"とか
		// マッチした行の情報
		LONGLONG		nLine,			//	マッチした行番号
		int				nColumn,		//	マッチした桁番号
		const wchar_t*	pMatchData,		//	マッチした文字列
		int				nMatchLen,		//	マッチした文字列の長さ
		int				nEolCodeLen,	//	EOLの長さ
		// オプション
		const SGrepOption&	sGrepOption
	);
	
	// 次行取得
	static EConvertResult ReadLine( CGrepDocInfo *pInfo );
	static int GetNextLine( const wchar_t **ppNextLine, void *pParam );	// callback 用
	
	DWORD m_dwTickAddTail;	// AddTail() を呼び出した時間
	DWORD m_dwTickUICheck;	// 処理中にユーザーによるUI操作が行われていないか確認した時間
	DWORD m_dwTickUIFileName;	// Cancelダイアログのファイル名表示更新を行った時間

public: //$$ 仮
	bool	m_bGrepMode;		//!< Grepモードか
	bool	m_bGrepRunning;		//!< Grep処理中
};
#endif /* SAKURA_CGREPAGENT_97F2B632_71C8_4E4A_AC42_13A6098B248F_H_ */
