/*!	@file
	@brief BREGEXP Library Handler

	Perl5互換正規表現を扱うDLLであるBREGEXP.DLLを利用するためのインターフェース

	@author genta
	@date Jun. 10, 2001
	@date 2002/2/1 hor		ReleaseCompileBufferを適宜追加
	@date Jul. 25, 2002 genta 行頭条件を考慮した検索を行うように．(置換はまだ)
	@date 2003.05.22 かろと 正規な正規表現に近づける
	@date 2005.03.19 かろと リファクタリング。クラス内部を隠蔽。
*/
/*
	Copyright (C) 2001-2002, genta
	Copyright (C) 2002, novice, hor, Azumaiya
	Copyright (C) 2003, かろと
	Copyright (C) 2005, かろと
	Copyright (C) 2006, かろと
	Copyright (C) 2007, ryoji

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
#include "CBregexp.h"
#include "charset/charcode.h"

CBregexp::CBregexp(){
	m_Re			= nullptr;
	m_MatchData		= nullptr;
	m_szMsg			= nullptr;
	m_szReplaceBuf	= nullptr;
	m_szReplacement	= nullptr;
	m_iReplaceBufSize	= 0;
}

CBregexp::~CBregexp(){
	//コンパイルバッファを解放
	ReleaseCompileBuffer();
}

/*!
	CBregexp::MakePattern()の代替。
	* エスケープされておらず、文字集合と \Q...\Eの中にない . を [^\r\n] に置換する。
	* エスケープされておらず、文字集合と \Q...\Eの中にない $ を (?<![\r\n])(?=\r|$) に置換する。
	これは「改行」の意味を LF のみ(BREGEXP.DLLの仕様)から、CR, LF, CRLF に拡張するための変更である。
	また、$ は改行の後ろ、行文字列末尾にマッチしなくなる。最後の一行の場合をのぞいて、
	正規表現DLLに与えられる文字列の末尾は文書末とはいえず、$ がマッチする必要はないだろう。
	$ が行文字列末尾にマッチしないことは、一括置換での期待しない置換を防ぐために必要である。
*/
void CBregexp::MakePatternAlternate( const wchar_t* const szSearch, std::wstring& strModifiedSearch ){
	static const wchar_t szDotAlternative[] = L"[^\\r\\n]";
	static const wchar_t szDollarAlternative[] = L"(?<![\\r\\n])(?=\\r|$)";

	// すべての . を [^\r\n] へ、すべての $ を (?<![\r\n])(?=\r|$) へ置換すると仮定して、strModifiedSearchの最大長を決定する。
	std::wstring::size_type modifiedSearchSize = 0;
	for( const wchar_t* p = szSearch; *p; ++p ) {
		if( *p == L'.') {
			modifiedSearchSize += (sizeof szDotAlternative) / (sizeof szDotAlternative[0]) - 1;
		} else if( *p == L'$' ) {
			modifiedSearchSize += (sizeof szDollarAlternative) / (sizeof szDollarAlternative[0]) - 1;
		} else {
			modifiedSearchSize += 1;
		}
	}
	++modifiedSearchSize; // '\0'

	strModifiedSearch.reserve( modifiedSearchSize );

	// szSearchを strModifiedSearchへ、ところどころ置換しながら順次コピーしていく。
	enum State {
		DEF = 0, /* DEFULT 一番外側 */
		D_E,     /* DEFAULT_ESCAPED 一番外側で \の次 */
		D_C,     /* DEFAULT_SMALL_C 一番外側で \cの次 */
		CHA,     /* CHARSET 文字クラスの中 */
		C_E,     /* CHARSET_ESCAPED 文字クラスの中で \の次 */
		C_C,     /* CHARSET_SMALL_C 文字クラスの中で \cの次 */
		QEE,     /* QEESCAPE \Q...\Eの中 */
		Q_E,     /* QEESCAPE_ESCAPED \Q...\Eの中で \の次 */
		NUMBER_OF_STATE,
		_EC = -1, /* ENTER CHARCLASS charsetLevelをインクリメントして CHAへ */
		_XC = -2, /* EXIT CHARCLASS charsetLevelをデクリメントして CHAか DEFへ */
		_DT = -3, /* DOT (特殊文字としての)ドットを置き換える */
		_DL = -4, /* DOLLAR (特殊文字としての)ドルを置き換える */
	};
	enum CharClass {
		OTHER = 0,
		DOT,    /* . */
		DOLLAR, /* $ */
		SMALLC, /* c */
		LARGEQ, /* Q */
		LARGEE, /* E */
		LBRCKT, /* [ */
		RBRCKT, /* ] */
		ESCAPE, /* \ */
		NUMBER_OF_CHARCLASS
	};
	static const State state_transition_table[NUMBER_OF_STATE][NUMBER_OF_CHARCLASS] = {
	/*        OTHER   DOT  DOLLAR  SMALLC LARGEQ LARGEE LBRCKT RBRCKT ESCAPE*/
	/* DEF */ {DEF,  _DT,   _DL,    DEF,   DEF,   DEF,   _EC,   DEF,   D_E},
	/* D_E */ {DEF,  DEF,   DEF,    D_C,   QEE,   DEF,   DEF,   DEF,   DEF},
	/* D_C */ {DEF,  DEF,   DEF,    DEF,   DEF,   DEF,   DEF,   DEF,   D_E},
	/* CHA */ {CHA,  CHA,   CHA,    CHA,   CHA,   CHA,   _EC,   _XC,   C_E},
	/* C_E */ {CHA,  CHA,   CHA,    C_C,   CHA,   CHA,   CHA,   CHA,   CHA},
	/* C_C */ {CHA,  CHA,   CHA,    CHA,   CHA,   CHA,   CHA,   CHA,   C_E},
	/* QEE */ {QEE,  QEE,   QEE,    QEE,   QEE,   QEE,   QEE,   QEE,   Q_E},
	/* Q_E */ {QEE,  QEE,   QEE,    QEE,   QEE,   DEF,   QEE,   QEE,   Q_E}
	};
	State state = DEF;
	int charsetLevel = 0; // ブラケットの深さ。POSIXブラケット表現など、エスケープされていない [] が入れ子になることがある。
	const wchar_t *left = szSearch, *right = szSearch;
	for( ; *right; ++right ) { // CNativeW::GetSizeOfChar()は使わなくてもいいかな？
		const wchar_t ch = *right;
		const CharClass charClass =
			ch == L'.'  ? DOT:
			ch == L'$'  ? DOLLAR:
			ch == L'c'  ? SMALLC:
			ch == L'Q'  ? LARGEQ:
			ch == L'E'  ? LARGEE:
			ch == L'['  ? LBRCKT:
			ch == L']'  ? RBRCKT:
			ch == L'\\' ? ESCAPE:
			OTHER;
		const State nextState = state_transition_table[state][charClass];
		if(0 <= nextState) {
			state = nextState;
		} else switch(nextState) {
			case _EC: // ENTER CHARSET
				charsetLevel += 1;
				state = CHA;
			break;
			case _XC: // EXIT CHARSET
				charsetLevel -= 1;
				state = 0 < charsetLevel ? CHA : DEF;
			break;
			case _DT: // DOT(match anything)
				strModifiedSearch.append( left, right );
				left = right + 1;
				strModifiedSearch.append( szDotAlternative );
			break;
			case _DL: // DOLLAR(match end of line)
				strModifiedSearch.append( left, right );
				left = right + 1;
				strModifiedSearch.append( szDollarAlternative );
			break;
			default: // バグ。enum Stateに見逃しがある。
			break;
		}
	}
	strModifiedSearch.append( left, right + 1 ); // right + 1 は '\0' の次を指す(明示的に '\0' をコピー)。
}

/*!
	JRE32のエミュレーション関数．空の文字列に対して検索・置換を行うことにより
	BREGEXP_W構造体の生成のみを行う．

	@param[in] szPattern0	検索or置換パターン
	@param[in] szPattern1	置換後文字列パターン(検索時はnullptr)
	@param[in] nOption		検索・置換オプション

	@retval true 成功
	@retval false 失敗
*/
bool CBregexp::Compile( const wchar_t *szPattern0, const wchar_t *szPattern1, int nOption ){
	//	BREGEXP_W構造体の解放
	ReleaseCompileBuffer();
	
	// ライブラリに渡す検索パターンを作成
	// 別関数で共通処理に変更 2003.05.03 by かろと
	
	std::wstring strModifiedSearch;
	
	MakePatternAlternate( szPattern0, strModifiedSearch );
	szPattern0 = strModifiedSearch.c_str();
	
	// pcre2 opt 生成
	m_iOption = nOption;
	int iPcreOpt	= PCRE2_MULTILINE;
	if( ~nOption & optCaseSensitive	) iPcreOpt |= PCRE2_CASELESS;		// 大文字小文字区別オプション
	
	int	iErrCode;
	PCRE2_SIZE	sizeErrOffset;
	
	m_Re = pcre2_compile(
		( PCRE2_SPTR )szPattern0,	// PCRE2_SPTR pattern
		PCRE2_ZERO_TERMINATED,		// PCRE2_SIZE length
		iPcreOpt,					// uint32_t options
		&iErrCode,					// int *errorcode
		&sizeErrOffset,				// PCRE2_SIZE *erroroffset
		nullptr						// pcre2_compile_context *ccontext
	);
	
	//	何らかのエラー発生。
	if( m_Re == nullptr ){
		ReleaseCompileBuffer();
		GenerateErrorMessage( iErrCode );
		return false;
	}
	
	// match_data 確保
	m_MatchData = pcre2_match_data_create_from_pattern(
		m_Re, 	// uint32_t ovecsize
		nullptr	// pcre2_general_context *gcontext
	); 
	
	if( szPattern1 ){
		// 置換実行
		//★置換側のチェック未実装
		//BSubst( pszNPattern, m_tmpBuf, m_tmpBuf+1, &m_pRegExp, m_szMsg );
		
		int len = wcslen( szPattern1 );
		m_szReplacement = new WCHAR[ len + 1 ];
		wcscpy( m_szReplacement, szPattern1 );
	}
	
	return true;
}

/*!
	JRE32のエミュレーション関数．既にあるコンパイル構造体を利用して検索（1行）を
	行う．

	@param[in] szTarget 検索対象領域先頭アドレス
	@param[in] nLen 検索対象領域サイズ
	@param[in] nStart 検索開始位置．(先頭は0)

	@retval true Match
	@retval false No Match または エラー。エラーは GetLastMessage()により判定可能。

*/
bool CBregexp::Match( const wchar_t* szTarget, int nLen, int nStart ){
	//	DLLが利用可能でないとき、または構造体が未設定の時はエラー終了
	if( m_Re == nullptr ) return false;
	
	// match
	int iResult = pcre2_match(
		m_Re,						// const pcre2_code *code
		( PCRE2_SPTR )szTarget,		// PCRE2_SPTR subject
		nLen,						// PCRE2_SIZE length
		nStart,						// PCRE2_SIZE startoffset
		0/*PCRE2_PARTIAL_HARD*/,	// uint32_t options
		m_MatchData,				// pcre2_match_data *match_data
		nullptr						// pcre2_match_context *mcontext
	);
	
	return iResult > 0;
}

//<< 2002/03/27 Azumaiya
/*!
	正規表現による文字列置換
	既にあるコンパイル構造体を利用して置換（1行）を
	行う．

	@param[in] szTarget 置換対象データ
	@param[in] nLen 置換対象データ長
	@param[in] nStart 置換開始位置(0からnLen未満)

	@retval 置換個数

	@date	2007.01.16 ryoji 戻り値を置換個数に変更
*/
int CBregexp::Replace( const wchar_t *szTarget, int nLen, int nStart ){
	
	// 必要な output buffer サイズを計算する．
	// 初期値は，2^n >= nlen * 2 となるサイズで最低 1KB．
	
	int iNeededSize = nLen * 2;
	int iResult;
	PCRE2_SIZE	OutputLen;
	m_iStart = nStart;
	
	while( 1 ){
		// 必要サイズ大きすぎ
		if( iNeededSize >= ( 1 << 30 )) return 0;
		
		// buf サイズ更新必要
		if( m_iReplaceBufSize < iNeededSize ){
			if( m_szReplaceBuf ) delete [] m_szReplaceBuf;
			m_szReplaceBuf = nullptr;
			
			if( m_iReplaceBufSize == 0 ) m_iReplaceBufSize = 1024; // 最小初期サイズ
			
			while( m_iReplaceBufSize < iNeededSize ) m_iReplaceBufSize <<= 1;
			
			m_szReplaceBuf = new WCHAR[ m_iReplaceBufSize ];
		}
		
		OutputLen	= m_iReplaceBufSize;
		
		// オプション
		int iOption = PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
		if( m_iOption & optGlobal ) iOption |= PCRE2_SUBSTITUTE_GLOBAL;
		
		iResult = pcre2_substitute(
			m_Re,							// const pcre2_code *code
			( PCRE2_SPTR )szTarget,			// PCRE2_SPTR subject
			nLen,							// PCRE2_SIZE length
			nStart,							// PCRE2_SIZE startoffset
			iOption,						// uint32_t options
			m_MatchData,					// pcre2_match_data *match_data
			nullptr,						// pcre2_match_context *mcontext
			( PCRE2_SPTR )m_szReplacement,	// PCRE2_SPTR replacement,
			PCRE2_ZERO_TERMINATED,			// PCRE2_SIZE rlength
			( PCRE2_UCHAR *)m_szReplaceBuf,	// PCRE2_UCHAR *outputbuffer
			&OutputLen						// PCRE2_SIZE *outlengthptr
		);
		
		if( iResult != PCRE2_ERROR_NOMEMORY ) break;
		
		// バッファが足りないので再試行
		iNeededSize = OutputLen;
	}
	
	// エラー
	if( iResult < 0 ){
		m_iReplacedLen	= 0;
		return 0;
	}
	
	// 正常終了
	m_iReplacedLen	= OutputLen;
	return iResult;
}

// pcre2 エラーメッセージ取得
void CBregexp::GenerateErrorMessage( int iErrorCode ){
	if( m_szMsg == nullptr ) m_szMsg = new WCHAR[ MSGBUF_SIZE ];
	pcre2_get_error_message( iErrorCode, ( PCRE2_UCHAR *)m_szMsg, MSGBUF_SIZE - 1 );
}

/*!
	正規表現が規則に従っているかをチェックする。

	@param szPattern [in] チェックする正規表現
	@param hWnd [in] メッセージボックスの親ウィンドウ
	@param bShowMessage [in] 初期化失敗時にエラーメッセージを出すフラグ
	@param nOption [in] 大文字と小文字を無視して比較するフラグ // 2002/2/1 hor追加

	@retval true 正規表現は規則通り
	@retval false 文法に誤りがある。または、ライブラリが使用できない。
*/
bool CheckRegexpSyntax(
	const wchar_t*	szPattern,
	HWND			hWnd,
	bool			bShowMessage,
	int				nOption
){
	CBregexp cRegexp;

	if( nOption == -1 ){
		nOption = CBregexp::optCaseSensitive;
	}
	if( !cRegexp.Compile( szPattern, nullptr, nOption )){
		if( bShowMessage ){
			::MessageBox( hWnd, cRegexp.GetLastMessage(),
				LS(STR_BREGONIG_TITLE), MB_OK | MB_ICONEXCLAMATION );
		}
		return false;
	}
	return true;
}
//	To Here Jun. 26, 2001 genta
