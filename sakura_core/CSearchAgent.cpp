/*! @file */
#include "StdAfx.h"

#include <vector>
#include <utility>
#include "CSearchAgent.h"
#include "doc/logic/CDocLineMgr.h"
#include "doc/logic/CDocLine.h"
#include "COpe.h"
#include "dlg/CDlgCancel.h"
#include "util/string_ex.h"
#include <algorithm>
#include "sakura_rc.h"
#include "CEditApp.h"
#include "CGrepAgent.h"

//#define MEASURE_SEARCH_TIME
#ifdef MEASURE_SEARCH_TIME
#include <time.h>
#endif

// CSearchStringPattern
// @date 2010.06.22 Moca
inline int CSearchStringPattern::GetMapIndex( wchar_t c )
{
	// ASCII    => 0x000 - 0x0ff
	// それ以外 => 0x100 - 0x1ff
	return ((c & 0xff00) ? 0x100 : 0 ) | (c & 0xff);
}

CSearchStringPattern::CSearchStringPattern() : 
	m_pszKey(NULL),
	m_psSearchOption(NULL),
	m_pRegexp(NULL),
	m_pszCaseKeyRef(NULL),
	m_pszPatternCase(NULL),
#ifdef SEARCH_STRING_SUNDAY_QUICK
	m_pnUseCharSkipArr(NULL)
#endif
{
}

	CSearchStringPattern::CSearchStringPattern(HWND hwnd, const wchar_t* pszPattern, int nPatternLen, const SSearchOption& sSearchOption, CBregexp* pRegexp) :
	m_pszKey(NULL),
	m_psSearchOption(NULL),
	m_pRegexp(NULL),
	m_pszCaseKeyRef(NULL),
	m_pszPatternCase(NULL),
#ifdef SEARCH_STRING_SUNDAY_QUICK
	m_pnUseCharSkipArr(NULL)
#endif
{
	SetPattern(hwnd, pszPattern, nPatternLen, sSearchOption, pRegexp);
}

CSearchStringPattern::~CSearchStringPattern()
{
	Reset();
}

void CSearchStringPattern::Reset(){
	m_pszKey = NULL;
	m_pszCaseKeyRef = NULL;
	m_psSearchOption = NULL;
	m_pRegexp = NULL;

	delete [] m_pszPatternCase;
	m_pszPatternCase = NULL;
#ifdef SEARCH_STRING_SUNDAY_QUICK
	delete [] m_pnUseCharSkipArr;
	m_pnUseCharSkipArr = NULL;
#endif
	m_uVzWordSearch	= 0;
}

bool CSearchStringPattern::SetPattern(HWND hwnd, const wchar_t* pszPattern, int nPatternLen, const SSearchOption& sSearchOption, CBregexp* regexp, bool bGlobal)
{
	Reset();
	m_pszCaseKeyRef = m_pszKey = pszPattern;
	m_nPatternLen = nPatternLen;
	m_psSearchOption = &sSearchOption;
	m_pRegexp = regexp;
	if( m_psSearchOption->bRegularExp ){
		if( !m_pRegexp ){
			return false;
		}
		if( !InitRegexp( hwnd, *m_pRegexp, true ) ){
			return false;
		}
		int nFlag = (GetLoHiCase() ? 0 : CBregexp::optIgnoreCase );
		if( bGlobal ){
			nFlag |= CBregexp::optGlobal;
		}
		/* 検索パターンのコンパイル */
		if( !m_pRegexp->Compile( pszPattern, nFlag ) ){
			return false;
		}
	}else{
		// 単語境界キャンセルの解析
		if( m_psSearchOption->bWordOnly ){
			
			if( CSearchAgent::IsAlnum( pszPattern[ 0 ])){
				// 単語先頭が英数字なら，単語境界
				m_uVzWordSearch = WORDSEARCH_TOP;
			}else if( pszPattern[ 0 ] == L'*' ){
				// '*' をスキップ
				m_pszCaseKeyRef	= m_pszKey = ++pszPattern;
				m_nPatternLen	= --nPatternLen;
			}
			
			if( CSearchAgent::IsAlnum( pszPattern[ nPatternLen - 1 ])){
				// 単語先頭が英数字なら，単語境界
				m_uVzWordSearch |= WORDSEARCH_TAIL;
			}else if( pszPattern[ nPatternLen - 1 ] == L'*' ){
				// '*' をスキップ
				m_nPatternLen	= --nPatternLen;
			}
		}
		
		if( GetIgnoreCase() ){
			m_pszPatternCase = new wchar_t[nPatternLen + 1];
			m_pszCaseKeyRef = m_pszPatternCase;
			//note: 合成文字,サロゲートの「大文字小文字同一視」未対応
			for( int i = 0; i < m_nPatternLen; i++ ){
				m_pszPatternCase[i] = (wchar_t)skr_towlower(pszPattern[i]);
			}
			m_pszPatternCase[nPatternLen] = L'\0';
		}

#ifdef SEARCH_STRING_SUNDAY_QUICK
		const int BM_MAPSIZE = 0x200;
		// 64KB も作らないで、ISO-8859-1 それ以外(包括) の2つの情報のみ記録する
		// 「あ」と「乂」　「ぅ」と「居」は値を共有している
		m_pnUseCharSkipArr = new int[BM_MAPSIZE];
		for( int n = 0; n < BM_MAPSIZE; ++n ){
			m_pnUseCharSkipArr[n] = nPatternLen + 1;
		}
		for( int n = 0; n < nPatternLen; ++n ){
			const int index = GetMapIndex(m_pszCaseKeyRef[n]);
			m_pnUseCharSkipArr[index] = nPatternLen - n;
		}
#endif
	}
	return true;
}

#define toLoHiLower(bLoHiCase, ch) (bLoHiCase? (ch) : skr_towlower(ch))

/*!
	文字列検索
	@return 見つかった場所のポインタ。見つからなかったらNULL。
*/
const wchar_t* CSearchAgent::SearchString(
	const wchar_t*	pLine,
	int				nLineLen,
	int				nIdxPos,
	const CSearchStringPattern& pattern,
	bool			bVzWordSearch
)
{
	// Vz サーチワード時は，とりあえず普通にサーチして後で単語境界をチェックする
	if( bVzWordSearch && pattern.m_uVzWordSearch ){
		const wchar_t *pRet = nullptr;
		
		while(
			nLineLen - nIdxPos >= pattern.GetLen() &&
			( pRet = SearchString( pLine, nLineLen, nIdxPos, pattern, false ))
		){
			if(
				(
					// 先頭単語境界チェックなし
					!( pattern.m_uVzWordSearch & CSearchStringPattern::WORDSEARCH_TOP ) ||
					// 行頭なら単語境界   一文字前が非英数なら単語境界
					( pLine == pRet ) || !IsAlnum( *( pRet - 1 ))
				) && (
					// 末端単語境界チェックなし
					!( pattern.m_uVzWordSearch & CSearchStringPattern::WORDSEARCH_TAIL ) ||
					// 行末なら単語境界
					( nLineLen - ( pRet - pLine ) == pattern.GetLen()) ||
					// 一文字後ろが非英数なら単語境界
					!IsAlnum( *( pRet + pattern.GetLen()))
				)
			) break; // 発見
			
			nIdxPos = pRet - pLine + 1;
			pRet = nullptr;
		}
		
		return pRet;
	}
	
	const int      nPatternLen = pattern.GetLen();
	const wchar_t* pszPattern  = pattern.GetCaseKey();
#ifdef SEARCH_STRING_SUNDAY_QUICK
	const int* const useSkipMap = pattern.GetUseCharSkipMap();
#endif
	bool bLoHiCase = pattern.GetLoHiCase();

	if( nLineLen < nPatternLen ){
		return NULL;
	}
	if( 0 >= nPatternLen || 0 >= nLineLen){
		return NULL;
	}

	// 線形探索
	const int nCompareTo = nLineLen - nPatternLen;	//	Mar. 4, 2001 genta

#if defined(SEARCH_STRING_SUNDAY_QUICK) && !defined(SEARCH_STRING_KMP)
	// SUNDAY_QUICKのみ版
	if( !bLoHiCase || nPatternLen > 5 ){
		for( int nPos = nIdxPos; nPos <= nCompareTo;){
			int i;
			for( i = 0; i < nPatternLen && toLoHiLower(bLoHiCase, pLine[nPos + i]) == pszPattern[i]; i++ ){
			}
			if( i >= nPatternLen ){
				return &pLine[nPos];
			}
			int index = CSearchStringPattern::GetMapIndex((wchar_t)toLoHiLower(bLoHiCase, pLine[nPos + nPatternLen]));
			nPos += useSkipMap[index];
		}
	} else {
		for( int nPos = nIdxPos; nPos <= nCompareTo; ){
			int n = wmemcmp( &pLine[nPos], pszPattern, nPatternLen );
			if( n == 0 ){
				return &pLine[nPos];
			}
			int index = CSearchStringPattern::GetMapIndex(pLine[nPos + nPatternLen]);
			nPos += useSkipMap[index];
		}
	}
#endif // defined(SEARCH_STRING_) && !defined(SEARCH_STRING_KMP)
	return NULL;
}

/* 現在位置の単語の範囲を調べる */
// 2001/06/23 N.Nakatani WhereCurrentWord()変更 WhereCurrentWord_2をコールするようにした
bool CSearchAgent::WhereCurrentWord(
	CLogicInt	nLineNum,
	CLogicInt	nIdx,
	CLogicInt*	pnIdxFrom,
	CLogicInt*	pnIdxTo,
	CNativeW*	pcmcmWord,
	CNativeW*	pcmcmWordLeft
)
{
	*pnIdxFrom = nIdx;
	*pnIdxTo = nIdx;

	const CDocLine*	pDocLine = m_pcDocLineMgr->GetLine( nLineNum );
	if( NULL == pDocLine ){
		return false;
	}

	CLogicInt		nLineLen;
	const wchar_t*	pLine = pDocLine->GetDocLineStrWithEOL( &nLineLen );

	/* 現在位置の単語の範囲を調べる */
	return CWordParse::WhereCurrentWord_2( pLine, nLineLen, nIdx, pnIdxFrom, pnIdxTo, pcmcmWord, pcmcmWordLeft );
}

// 現在位置の左右の単語の先頭位置を調べる
bool CSearchAgent::PrevOrNextWord(
	CLogicInt	nLineNum,		//	行数
	CLogicInt	nIdx,			//	桁数
	CLogicInt*	pnColumnNew,	//	見つかった位置
	BOOL		bLEFT,			//	TRUE:前方（左）へ向かう。FALSE:後方（右）へ向かう。
	BOOL		bStopsBothEnds	//	単語の両端で止まる
)
{
	using namespace WCODE;

	const CDocLine*	pDocLine = m_pcDocLineMgr->GetLine( nLineNum );
	if( NULL == pDocLine ){
		return false;
	}

	CLogicInt		nLineLen;
	const wchar_t*	pLine = pDocLine->GetDocLineStrWithEOL( &nLineLen );

	// ABC D[EOF]となっていたときに、Dの後ろにカーソルを合わせ、単語の左端に移動すると、Aにカーソルがあうバグ修正。YAZAKI
	if( nIdx >= nLineLen ){
		if (bLEFT && nIdx == nLineLen){
		}
		else {
			// 2011.12.26 EOFより右へ行こうとするときもfalseを返すように
			// nIdx = nLineLen - CLogicInt(1);
			return false;
		}
	}
	/* 現在位置の文字の種類によっては選択不能 */
	if( !bLEFT && WCODE::IsLineDelimiter(pLine[nIdx], GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol) ){
		return false;
	}
	/* 前の単語か？後ろの単語か？ */
	if( bLEFT ){
		/* 現在位置の文字の種類を調べる */
		ECharKind	nCharKind = CWordParse::WhatKindOfChar( pLine, nLineLen, nIdx );
		if( nIdx == 0 ){
			return false;
		}

		/* 文字種類が変わるまで前方へサーチ */
		/* 空白とタブは無視する */
		int		nCount = 0;
		CLogicInt	nIdxNext = nIdx;
		CLogicInt	nCharChars = CLogicInt(&pLine[nIdxNext] - CNativeW::GetCharPrev( pLine, nLineLen, &pLine[nIdxNext] ));
		while( nCharChars > 0 ){
			CLogicInt		nIdxNextPrev = nIdxNext;
			nIdxNext -= nCharChars;
			ECharKind nCharKindNext = CWordParse::WhatKindOfChar( pLine, nLineLen, nIdxNext );

			ECharKind nCharKindMerge = CWordParse::WhatKindOfTwoChars( nCharKindNext, nCharKind );
			if( nCharKindMerge == CK_NULL ){
				/* サーチ開始位置の文字が空白またはタブの場合 */
				if( nCharKind == CK_TAB	|| nCharKind == CK_SPACE ){
					if ( bStopsBothEnds && nCount ){
						nIdxNext = nIdxNextPrev;
						break;
					}
					nCharKindMerge = nCharKindNext;
				}else{
					if( nCount == 0){
						nCharKindMerge = nCharKindNext;
					}else{
						nIdxNext = nIdxNextPrev;
						break;
					}
				}
			}
			nCharKind = nCharKindMerge;
			nCharChars = CLogicInt(&pLine[nIdxNext] - CNativeW::GetCharPrev( pLine, nLineLen, &pLine[nIdxNext] ));
			++nCount;
		}
		*pnColumnNew = nIdxNext;
	}else{
		CWordParse::SearchNextWordPosition(pLine, nLineLen, nIdx, pnColumnNew, bStopsBothEnds);
	}
	return true;
}

/*!
 * @brief 1行内の検索を実行する
 * @retval true: hit
 */
bool CSearchAgent::SearchWord1Line(
	const CSearchStringPattern& Pattern,	//!< 検索パターン
	wchar_t		*szSubject,					//!< 検索対象文字列
	int			iSubjectSize,				//!< 検索対象文字列長
	int			iStart,						//!< 検索開始位置
	CLogicRange	*pMatchRange,				//!< hit 範囲，Y は不定
	bool		bPartial					//!< partial 検索
){
	//正規表現
	CBregexp *pRegexp = Pattern.GetRegexp();
	
	if( Pattern.GetSearchOption().bRegularExp ){
		if( !pRegexp->Match(
			szSubject, iSubjectSize, iStart,
			bPartial ? CBregexp::optPartialMatch : 0
		)){
			return false;
		}
		
		// マッチした行がある
		// SearchBuf 使用時は SearchBuf 内の index
		pMatchRange->SetFromX( CLogicInt( pRegexp->GetIndex()));		// マッチ位置 from
		pMatchRange->SetToX  ( CLogicInt( pRegexp->GetLastIndex()));	// マッチ位置 to
		
		// マッチ位置 from
		// \r\n改行時に\nにマッチすると置換できない不具合となるため
		// 改行文字内でマッチした場合、改行文字の始めからマッチしたことにする
		/* ★暫定
		pMatchRange->SetFromX( CLogicInt(
			pRegexp->GetIndex() > iSizeNoEOL ?
				iSizeNoEOL : pRegexp->GetIndex()
		));*/
		
		return true;
	}
	
	// 通常検索
	const wchar_t *pResult = SearchString(
		szSubject,
		iSubjectSize,
		iStart,
		Pattern
	);
	
	if( !pResult ) return false;	// HIT しない
	
	pMatchRange->SetFromX( CLogicInt( pResult - szSubject ));	// マッチ位置 from
	pMatchRange->SetToX  ( CLogicInt(
		pResult + Pattern.GetLen() - szSubject
	));	// マッチ位置 to
	
	return true;
}

int CSearchAgent::GetNextLine( const wchar_t *&pNextLine, void *pParam ){
	CDocLine **ppDoc = reinterpret_cast<CDocLine **>( pParam );
	
	*ppDoc = ( **ppDoc ).GetNextLine();		// 次行取得
	if( !*ppDoc ) return 0;					// 次行なし
	
	int iLen;
	pNextLine = ( wchar_t *)( **ppDoc ).GetDocLineStrWithEOL( &iLen );
	
	// この行が最終行?
	if( !( **ppDoc ).GetNextLine()) iLen |= CBregexp::SIZE_NOPARTIAL;
	
	return iLen;
}

/*! 単語検索
 * @retval 0:見つからない
 */
int CSearchAgent::SearchWord(
	CLogicPoint				ptSerachBegin,	//!< 検索開始位置
	ESearchDirection		eDirection,		//!< 検索方向
	CLogicRange*			pMatchRange,	//!< [out] マッチ範囲。ロジック単位。
	const CSearchStringPattern&	pattern		//!< 検索パターン
){
	CLogicInt	nIdxPos		= ptSerachBegin.x;
	CLogicInt	nLinePos	= ptSerachBegin.GetY2();
	CDocLine*	pDocLine	= m_pcDocLineMgr->GetLine( nLinePos );
	
	CDocLine*	pDocLineGetNext;
	
	bool bRe = pattern.GetSearchOption().bRegularExp;
	
	// 正規表現の場合，次行取得コールバック設定
	if( bRe ){
		pattern.GetRegexp()->SetNextLineCallback( GetNextLine, &pDocLineGetNext );
	}
	
	// 前方検索
	if( eDirection & SEARCH_FORWARD ){
		while( NULL != pDocLine ){
			int iSubjectSize;
			wchar_t *szSubject = ( wchar_t *)pDocLine->GetDocLineStrWithEOL( &iSubjectSize );
			pDocLineGetNext = pDocLine;
			
			if( SearchWord1Line(
				pattern, szSubject, iSubjectSize, nIdxPos, pMatchRange,
				( eDirection & SEARCH_PARTIAL ) ? true : false
			)){
				break;	// hit
			}
			
			++nLinePos;
			pDocLine = pDocLine->GetNextLine();
			nIdxPos = 0;
		}
	}
	
	// 後方検索
	else{
		int iXLimit = -1;	// 検索開始位置
		
		while( NULL != pDocLine ){
			int iSubjectSize;
			wchar_t *szSubject = ( wchar_t *)pDocLine->GetDocLineStrWithEOL( &iSubjectSize );
			pDocLineGetNext = pDocLine;
			
			CLogicRange	LastRange;
			LastRange.SetFromX( CLogicInt( -1 ));
			CLogicInt	StartPos( CLogicInt( 0 ));
			
			// re 時は複数行が SearchBuf に溜まっていくので，
			//   行を遡るごとに iXLimit は増加する．
			// 非 re 時は加算することにあまり意味は無い．
			iXLimit = iXLimit < 0 ? nIdxPos : iXLimit + iSubjectSize;
			
			// 行頭から iXLimit に達するまで連続で検索し，最後に hit したものが後方検索の結果
			while( SearchWord1Line(
				pattern, szSubject, iSubjectSize, StartPos, pMatchRange,
				( eDirection & SEARCH_PARTIAL ) ? true : false
			)){
				if( pMatchRange->GetFrom().x >= iXLimit ) break;
				LastRange	= *pMatchRange;
				
				// 長さ 0 の場合は再度マッチしないように 1文字進める
				StartPos	= LastRange.GetFrom().x == LastRange.GetTo().x ?
					LastRange.GetTo().x + 1 : LastRange.GetTo().x;
				
				// 正規表現かつ szSearchBuf の続きから検索する場合
				if( bRe && pattern.GetRegexp()->GetSubjectLen()){
					szSubject = nullptr;
				}
			}
			
			// hit した
			if( LastRange.GetFrom().x >= 0 ){
				*pMatchRange = LastRange;
				break;
			}
			
			--nLinePos;
			pDocLine = pDocLine->GetPrevLine();
		}
	}
	
	if( !pDocLine ) return 0;
	
	// Y 補正
	if( bRe ){
		pattern.GetRegexp()->GetMatchRange( pMatchRange, nLinePos );
	}else{
		pMatchRange->SetFromY( nLinePos );	// マッチ行
		pMatchRange->SetToY  ( nLinePos );	// マッチ行
	}
	
#ifdef MEASURE_SEARCH_TIME
	clockEnd = clock();
	TCHAR buf[100];
	memset(buf, 0x00, sizeof(buf));
	wsprintf( buf, _T("%d"), clockEnd - clockStart);
	::MessageBox( NULL, buf, GSTR_APPNAME, MB_OK );
#endif
	
	return 1;
}

/* 指定範囲のデータを置換(削除 & データを挿入)
  Fromを含む位置からToの直前を含むデータを削除する
  Fromの位置へテキストを挿入する
*/
void CSearchAgent::ReplaceData( DocLineReplaceArg* pArg )
{
//	MY_RUNNINGTIMER( cRunningTimer, "CDocLineMgr::ReplaceData()" );

	/* 挿入によって増えた行の数 */
	pArg->nInsLineNum = CLogicInt(0);
	/* 削除した行の総数 */
	pArg->nDeletedLineNum = CLogicInt(0);
	/* 削除されたデータ */
	if( pArg->pcmemDeleted ){
		pArg->pcmemDeleted->clear();
	}

	CDocLine* pCDocLine;
	CDocLine* pCDocLinePrev;
	CDocLine* pCDocLineNext;
	int nWorkPos;
	int nWorkLen;
	const wchar_t* pLine;
	int nLineLen;
	int i;
	CLogicInt	nAllLinesOld;
	int			nProgress;
	CDocLine::MarkType	markNext;
	//	May 15, 2000
	HWND		hwndCancel = NULL;	//	初期化
	HWND		hwndProgress = NULL;	//	初期化

	pArg->ptNewPos = pArg->sDelRange.GetFrom();

	/* 大量のデータを操作するとき */
	CDlgCancel*	pCDlgCancel = NULL;
	class CDLgCandelCloser{
		CDlgCancel*& m_pDlg;
	public:
		CDLgCandelCloser(CDlgCancel*& pDlg): m_pDlg(pDlg){}
		~CDLgCandelCloser(){
			if( NULL != m_pDlg ){
				// 進捗ダイアログを表示しない場合と同じ動きになるようにダイアログは遅延破棄する
				// ここで pCDlgCancel を delete すると delete から戻るまでの間に
				// ダイアログ破棄 -> 編集画面へフォーカス移動 -> キャレット位置調整
				// まで一気に動くので無効なレイアウト情報参照で異常終了することがある
				m_pDlg->DeleteAsync();	// 自動破棄を遅延実行する	// 2008.05.28 ryoji
			}
		}
	};
	CDLgCandelCloser closer(pCDlgCancel);
	const CLogicInt nDelLines = pArg->sDelRange.GetTo().y - pArg->sDelRange.GetFrom().y;
	const CLogicInt nEditLines = std::max<CLogicInt>(CLogicInt(1), nDelLines + CLogicInt(pArg->pInsData ? pArg->pInsData->size(): 0));
	if( !CEditApp::getInstance()->m_pcGrepAgent->m_bGrepRunning ){
		if( 3000 < nEditLines ){
			/* 進捗ダイアログの表示 */
			pCDlgCancel = new CDlgCancel;
			if( NULL != ( hwndCancel = pCDlgCancel->DoModeless( ::GetModuleHandle( NULL ), NULL, IDD_OPERATIONRUNNING ) ) ){
				hwndProgress = ::GetDlgItem( hwndCancel, IDC_PROGRESS );
				Progress_SetRange( hwndProgress, 0, 101 );
				Progress_SetPos( hwndProgress, 0 );
			}
		}
	}
	int nProgressOld = 0;

	// バッファを確保
	if( pArg->pcmemDeleted ){
		pArg->pcmemDeleted->reserve( pArg->sDelRange.GetTo().y + CLogicInt(1) - pArg->sDelRange.GetFrom().y );
	}

	// 2012.01.10 行内の削除&挿入のときの操作を1つにする
	bool bChangeOneLine = false;	// 行内の挿入
	bool bInsOneLine = false;
	bool bLastEOLReplace = false;	// 「最後改行」を「最後改行」で置換
	if( pArg->pInsData && 0 < pArg->pInsData->size() ){
		const CNativeW& cmemLine = pArg->pInsData->back().cmemLine;
		int nLen = cmemLine.GetStringLength();
		const wchar_t* pInsLine = cmemLine.GetStringPtr();
		if( 0 < nLen && WCODE::IsLineDelimiter(pInsLine[nLen - 1], GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol) ){
			// 行挿入
			bLastEOLReplace = true; // 仮。後で修正
		}else{
			if( 1 == pArg->pInsData->size() ){
				bChangeOneLine = true; // 「abc\ndef」=>「123」のような置換もtrueなのに注意
			}
		}
	}
	const wchar_t* pInsData = L"";
	int nInsLen = 0;
	int nSetSeq = 0;
	if( bChangeOneLine ){
		nInsLen = pArg->pInsData->back().cmemLine.GetStringLength();
		pInsData = pArg->pInsData->back().cmemLine.GetStringPtr();
		nSetSeq = pArg->pInsData->back().nSeq;
	}

	/* 現在行の情報を得る */
	pCDocLine = m_pcDocLineMgr->GetLine( pArg->sDelRange.GetTo().GetY2() );
	i = pArg->sDelRange.GetTo().y;
	if( 0 < pArg->sDelRange.GetTo().y && NULL == pCDocLine ){
		pCDocLine = m_pcDocLineMgr->GetLine( pArg->sDelRange.GetTo().GetY2() - CLogicInt(1) );
		i--;
	}
	bool bFirstLine = true;
	bool bSetMark = false;
	/* 後ろから処理していく */
	for( ; i >= pArg->sDelRange.GetFrom().y && NULL != pCDocLine; i-- ){
		pLine = pCDocLine->GetPtr(); // 2002/2/10 aroka CMemory変更
		nLineLen = pCDocLine->GetLengthWithEOL(); // 2002/2/10 aroka CMemory変更
		pCDocLinePrev = pCDocLine->GetPrevLine();
		pCDocLineNext = pCDocLine->GetNextLine();
		/* 現在行の削除開始位置を調べる */
		if( i == pArg->sDelRange.GetFrom().y ){
			nWorkPos = pArg->sDelRange.GetFrom().x;
		}else{
			nWorkPos = 0;
		}
		/* 現在行の削除データ長を調べる */
		if( i == pArg->sDelRange.GetTo().y ){
			nWorkLen = pArg->sDelRange.GetTo().x - nWorkPos;
		}else{
			nWorkLen = nLineLen - nWorkPos; // 2002/2/10 aroka CMemory変更
		}

		if( 0 == nWorkLen ){
			/* 前の行へ */
			goto prev_line;
		}
		/* 改行も削除するんかぃのぉ・・・？ */
		if( EOL_NONE != pCDocLine->GetEol() &&
			nWorkPos + nWorkLen > nLineLen - pCDocLine->GetEol().GetLen() // 2002/2/10 aroka CMemory変更
		){
			/* 削除する長さに改行も含める */
			nWorkLen = nLineLen - nWorkPos; // 2002/2/10 aroka CMemory変更
		}

		/* 行全体の削除 */
		if( nWorkLen >= nLineLen ){ // 2002/2/10 aroka CMemory変更
			/* 削除した行の総数 */
			++(pArg->nDeletedLineNum);
			/* 行オブジェクトの削除、リスト変更、行数-- */
			if( pArg->pcmemDeleted ){
				CLineData tmp;
				pArg->pcmemDeleted->push_back(tmp);
				CLineData& delLine = pArg->pcmemDeleted->back();
				delLine.cmemLine.swap(pCDocLine->_GetDocLineData()); // CDocLine書き換え
				delLine.nSeq = CModifyVisitor().GetLineModifiedSeq(pCDocLine);
			}
			m_pcDocLineMgr->DeleteLine( pCDocLine );
			pCDocLine = NULL;
		}
		/* 次の行と連結するような削除 */
		else if( nWorkPos + nWorkLen >= nLineLen ){ // 2002/2/10 aroka CMemory変更
			if( pArg->pcmemDeleted ){
				if( pCDocLineNext && 0 == pArg->pcmemDeleted->size() ){
					// 1行以内の行末削除のときだけ、次の行のseqが保存されないので必要
					// 2014.01.07 最後が改行の範囲を最後が改行のデータで置換した場合を変更
					if( !bLastEOLReplace ){
						CLineData tmp;
						pArg->pcmemDeleted->push_back(tmp);
						CLineData& delLine =  pArg->pcmemDeleted->back();
						delLine.cmemLine.SetString(L"");
						delLine.nSeq = CModifyVisitor().GetLineModifiedSeq(pCDocLineNext);
					}
				}
				CLineData tmp;
				pArg->pcmemDeleted->push_back(tmp);
				CLineData& delLine = pArg->pcmemDeleted->back();
				delLine.cmemLine.SetString(&pLine[nWorkPos], nWorkLen);
				delLine.nSeq = CModifyVisitor().GetLineModifiedSeq(pCDocLine);
			}

			/* 次の行がある */
			if( pCDocLineNext ){
				/* 次の行のデータを最後に追加 */
				// 改行を削除するような置換
				int nNewLen = nWorkPos + pCDocLineNext->GetLengthWithEOL() + nInsLen;
				if( nWorkLen <= nWorkPos && nLineLen <= nNewLen + 10 ){
					// 行を連結して1行にするような操作の高速化
					// 削除が元データの有効長以下で行の長さが伸びるか少し減る場合reallocを試みる
					static CDocLine* pDocLinePrevAccess = NULL;
					static int nAccessCount = 0;
					int nBufferReserve = nNewLen;
					if( pDocLinePrevAccess == pCDocLine ){
						if( 100 < nAccessCount ){
							if( 1000 < nNewLen ){
								int n = 1000;
								while( n < nNewLen ){
									n += n / 5; // 20%づつ伸ばす
								}
								nBufferReserve = n;
							}
						}else{
							nAccessCount++;
						}
					}else{
						pDocLinePrevAccess = pCDocLine;
						nAccessCount = 0;
					}
					CNativeW& ref = pCDocLine->_GetDocLineData();
					ref.AllocStringBuffer(nBufferReserve);
					ref._SetStringLength(nWorkPos);
					ref.AppendString(pInsData, nInsLen);
					ref.AppendNativeData(pCDocLineNext->_GetDocLineDataWithEOL());
					pCDocLine->SetEol();
				}else{
					CNativeW tmp;
					tmp.AllocStringBuffer(nNewLen);
					tmp.AppendString(pLine, nWorkPos);
					tmp.AppendString(pInsData, nInsLen);
					tmp.AppendNativeData(pCDocLineNext->_GetDocLineDataWithEOL());
					pCDocLine->SetDocLineStringMove(&tmp);
				}
				if( bChangeOneLine ){
					pArg->nInsSeq = CModifyVisitor().GetLineModifiedSeq(pCDocLine);
					CModifyVisitor().SetLineModified(pCDocLine, nSetSeq);
					if( !bInsOneLine ){
						pArg->ptNewPos.x = pArg->ptNewPos.x + nInsLen;
						bInsOneLine = true;
					}
				}else{
					CModifyVisitor().SetLineModified(pCDocLine, pArg->nDelSeq);
					// 削除される行のマーク類を保存
					markNext = pCDocLineNext->m_sMark;
					bSetMark = true;
				}

				/* 次の行 行オブジェクトの削除 */
				m_pcDocLineMgr->DeleteLine( pCDocLineNext );
				pCDocLineNext = NULL;

				/* 削除した行の総数 */
				++(pArg->nDeletedLineNum);
			}else{
				/* 行内データ削除 */
				CNativeW tmp;
				tmp.SetString(pLine, nWorkPos);
				pCDocLine->SetDocLineStringMove(&tmp);
				CModifyVisitor().SetLineModified(pCDocLine, pArg->nDelSeq);	/* 変更フラグ */
			}
		}
		else{
			/* 行内だけの削除 */
			if( pArg->pcmemDeleted ){
				CLineData tmp;
				pArg->pcmemDeleted->push_back(tmp);
				CLineData& delLine =  pArg->pcmemDeleted->back();
				delLine.cmemLine.SetString(&pLine[nWorkPos], nWorkLen);
				delLine.nSeq = CModifyVisitor().GetLineModifiedSeq(pCDocLine);
			}
			{// 20020119 aroka ブロック内に pWork を閉じ込めた
				// 2002/2/10 aroka CMemory変更 何度も GetLength,GetPtr をよばない。
				int nNewLen = nLineLen - nWorkLen + nInsLen;
				int nAfterLen = nLineLen - (nWorkPos + nWorkLen);
				if( pCDocLine->_GetDocLineData().capacity() * 9 / 10 < nNewLen
					&& nNewLen <= pCDocLine->_GetDocLineData().capacity() ){
					CNativeW& ref = pCDocLine->_GetDocLineData();
					WCHAR* pBuf = const_cast<WCHAR*>(ref.GetStringPtr());
					if( nWorkLen != nInsLen ){
						wmemmove(&pBuf[nWorkPos + nInsLen], &pLine[nWorkPos + nWorkLen], nAfterLen);
					}
					wmemcpy(&pBuf[nWorkPos], pInsData, nInsLen);
					ref._SetStringLength(nNewLen);
				}else{
					int nBufferSize = 16;
					if( 1000 < nNewLen ){
						nBufferSize = 1000;
						while( nBufferSize < nNewLen ){
							nBufferSize += nBufferSize / 20; // 5%づつ伸ばす
						}
					}
					CNativeW tmp;
					tmp.AllocStringBuffer(nBufferSize);
					tmp.AppendString(pLine, nWorkPos);
					tmp.AppendString(pInsData, nInsLen);
					tmp.AppendString(&pLine[nWorkPos + nWorkLen], nAfterLen);
					pCDocLine->SetDocLineStringMove(&tmp);
				}
			}
			if( bChangeOneLine ){
				pArg->nInsSeq = CModifyVisitor().GetLineModifiedSeq(pCDocLine);
				CModifyVisitor().SetLineModified(pCDocLine, nSetSeq);
				pArg->ptNewPos.x = pArg->ptNewPos.x + nInsLen;
				bInsOneLine = true;
				pInsData = L"";
				nInsLen = 0;
			}else{
				CModifyVisitor().SetLineModified(pCDocLine, pArg->nDelSeq);
			}
			if( bFirstLine ){
				bLastEOLReplace = false;
			}
		}
		bFirstLine = false;

prev_line:;
		/* 直前の行のオブジェクトのポインタ */
		pCDocLine = pCDocLinePrev;
		/* 最近参照した行番号と行データ */
		--m_pcDocLineMgr->m_nPrevReferLine;
		m_pcDocLineMgr->m_pCodePrevRefer = pCDocLine;

		if( NULL != hwndCancel){
			int nLines = pArg->sDelRange.GetTo().y - i;
			if( 0 == (nLines % 32) ){
				nProgress = ::MulDiv(nLines, 100, nEditLines);
				if( nProgressOld != nProgress ){
					nProgressOld = nProgress;
					Progress_SetPos( hwndProgress, nProgress + 1 );
					Progress_SetPos( hwndProgress, nProgress );
				}
			}
		}
	}

	if( pArg->pcmemDeleted ){
		// 下から格納されているのでひっくり返す
		std::reverse(pArg->pcmemDeleted->begin(), pArg->pcmemDeleted->end());
	}
	if( bInsOneLine ){
		// 挿入済み
		return;
	}

	/* データ挿入処理 */
	if( NULL == pArg->pInsData || 0 == pArg->pInsData->size() ){
		pArg->nInsSeq = 0;
		return;
	}
	nAllLinesOld= m_pcDocLineMgr->GetLineCount();
	pArg->ptNewPos.y = pArg->sDelRange.GetFrom().y;	/* 挿入された部分の次の位置の行 */
	pArg->ptNewPos.x = 0;	/* 挿入された部分の次の位置のデータ位置 */

	/* 挿入データを行終端で区切った行数カウンタ */
	pCDocLine = m_pcDocLineMgr->GetLine( pArg->sDelRange.GetFrom().GetY2() );

	int nInsSize = pArg->pInsData->size();
	bool bInsertLineMode = false;
	bool bLastInsert = false;
	{
		CNativeW& cmemLine = pArg->pInsData->back().cmemLine;
		int nLen = cmemLine.GetStringLength();
		const wchar_t* pInsLine = cmemLine.GetStringPtr();
		if( 0 < nLen && WCODE::IsLineDelimiter(pInsLine[nLen - 1], GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol) ){
			if( 0 == pArg->sDelRange.GetFrom().x ){
				// 挿入データの最後が改行で行頭に挿入するとき、現在行を維持する
				bInsertLineMode = true;
				if( pCDocLine && m_pcDocLineMgr->m_pCodePrevRefer == pCDocLine ){
					m_pcDocLineMgr->m_pCodePrevRefer = pCDocLine->GetPrevLine();
					if( m_pcDocLineMgr->m_pCodePrevRefer ){
						m_pcDocLineMgr->m_nPrevReferLine--;
					}
				}
			}
		}else{
			bLastInsert = true;
			nInsSize--;
		}
	}
	CStringRef	cPrevLine;
	CStringRef	cNextLine;
	CNativeW	cmemCurLine;
	if( NULL == pCDocLine ){
		/* ここでNULLが帰ってくるということは、*/
		/* 全テキストの最後の次の行を追加しようとしていることを示す */
		pArg->nInsSeq = 0;
	}else{
		// 2002/2/10 aroka 何度も GetPtr を呼ばない
		if( !bInsertLineMode ){
			cmemCurLine.swap(pCDocLine->_GetDocLineData());
			pLine = cmemCurLine.GetStringPtr(&nLineLen);
			cPrevLine = CStringRef(pLine, pArg->sDelRange.GetFrom().x);
			cNextLine = CStringRef(&pLine[pArg->sDelRange.GetFrom().x], nLineLen - pArg->sDelRange.GetFrom().x);
			pArg->nInsSeq = CModifyVisitor().GetLineModifiedSeq(pCDocLine);
		}else{
			pArg->nInsSeq = 0;
		}
	}
	int nCount;
	for( nCount = 0; nCount < nInsSize; nCount++ ){
		CNativeW& cmemLine = (*pArg->pInsData)[nCount].cmemLine;
#ifdef _DEBUG
		int nLen = cmemLine.GetStringLength();
		const wchar_t* pInsLine = cmemLine.GetStringPtr();
		assert( 0 < nLen && WCODE::IsLineDelimiter(pInsLine[nLen - 1], GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol) );
#endif
		{
			if( NULL == pCDocLine ){
				CDocLine* pCDocLineNew = m_pcDocLineMgr->AddNewLine();

				/* 挿入データを行終端で区切った行数カウンタ */
				if( 0 == nCount ){
					CNativeW tmp;
					tmp.AllocStringBuffer(cPrevLine.GetLength() + cmemLine.GetStringLength());
					tmp.AppendString(cPrevLine.GetPtr(), cPrevLine.GetLength());
					tmp.AppendNativeData(cmemLine);
					pCDocLineNew->SetDocLineStringMove(&tmp);
				}
				else{
					pCDocLineNew->SetDocLineStringMove(&cmemLine);
				}
				CModifyVisitor().SetLineModified(pCDocLineNew, (*pArg->pInsData)[nCount].nSeq);
			}
			else{
				/* 挿入データを行終端で区切った行数カウンタ */
				if( 0 == nCount && !bInsertLineMode ){
					if( cmemCurLine.GetStringLength() - cPrevLine.GetLength() < cmemCurLine.GetStringLength() / 100
						&& cPrevLine.GetLength() + cmemLine.GetStringLength() <= cmemCurLine.GetStringLength()
						&& cmemCurLine.capacity() / 2 <= cPrevLine.GetLength() + cmemLine.GetStringLength() ){
						// 行のうちNextになるのが1%以下で行が短くなるなら再利用する(長い一行を分割する場合の最適化)
						CNativeW tmp; // Nextを退避
						tmp.SetString(cNextLine.GetPtr(), cNextLine.GetLength());
						cmemCurLine.swap(tmp);
						tmp._SetStringLength(cPrevLine.GetLength());
						tmp.AppendNativeData(cmemLine);
						pCDocLine->SetDocLineStringMove(&tmp);
						cNextLine = CStringRef(cmemCurLine.GetStringPtr(), cmemCurLine.GetStringLength());
					}else{
						CNativeW tmp;
						tmp.AllocStringBuffer(cPrevLine.GetLength() + cmemLine.GetStringLength());
						tmp.AppendString(cPrevLine.GetPtr(), cPrevLine.GetLength());
						tmp.AppendNativeData(cmemLine);
						pCDocLine->SetDocLineStringMove(&tmp);
					}
					CModifyVisitor().SetLineModified(pCDocLine, (*pArg->pInsData)[nCount].nSeq);
					pCDocLine = pCDocLine->GetNextLine();
				}
				else{
					CDocLine* pCDocLineNew = m_pcDocLineMgr->InsertNewLine(pCDocLine);	//pCDocLineの前に挿入
					pCDocLineNew->SetDocLineStringMove(&cmemLine);
					CModifyVisitor().SetLineModified(pCDocLineNew, (*pArg->pInsData)[nCount].nSeq);
				}
			}

			/* 挿入データを行終端で区切った行数カウンタ */
			++(pArg->ptNewPos.y);	/* 挿入された部分の次の位置の行 */
			if( NULL != hwndCancel ){
				if( 0 == (nCount % 32) ){
					nProgress = ::MulDiv(nCount + nDelLines, 100, nEditLines);
					if( nProgressOld != nProgress ){
						nProgressOld = nProgress;
						Progress_SetPos( hwndProgress, nProgress + 1 );
						Progress_SetPos( hwndProgress, nProgress );
					}
				}
			}
		}
	}
	if( bLastInsert || 0 < cNextLine.GetLength() ){
		CNativeW cNull;
		CStringRef cNullStr(L"", 0);
		CNativeW& cmemLine = bLastInsert ? pArg->pInsData->back().cmemLine : cNull;
		const CStringRef& cPrevLine2 = ((0 == nCount) ? cPrevLine: cNullStr);
		int nSeq = pArg->pInsData->back().nSeq;
		int nLen = cmemLine.GetStringLength();
		CNativeW tmp;
		tmp.AllocStringBuffer(cPrevLine2.GetLength() + cmemLine.GetStringLength() + cNextLine.GetLength());
		tmp.AppendString(cPrevLine2.GetPtr(), cPrevLine2.GetLength());
		tmp.AppendNativeData(cmemLine);
		tmp.AppendString(cNextLine.GetPtr(), cNextLine.GetLength());
		if( NULL == pCDocLine ){
			CDocLine* pCDocLineNew = m_pcDocLineMgr->AddNewLine();	//末尾に追加
			pCDocLineNew->SetDocLineStringMove(&tmp);
			pCDocLineNew->m_sMark = markNext;
			if( !bLastEOLReplace || !bSetMark ){
				CModifyVisitor().SetLineModified(pCDocLineNew, nSeq);
			}
			pArg->ptNewPos.x = nLen;	/* 挿入された部分の次の位置のデータ位置 */
		}else{
			if( 0 == nCount ){
				// 行の中間に挿入(削除データがなかった。1文字入力など)
			}else{
				// 複数行挿入の最後の行
				pCDocLine = m_pcDocLineMgr->InsertNewLine(pCDocLine);	//pCDocLineの前に挿入
				pCDocLine->m_sMark = markNext;
			}
			pCDocLine->SetDocLineStringMove(&tmp);
			if( !bLastEOLReplace || !bSetMark ){
				CModifyVisitor().SetLineModified(pCDocLine, nSeq);
			}
			pArg->ptNewPos.x = cPrevLine2.GetLength() + nLen;	/* 挿入された部分の次の位置のデータ位置 */
		}
	}
	pArg->nInsLineNum = m_pcDocLineMgr->GetLineCount() - nAllLinesOld;
	return;
}
