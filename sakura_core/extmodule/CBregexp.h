﻿/*!	@file
	@brief PCRE2 Library Handler

	PCRE2 を利用するためのインターフェース

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

#pragma once

#include "CDllHandler.h"

class CBregexp {
public:
	/*!
		@brief partial match 時の次行取得コールバック
		@param[in] pNextLine	次行の文字列ポインタを受け取るポインタ，nullptr 時は unget 動作
		@param[in] pParam		コールバックに渡す任意のポインタ
		@retval 0: 次行なし 0 >: 取得成功 \
					SIZE_LAST が立っていたら，その行が最終行
	*/
	typedef int ( *GetNextLineCallback_t )( const wchar_t **ppNextLine, void *pParam );
	static const int SIZE_LAST = ( 1 << 31 );
	
	CBregexp();
	~CBregexp();
	
	// 2006.01.22 かろと オプション追加・名称変更
	enum Option {						//	いつ指定?
		optNothing			= 0,		//!<			オプションなし
		optIgnoreCase		= 1 <<  0,	//!< compile	ignore case
		optGlobal			= 1 <<  1,	//!< replace	全域オプション(/g)
		optNoPartialMatch	= 1 <<  2,	//!< match		re 時 partial matchなし
		optLiteral			= 1 <<  3,	//!< compile	基本検索
		optWordSearch		= 1 <<  4,	//!< compile	単語検索
		optNotBol			= 1 <<  5,	//!< match		sbj 先頭は非行頭
	};

	//! DLLのバージョン情報を取得
	const WCHAR* GetVersionW(){ return L""; }

	//	CJreエミュレーション関数
	//!	検索パターンのコンパイル
	// 2002/01/19 novice 正規表現による文字列置換
	// 2002.01.26 hor    置換後文字列を別引数に
	// 2002.02.01 hor    大文字小文字を無視するオプション追加
	//>> 2002/03/27 Azumaiya 正規表現置換にコンパイル関数を使う形式を追加
	bool Compile(const wchar_t *szPattern, UINT uOption = 0 );
	bool Match(const wchar_t *szTarget, int nLen, int iStart = 0, UINT uOption = 0 );	//!< 検索を実行する
	int Replace( const wchar_t *szReplacement, const wchar_t *szSubject = nullptr, int iSubjectLen = 0, int iStart = -1, UINT uOption = 0 );	//!< 置換を実行する	// 2007.01.16 ryoji 戻り値を置換個数に変更

	/*!
	    検索に一致した文字列の先頭位置を返す(文字列先頭なら0)
	    global 時は常に検索開始位置
		@retval 検索に一致した文字列の先頭位置
	*/
	CLogicInt GetIndex( void ){
		return CLogicInt(
			m_uOption & optGlobal ? m_iStart :
									pcre2_get_ovector_pointer( m_MatchData )[ 0 ]
		);
	}
	/*!
	    検索に一致した文字列終端の次の位置を返す
		@retval 検索に一致した文字列の次の位置
	*/
	CLogicInt GetLastIndex( void ){
		return CLogicInt( pcre2_get_ovector_pointer( m_MatchData )[ 1 ]);
	}
	/*!
		検索に一致した文字列の長さを返す
		@retval 検索に一致した文字列の長さ
	*/
	CLogicInt GetMatchLen( void ){
		return GetLastIndex() - GetIndex();
	}
	/*!
		置換された文字列の長さを返す
		@retval 置換された文字列長
	*/
	CLogicInt GetReplacedLen( void ){
		return CLogicInt(
			m_iReplacedLen - m_iSubjectLen + GetMatchLen()
		);
	}
	/*!
		置換された文字列を返す
		@retval 置換された文字列へのポインタ
	*/
	const wchar_t *GetReplacedString( void ){
		if( m_iReplacedLen == 0 || m_szReplaceBuf == nullptr ) return L"";
		return m_szReplaceBuf + GetIndex();
	}
	
	/*! hit レンジ SearchBuf 内-->行番号付き の変換 */
	void GetMatchRange( CLogicRange *pRangeOut, const CLogicRange *pRangeIn, int iLineOffs = 0 ){
		GetMatchRange( pRangeOut, pRangeIn->GetFrom().x, pRangeIn->GetTo().x, iLineOffs );
	}
	void GetMatchRange( CLogicRange *pRangeOut, int iFrom, int iTo, int iLineOffs = 0 );

	/*! hit レンジ 取得 */
	void GetMatchRange( CLogicRange *pRangeOut, int iLineOffs = 0 ){
		GetMatchRange( pRangeOut, GetIndex(), GetLastIndex(), iLineOffs );
	}
	
	//! match した行全体を得る，grep 結果表示用
	void GetMatchLine( const wchar_t **ppLine, int *piLen );
	
	//-----------------------------------------

	/*! BREGEXPメッセージを取得する
		@retval メッセージへのポインタ
	*/
	const wchar_t* GetLastMessage( void );
	void ShowErrorMsg( HWND hWnd = nullptr );
	
	// 次行取得コールバック登録
	void SetNextLineCallback( GetNextLineCallback_t pFunc, void *pCallbackParam ){
		m_GetNextLineCallback	= pFunc;
		m_pCallbackParam		= pCallbackParam;
	}
	
	//! Subject を得る
	wchar_t *GetSubject( void ){ return m_szSubject; }
	
	/*! SearchBuf の文字列長取得 */
	int GetSubjectLen( void ){ return m_iSubjectLen; }
	
	// 簡易的な subst
	int Substitute(
		const wchar_t *szSubject, const wchar_t *szPattern,
		const wchar_t *szReplacement, std::wstring *pResult, UINT uOption = 0
	);
	
protected:

	//!	コンパイルバッファを解放する
	/*!
		m_MatchData, m_Re を解放する．解放後はnullptrにセットする．
		元々nullptrなら何もしない
	*/
	void ReleaseCompileBuffer( void );

private:
	//	内部関数

	//	メンバ変数
	static const int	MSGBUF_SIZE	= 80;
	int					m_iLastCode;		//!< 最後のエラーコード
	wchar_t				*m_szMsg;			//!< BREGEXP_Wからのメッセージを保持する
	
	UINT				m_uOption;			// !< オプション
	wchar_t				*m_szSubject;		// !< 最後に検索した文字列
	int					m_iSubjectLen;		// !< *m_szSubject 文字列長
	
	wchar_t				*m_szSearchBuf;		// !< 検索バッファ
	int					m_iSearchBufSize;	// !< 検索バッファサイズ
	
	std::vector<int>	m_iLineTop;			// !< search buf の各行頭を示す
	
	wchar_t				*m_szReplaceBuf;	// !< 置換結果バッファ
	int					m_iReplaceBufSize;	// !< 置換バッファサイズ
	int					m_iReplacedLen;		// !< 置換結果文字列長
	int					m_iStart;			// !< 検索開始位置
	
	GetNextLineCallback_t	m_GetNextLineCallback;	// !< 次行取得コールバック
	void					*m_pCallbackParam;		// !< コールバックパラメータ
	
	// pcre2
	pcre2_compile_context	*m_Context;
	pcre2_match_data		*m_MatchData;
	pcre2_code				*m_Re;
	
	//! Buf 確保・リサイズ
	static bool ResizeBuf( int iSize, wchar_t *&pBuf, int &iBufSize, bool bDisposable = false );
	
	// 行末が EOL かどうか
	bool IsEolTail( const wchar_t* szStr, int iLen ){
		return iLen > 0 && szStr[ iLen - 1 ] == L'\n';
	}
	
	// bregonig.dll I/F
public:
	// DLL の名残
	bool IsAvailable( void ){ return true; }
	
	//! DLLロードと初期処理
	EDllResult InitDll(
		LPCTSTR pszSpecifiedDllName = nullptr	//!< [in] クラスが定義しているDLL名以外のDLLを読み込みたいときに、そのDLL名を指定。
	){
		return DLL_SUCCESS;
	}
};

//	Jun. 26, 2001 genta
//!	正規表現ライブラリのバージョン取得
static inline bool CheckRegexpVersion( HWND hWnd, int nCmpId, bool bShowMsg = false ){ return true; };
bool CheckRegexpSyntax( const wchar_t* szPattern, HWND hWnd, bool bShowMessage, UINT uOption = 0 );
static inline bool InitRegexp( HWND hWnd, CBregexp& rRegexp, bool bShowMessage ){
	return true;
}
