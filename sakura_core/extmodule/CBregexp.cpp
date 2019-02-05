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
	m_Re					= nullptr;
	m_MatchData				= nullptr;
	m_szMsg					= nullptr;
	m_szSearchBuf			= nullptr;
	m_iSearchBufSize		= 0;
	m_iSubjectLen			= 0;
	m_szReplaceBuf			= nullptr;
	m_iReplaceBufSize		= 0;
	m_iReplacedLen			= 0;
	m_szReplacement			= nullptr;
	
	m_GetNextLineCallback	= nullptr;
}

CBregexp::~CBregexp(){
	//コンパイルバッファを解放
	ReleaseCompileBuffer();
}

void CBregexp::ReleaseCompileBuffer(void){
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
	m_iSearchBufSize		= 0;
	m_iSubjectLen			= 0;
	
	if( m_szReplaceBuf ){
		free( m_szReplaceBuf );
		m_szReplaceBuf = nullptr;
	}
	m_iReplaceBufSize		= 0;
	m_iReplacedLen			= 0;
	
	if( m_szReplacement ){
		delete [] m_szReplacement;
		m_szReplacement = nullptr;
	}
	
	if( m_szMsg ){
		delete [] m_szMsg;
		m_szMsg = nullptr;
	}
}

// バッファを除くコピー
void CBregexp::Copy( CBregexp &re ){
	if( re.m_Re ){
		m_Re		= pcre2_code_copy( re.m_Re );
		m_MatchData = pcre2_match_data_create_from_pattern(
			m_Re, 	// uint32_t ovecsize
			nullptr	// pcre2_general_context *gcontext
		);
	}
	
	m_szMsg					= nullptr;
	m_szSearchBuf			= nullptr;
	m_iSearchBufSize		= 0;
	m_iSubjectLen			= 0;
	m_szReplaceBuf			= nullptr;
	m_iReplaceBufSize		= 0;
	m_iReplacedLen			= 0;
	
	if( re.m_szReplacement ) SetReplacement( re.m_szReplacement );
	
	m_GetNextLineCallback	= re.m_GetNextLineCallback;
	m_pCallbackParam		= re.m_pCallbackParam;
	m_uOption				= re.m_uOption;
	m_iStart				= re.m_iStart;
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
	@param[in] uOption		検索・置換オプション

	@retval true 成功
	@retval false 失敗
*/
bool CBregexp::Compile( const wchar_t *szPattern0, const wchar_t *szPattern1, UINT uOption ){
	//	BREGEXP_W構造体の解放
	ReleaseCompileBuffer();
	
	// ライブラリに渡す検索パターンを作成
	// 別関数で共通処理に変更 2003.05.03 by かろと
	
	std::wstring strModifiedSearch;
	
	MakePatternAlternate( szPattern0, strModifiedSearch );
	szPattern0 = strModifiedSearch.c_str();
	
	// pcre2 opt 生成
	m_uOption = uOption;
	int iPcreOpt	= PCRE2_MULTILINE;
	if( ~uOption & optCaseSensitive	) iPcreOpt |= PCRE2_CASELESS;		// 大文字小文字区別オプション
	
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
		
		SetReplacement( szPattern1 );
	}
	
	return true;
}

/*!
	JRE32のエミュレーション関数．既にあるコンパイル構造体を利用して検索（1行）を
	行う．

	@param[in] szSubject 置換対象データ, null の場合 m_szSubject を使用する
	@param[in] iSubjectLen 置換対象データ長，szSubject = nul の場合 m_iSubjectLen を使用する
	@param[in] iStart 検索開始位置．(先頭は0)  -1: m_iStart を使用

	@retval true Match
	@retval false No Match または エラー

*/
bool CBregexp::Match( const wchar_t* szSubject, int iSubjectLen, int iStart, UINT uOption ){
	//	構造体が未設定の時はエラー終了
	if( m_Re == nullptr ) return false;
	
	if( szSubject ){
		m_szSubject		= const_cast<wchar_t *>( szSubject );
		m_iSubjectLen	= iSubjectLen;
		m_iLineTop.clear();
	}
	
	if( iStart >= 0 ) m_iStart = iStart;
	
	UINT uPcre2Opt = ( uOption & optPartialMatch ) ? PCRE2_PARTIAL_HARD : 0;
	
	while( 1 ){
		// match
		int iResult = pcre2_match(
			m_Re,						// const pcre2_code *code
			( PCRE2_SPTR )m_szSubject,	// PCRE2_SPTR subject
			m_iSubjectLen,				// PCRE2_SIZE length
			m_iStart,					// PCRE2_SIZE startoffset
			uPcre2Opt,					// uint32_t options
			m_MatchData,				// pcre2_match_data *match_data
			nullptr						// pcre2_match_context *mcontext
		);
		
		if( iResult != PCRE2_ERROR_PARTIAL ) return iResult > 0;
		
		// partial 1回目，szSubject を m_szSearchBuf にコピー
		if( m_szSubject != m_szSearchBuf ){
			if( !ResizeBuf( m_iSubjectLen, m_szSearchBuf, m_iSearchBufSize ))
				return false;
			
			memcpy( m_szSearchBuf, m_szSubject, m_iSubjectLen * sizeof( wchar_t ));
			
			#ifdef _DEBUG
				if( m_iSubjectLen < m_iSearchBufSize ) m_szSearchBuf[ m_iSubjectLen ] = L'\0';
			#endif
		}
		
		// partial match したので，次行読み出し
		if( !m_GetNextLineCallback ) return false;
		const wchar_t	*pNextLine;
		int iNextSize = m_GetNextLineCallback( pNextLine, m_pCallbackParam );
		
		// partial match したけど次行がないので，partial match を外して再検索
		if( iNextSize == 0 ){
			uPcre2Opt &= ~PCRE2_PARTIAL_HARD;
			continue;
		}
		
		// SIZE_NOPARTIAL bit が立っていたら，partial option を外す
		if( iNextSize & SIZE_NOPARTIAL ){
			iNextSize &= ~SIZE_NOPARTIAL;
			uPcre2Opt &= ~PCRE2_PARTIAL_HARD;
		}
		
		// cat
		if( !ResizeBuf( m_iSubjectLen + iNextSize, m_szSearchBuf, m_iSearchBufSize ))
			return false;
		
		m_iLineTop.emplace_back( m_iSubjectLen );	// 行頭位置記憶
		memcpy( m_szSearchBuf + m_iSubjectLen, pNextLine, iNextSize * sizeof( wchar_t ));
		m_iSubjectLen += iNextSize;
		#ifdef _DEBUG
			if( m_iSubjectLen < m_iSearchBufSize ) m_szSearchBuf[ m_iSubjectLen ] = L'\0';
		#endif
		
		m_szSubject	= m_szSearchBuf;
	}
}

void CBregexp::GetMatchRange( CLogicRange *pRangeOut, int iFrom, int iTo, int iLineOffs ){
	
	// 行跨ぎなし, X の変換なし
	if( m_iLineTop.size() == 0 ){
		pRangeOut->SetFromY( CLogicInt( iLineOffs ));
		pRangeOut->SetToY	( CLogicInt( iLineOffs ));
	}
	
	// 行跨ぎあり
	else{
		// 開始行を先頭からリニアサーチ
		int i;
		for( i = 0; i < ( int )m_iLineTop.size(); ++i ){
			if( m_iLineTop[ i ] > iFrom ) break;
		}
		pRangeOut->SetFromY( CLogicInt( i + iLineOffs ));
		pRangeOut->SetFromX( CLogicInt( i == 0 ? iFrom : iFrom - m_iLineTop[ i - 1 ]));
		
		// 終了を最後からリニアサーチ
		for( i = m_iLineTop.size() - 1; i >= 0; --i ){
			if( m_iLineTop[ i ] < iTo ) break;
		}
		pRangeOut->SetToY( CLogicInt( i + 1 + iLineOffs ));
		pRangeOut->SetToX( CLogicInt( i < 0 ? iTo : iTo - m_iLineTop[ i ]));
	}
}

/*! ReplaceBuf 確保・リサイズ

	@param[in] iSize 確保サイズ (wchar_t 単位)
	@retval true: 成功
*/
bool CBregexp::ResizeBuf( int iSize, wchar_t *&pBuf, int &iBufSize ){
	
	if( iBufSize >= iSize )		return true;	// 元々必要サイズ
	if( iSize >= ( 1 << 30 ))	return false;	// 必要サイズ大きすぎ
	
	// buf サイズ更新必要
	int iBufSizeTmp = iBufSize ? iBufSize : 1024;	// 最小初期サイズ
	while( iBufSizeTmp < iSize ) iBufSizeTmp <<= 1;
	
	void *p;
	if( pBuf ){
		p = realloc( pBuf, iBufSizeTmp * sizeof( wchar_t ));	// リサイズ
	}else{	
		p = malloc( iBufSizeTmp * sizeof( wchar_t ));			// 新規取得
	}
	if( !p ) return false;	// 確保失敗
	
	pBuf		= ( wchar_t *)p;
	iBufSize	= iBufSizeTmp;
	return true;
}

/*!
	正規表現による文字列置換
	既にあるコンパイル構造体を利用して置換（1行）を
	行う．

	@param[in] szSubject 置換対象データ, null の場合 m_szSubject を使用する
	@param[in] iSubjectLen 置換対象データ長，szSubject = nul の場合 m_iSubjectLen を使用する
	@param[in] iStart 置換開始位置(0からiSubjectLen未満) -1: 前回の検索位置から
	@param[in] szReplacement 置換後文字列, null の場合 m_szReplacement を使用する

	@retval 置換個数

	@date	2007.01.16 ryoji 戻り値を置換個数に変更
*/
int CBregexp::Replace( const wchar_t *szSubject, int iSubjectLen, int iStart, const wchar_t *szReplacement ){
	
	// 必要な output buffer サイズを計算する．
	// 初期値は，2^n >= nlen * 2 となるサイズで最低 1KB．
	
	PCRE2_SIZE	OutputLen;
	
	// SearchBuf を使用する?
	if( szSubject == nullptr ){
		if( m_szSubject == nullptr || m_iSubjectLen == 0 ) return 0;
		szSubject	= m_szSubject;
		iSubjectLen	= m_iSubjectLen;
	}
	
	if( iStart < 0 ) iStart = m_iStart;
	
	if( !szReplacement ) szReplacement = m_szReplacement;
	
	int iNeededSize = iSubjectLen * 2;
	int iResult;
	
	while( 1 ){
		if( !ResizeBuf( iNeededSize, m_szReplaceBuf, m_iReplaceBufSize )) return 0;
		
		OutputLen	= m_iReplaceBufSize;
		
		// オプション
		int iOption = PCRE2_SUBSTITUTE_OVERFLOW_LENGTH | PCRE2_SUBSTITUTE_EXTENDED;
		if( m_uOption & optGlobal ) iOption |= PCRE2_SUBSTITUTE_GLOBAL;
		
		iResult = pcre2_substitute(
			m_Re,							// const pcre2_code *code
			( PCRE2_SPTR )szSubject,		// PCRE2_SPTR subject
			iSubjectLen,					// PCRE2_SIZE length
			iStart,							// PCRE2_SIZE startoffset
			iOption,						// uint32_t options
			m_MatchData,					// pcre2_match_data *match_data
			nullptr,						// pcre2_match_context *mcontext
			( PCRE2_SPTR )szReplacement,	// PCRE2_SPTR replacement,
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
	@param uOption [in] 大文字と小文字を無視して比較するフラグ // 2002/2/1 hor追加

	@retval true 正規表現は規則通り
	@retval false 文法に誤りがある。または、ライブラリが使用できない。
*/
bool CheckRegexpSyntax(
	const wchar_t*	szPattern,
	HWND			hWnd,
	bool			bShowMessage,
	UINT			uOption
){
	CBregexp cRegexp;

	if( uOption == -1 ){
		uOption = CBregexp::optCaseSensitive;
	}
	if( !cRegexp.Compile( szPattern, nullptr, uOption )){
		if( bShowMessage ){
			::MessageBox( hWnd, cRegexp.GetLastMessage(),
				LS(STR_BREGONIG_TITLE), MB_OK | MB_ICONEXCLAMATION );
		}
		return false;
	}
	return true;
}
//	To Here Jun. 26, 2001 genta
