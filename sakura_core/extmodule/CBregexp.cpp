/*!	@file
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

#include "StdAfx.h"
#include "CBregexp.h"
#include "charset/charcode.h"

CBregexp::CBregexp(){
	m_Re					= nullptr;
	m_MatchData				= nullptr;
	m_szMsg					= nullptr;
	m_iLastCode				= 0;
	m_szSearchBuf			= nullptr;
	m_iSearchBufSize		= 0;
	m_iSubjectLen			= 0;
	m_szReplaceBuf			= nullptr;
	m_iReplaceBufSize		= 0;
	m_iReplacedLen			= 0;
	
	m_GetNextLineCallback	= nullptr;
	
	// context 設定
	m_Context = pcre2_compile_context_create( nullptr );
	pcre2_set_newline( m_Context, PCRE2_NEWLINE_ANYCRLF );
}

CBregexp::~CBregexp(){
	//コンパイルバッファを解放
	ReleaseCompileBuffer();
	
	if( m_Context ) pcre2_compile_context_free( m_Context );
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
	
	if( m_szMsg ){
		delete [] m_szMsg;
		m_szMsg = nullptr;
	}
}

/*!
	JRE32のエミュレーション関数．空の文字列に対して検索・置換を行うことにより
	BREGEXP_W構造体の生成のみを行う．

	@param[in] szPattern	検索or置換パターン
	@param[in] uOption		検索・置換オプション

	@retval true 成功
	@retval false 失敗
*/
bool CBregexp::Compile( const wchar_t *szPattern, UINT uOption ){
	//	BREGEXP_W構造体の解放
	ReleaseCompileBuffer();
	
	// 単語単位検索のパターン変形
	std::wstring WordSearchPat;
	if( uOption & optWordSearch ){
		Substitute( szPattern, L"([!-/:-@\\[-`\\{-~])", L"\\\\$1", &WordSearchPat, optGlobal );
		
		// 行末が '*' なら削る
		if(
			WordSearchPat.length() >= 2 &&
			WordSearchPat[ WordSearchPat.length() - 2 ] == L'\\' &&
			WordSearchPat[ WordSearchPat.length() - 1 ] == L'*'
		){
			WordSearchPat.pop_back();
			WordSearchPat.pop_back();
		}
		
		// 行末が \w なら \b を追加
		else if(
			WordSearchPat.length() >= 1 &&
			WordSearchPat[ WordSearchPat.length() - 1 ] < 256 && (
				iswalnum( WordSearchPat[ WordSearchPat.length() - 1 ]) ||
				WordSearchPat[ WordSearchPat.length() - 1 ] == L'_'
			)
		){
			WordSearchPat += L"\\b";
		}
		
		// 先頭が '*' なら削る
		if(
			WordSearchPat.length() >= 2 &&
			WordSearchPat[ 0 ] == L'\\' &&
			WordSearchPat[ 1 ] == L'*'
		){
			szPattern = WordSearchPat.c_str() + 2;
		}
		
		// 行頭が \w なら \b を追加
		else if(
			WordSearchPat[ 0 ] < 256 && (
				iswalnum( WordSearchPat[ 0 ]) ||
				WordSearchPat[ 0 ] == L'_'
			)
		){
			WordSearchPat.insert( 0, L"\\b" );
			szPattern = WordSearchPat.c_str();
		}
		
		else{
			szPattern = WordSearchPat.c_str();
		}
	}
	
	// pcre2 opt 生成
	m_uOption = uOption;
	int iPcreOpt	= PCRE2_MULTILINE;
	if( uOption & optLiteral	){
		iPcreOpt = PCRE2_LITERAL;		// 基本検索
	}else if( uOption & optWordSearch ){
		iPcreOpt = 0;
	}
	if( uOption & optIgnoreCase	) iPcreOpt |= PCRE2_CASELESS;	// 大文字小文字区別オプション
	
	PCRE2_SIZE	sizeErrOffset;
	
	m_Re = pcre2_compile(
		( PCRE2_SPTR )szPattern,	// PCRE2_SPTR pattern
		PCRE2_ZERO_TERMINATED,		// PCRE2_SIZE length
		iPcreOpt,					// uint32_t options
		&m_iLastCode,				// int *errorcode
		&sizeErrOffset,				// PCRE2_SIZE *erroroffset
		m_Context					// pcre2_compile_context *ccontext
	);
	
	//	何らかのエラー発生。
	if( m_Re == nullptr ){
		ReleaseCompileBuffer();
		#ifdef _DEBUG
			ShowErrorMsg();
		#endif
		return false;
	}
	
	// match_data 確保
	m_MatchData = pcre2_match_data_create_from_pattern(
		m_Re, 	// uint32_t ovecsize
		nullptr	// pcre2_general_context *gcontext
	);
	
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
	
	UINT uPcre2Opt = 0;
	if(( m_uOption & ( optWordSearch | optLiteral )) || !( uOption & optPartialMatch )){
		m_uOption &= ~optPartialMatch;
	}else{
		m_uOption |= optPartialMatch;
		uPcre2Opt |= PCRE2_PARTIAL_HARD | PCRE2_NOTEOL;
	}
	
	while( 1 ){
		// match
		int m_iLastCode = pcre2_match(
			m_Re,						// const pcre2_code *code
			( PCRE2_SPTR )m_szSubject,	// PCRE2_SPTR subject
			m_iSubjectLen,				// PCRE2_SIZE length
			m_iStart,					// PCRE2_SIZE startoffset
			uPcre2Opt,					// uint32_t options
			m_MatchData,				// pcre2_match_data *match_data
			nullptr						// pcre2_match_context *mcontext
		);
		
		if( m_iLastCode != PCRE2_ERROR_PARTIAL ) return m_iLastCode > 0;
		
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
			uPcre2Opt	&= ~( PCRE2_PARTIAL_HARD | PCRE2_NOTEOL );
			uOption		&= ~optPartialMatch;
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
		pRangeOut->SetFromX( CLogicInt( iFrom ));
		pRangeOut->SetFromY( CLogicInt( iLineOffs ));
		pRangeOut->SetToX  ( CLogicInt( iTo ));
		pRangeOut->SetToY  ( CLogicInt( iLineOffs ));
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

//! match した行全体を得る，grep 結果表示用
void CBregexp::GetMatchLine( const wchar_t **ppLine, int *piLen ){
	// 行跨ぎなし, X の変換なし
	if( m_iLineTop.size() == 0 ){
		*ppLine	= m_szSubject;
		*piLen	= m_iSubjectLen;
		return;
	}
	
	// 行またぎあり
	CLogicRange Range;
	GetMatchRange( &Range );
	
	int iFrom	= Range.GetFrom().y == 0 ? 0 : m_iLineTop[ Range.GetFrom().y - 1 ];
	int iTo		= Range.GetTo().y == m_iLineTop.size() ? m_iSubjectLen : m_iLineTop[ Range.GetFrom().y ];
	
	*ppLine	= m_szSubject + iFrom;
	*piLen	= iTo - iFrom;
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
	既にあるコンパイル構造体を利用して置換（1行）を行う．

	@param[in] szSubject 置換対象データ, null の場合 m_szSubject を使用する
	@param[in] iSubjectLen 置換対象データ長，szSubject = nul の場合 m_iSubjectLen を使用する
	@param[in] iStart 置換開始位置(0からiSubjectLen未満) -1: 前回の検索位置から
	@param[in] szReplacement 置換後文字列

	@retval 置換個数，< 0 の場合エラー

	@date	2007.01.16 ryoji 戻り値を置換個数に変更
*/
int CBregexp::Replace( const wchar_t *szReplacement, const wchar_t *szSubject, int iSubjectLen, int iStart, UINT uOption ){
	
	PCRE2_SIZE	OutputLen;
	
	// SearchBuf を使用する?
	if( szSubject == nullptr ){
		if( m_szSubject == nullptr || m_iSubjectLen == 0 ) return 0;
		szSubject	= m_szSubject;
		iSubjectLen	= m_iSubjectLen;
	}
	
	if( iStart < 0 ) iStart = m_iStart;
	
	int iNeededSize = iSubjectLen * 2;
	
	m_uOption |= uOption;
	
	while( 1 ){
		if( !ResizeBuf( iNeededSize, m_szReplaceBuf, m_iReplaceBufSize )) return 0;
		
		OutputLen	= m_iReplaceBufSize;
		
		// オプション
		int iOption = PCRE2_SUBSTITUTE_OVERFLOW_LENGTH | PCRE2_SUBSTITUTE_EXTENDED;
		if( m_uOption & optGlobal )			iOption |= PCRE2_SUBSTITUTE_GLOBAL;
		if( m_uOption & optPartialMatch )	iOption |= PCRE2_NOTEOL;
		
		m_iLastCode = pcre2_substitute(
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
		
		if( m_iLastCode != PCRE2_ERROR_NOMEMORY ) break;
		
		// バッファが足りないので再試行
		iNeededSize = OutputLen;
	}
	
	// エラー
	if( m_iLastCode < 0 ){
		m_iReplacedLen	= 0;
		return m_iLastCode;
	}
	
	// 正常終了
	m_iReplacedLen	= OutputLen;
	return m_iLastCode;
}

// pcre2 エラーメッセージ取得
const wchar_t* CBregexp::GetLastMessage( void ){
	if( m_szMsg == nullptr ) m_szMsg = new wchar_t[ MSGBUF_SIZE ];
	pcre2_get_error_message( m_iLastCode, ( PCRE2_UCHAR *)m_szMsg, MSGBUF_SIZE - 1 );
	
	return m_szMsg;
}

// エラーメッセージ表示
void CBregexp::ShowErrorMsg( HWND hWnd ){
	wchar_t *szMsg = new wchar_t[ MSGBUF_SIZE ];
	pcre2_get_error_message( m_iLastCode, ( PCRE2_UCHAR *)szMsg, MSGBUF_SIZE - 1 );
	
	::MessageBox( hWnd, szMsg, LS( STR_BREGONIG_TITLE ), MB_OK | MB_ICONEXCLAMATION );
	delete [] szMsg;
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

	if( uOption == -1 ) uOption = 0;
	
	if( !cRegexp.Compile( szPattern, uOption )){
		if( bShowMessage ) cRegexp.ShowErrorMsg( hWnd );
		return false;
	}
	return true;
}

// 簡易的な subst
int CBregexp::Substitute(
	const wchar_t *szSubject, const wchar_t *szPattern,
	const wchar_t *szReplacement, std::wstring *pResult, UINT uOption
){
	CBregexp Re;
	int iRet;
	
	if( Re.Compile( szPattern, uOption ) &&
		( iRet = Re.Replace( szReplacement, szSubject, wcslen( szSubject ), 0 )) >= 0
	){
		pResult->assign( Re.m_szReplaceBuf );
		return iRet;
	}
	return -1;
}
