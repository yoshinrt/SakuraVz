/*!	@file
	@brief BREGEXP Library Handler

	pcre2 を利用するためのインターフェース

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

#ifndef _DLL_BREGEXP_H_
#define _DLL_BREGEXP_H_

#include "CDllHandler.h"

class CBregexp {
public:
	// 0: 次行なし 0 >: 取得成功
	typedef int ( *GetNextLineCallback_t )( wchar_t *&pNextLine, void *pParam );
	
	CBregexp();
	~CBregexp();

	// 2006.01.22 かろと オプション追加・名称変更
	enum Option {
		optNothing			= 0,			//!< オプションなし
		optCaseSensitive	= 1 << 0,		//!< 大文字小文字区別オプション(/iをつけない)
		optGlobal			= 1 << 1,		//!< 全域オプション(/g)
		optExtend			= 1 << 2,		//!< 拡張正規表現(/x)
		optASCII			= 1 << 3,		//!< ASCII(/a)
		optUnicode			= 1 << 4,		//!< Unicode(/u)
		optDefault			= 1 << 5,		//!< Default(/d)
		optLocale			= 1 << 6,		//!< Locale(/l)
		optR				= 1 << 7,		//!< CRLF(/R)
		optPartialMatch		= 1 << 8,		//!< partial match
	};

	//! DLLのバージョン情報を取得
	const TCHAR* GetVersionT(){ return _T(""); }

	//	CJreエミュレーション関数
	//!	検索パターンのコンパイル
	// 2002/01/19 novice 正規表現による文字列置換
	// 2002.01.26 hor    置換後文字列を別引数に
	// 2002.02.01 hor    大文字小文字を無視するオプション追加
	//>> 2002/03/27 Azumaiya 正規表現置換にコンパイル関数を使う形式を追加
	bool Compile(const wchar_t *szPattern, int nOption = 0) {
		return Compile(szPattern, nullptr, nOption);
	}
	bool Compile(const wchar_t *szPattern0, const wchar_t *szPattern1, UINT uOption = 0 );	//!< Replace用
	bool Match(const wchar_t *szTarget, int nLen, int nStart = 0, UINT uOption = 0 );	//!< 検索を実行する
	int Replace(const wchar_t *szTarget, int nLen, int nStart = 0);					//!< 置換を実行する	// 2007.01.16 ryoji 戻り値を置換個数に変更

	/*!
	    検索に一致した文字列の先頭位置を返す(文字列先頭なら0)
		@retval 検索に一致した文字列の先頭位置
	*/
	CLogicInt GetIndex( void ){
		return CLogicInt( pcre2_get_ovector_pointer( m_MatchData )[ 0 ]);
	}
	/*!
	    検索に一致した文字列の次の位置を返す
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
		int *p = ( int *)pcre2_get_ovector_pointer( m_MatchData );
		return CLogicInt( p[ 1 ] - p[ 0 ]);
	}
	/*!
		置換された文字列の長さを返す
		@retval 置換された文字列先頭～文字列末端 までの長さ
	*/
	CLogicInt GetStringLen( void ){
		return CLogicInt(
			m_iReplacedLen - (( m_uOption & optGlobal ) ? m_iStart : GetIndex())
		);
	}
	/*!
		置換された文字列を返す
		@retval 置換された文字列へのポインタ
	*/
	const wchar_t *GetString( void ){
		if( m_iReplacedLen == 0 || m_szReplaceBuf == nullptr ) return L"";
		return m_szReplaceBuf + (( m_uOption & optGlobal ) ? m_iStart : GetIndex());
	}
	
	/*! hit レンジ SearchBuf 内-->行番号付き の変換 */
	void GetMatchRange( CLogicRange *pRange, int iLineOffs = 0 );
	
	//-----------------------------------------

	/*! BREGEXPメッセージを取得する
		@retval メッセージへのポインタ
	*/
	const TCHAR* GetLastMessage() const { return to_tchar( m_szMsg ? m_szMsg : L"" ); }
	
	void GenerateErrorMessage( int iErrorCode );
	
	// 次行取得コールバック登録
	void SetNextLineCallback( GetNextLineCallback_t pFunc, void *pCallbackParam ){
		m_GetNextLineCallback	= pFunc;
		m_pCallbackParam		= pCallbackParam;
	}
	
	/*! SearchBuf の文字列長取得 */
	int GetSearchBufLen( void ){ return m_iSearchBufLen; }
	
protected:

	//!	コンパイルバッファを解放する
	/*!
		m_MatchData, m_Re を解放する．解放後はnullptrにセットする．
		元々nullptrなら何もしない
	*/
	void ReleaseCompileBuffer(void){
		if( m_MatchData ){
			pcre2_match_data_free( m_MatchData );
			m_MatchData = nullptr;
		}
		
		if( m_Re ){
			pcre2_code_free( m_Re );
			m_Re = nullptr;
		}
		
		if( m_szSearchBuf ){
			free( m_szSearchBuf );
			m_szSearchBuf = nullptr;
		}
		
		if( m_szReplaceBuf ){
			free( m_szReplaceBuf );
			m_szReplaceBuf = nullptr;
		}
		
		if( m_szReplacement ){
			delete [] m_szReplacement;
			m_szReplacement = nullptr;
		}
		
		if( m_szMsg ){
			delete [] m_szMsg;
			m_szMsg = nullptr;
		}
	}

private:
	//	内部関数

	//! 検索パターン作成
	void MakePatternAlternate( const wchar_t* const szSearch, std::wstring& strModifiedSearch );
	
	//	メンバ変数
	static const int	MSGBUF_SIZE	= 80;
	wchar_t				*m_szMsg;			//!< BREGEXP_Wからのメッセージを保持する
	
	UINT				m_uOption;			// !< オプション
	
	wchar_t				*m_szSearchBuf;		// !< 検索バッファ
	int					m_iSearchBufSize;	// !< 検索バッファサイズ
	int					m_iSearchBufLen;	// !< 検索バッファ有効文字列長
	std::vector<int>	m_iLineTop;			// !< search buf の各行頭を示す
	
	wchar_t				*m_szReplaceBuf;	// !< 置換結果バッファ
	int					m_iReplaceBufSize;	// !< 置換バッファサイズ
	
	wchar_t				*m_szReplacement;	// !< 置換後文字列
	int					m_iReplacedLen;		// !< 置換結果文字列長
	int					m_iStart;			// !< 検索開始位置
	
	GetNextLineCallback_t	m_GetNextLineCallback;	// !< 次行取得コールバック
	void					*m_pCallbackParam;		// !< コールバックパラメータ
	
	// pcre2
	pcre2_match_data	*m_MatchData;
	pcre2_code			*m_Re;
	
	//! Buf 確保・リサイズ
	static bool ResizeBuf( int iSize, wchar_t *&pBuf, int &iBufSize );
	
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

#endif
