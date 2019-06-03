﻿/*! @file */
#include "StdAfx.h"

#include "CSearchAgent.h"
#include "doc/logic/CDocLineMgr.h"
#include "doc/logic/CDocLine.h"
#include "COpe.h"
#include "dlg/CDlgCancel.h"
#include "util/string_ex.h"
#include "sakura_rc.h"
#include "CEditApp.h"
#include "CGrepAgent.h"

//#define MEASURE_SEARCH_TIME
#ifdef MEASURE_SEARCH_TIME
#include <time.h>
#endif

CSearchStringPattern::CSearchStringPattern() : 
	m_psSearchOption(NULL),
	m_pRegexp(NULL)
{}

	CSearchStringPattern::CSearchStringPattern(HWND hwnd, const wchar_t* pszPattern, int nPatternLen, const SSearchOption& sSearchOption, CBregexp* pRegexp) :
	m_psSearchOption(NULL),
	m_pRegexp(NULL)
{
	SetPattern(hwnd, pszPattern, nPatternLen, sSearchOption, pRegexp);
}

CSearchStringPattern::~CSearchStringPattern()
{
	Reset();
}

void CSearchStringPattern::Reset(){
	m_psSearchOption = NULL;
	m_pRegexp = NULL;
}

bool CSearchStringPattern::SetPattern(HWND hwnd, const wchar_t* pszPattern, int nPatternLen, const SSearchOption& sSearchOption, CBregexp* regexp, bool bGlobal)
{
	Reset();
	m_psSearchOption = &sSearchOption;
	m_pRegexp = regexp;
	
	if( !m_pRegexp ) return false;
	
	int nFlag = 0;
	if( GetIgnoreCase())	nFlag |= CBregexp::optIgnoreCase;
	if( bGlobal ) 			nFlag |= CBregexp::optGlobal;
	if( !m_psSearchOption->bRegularExp ){
		if( m_psSearchOption->bWordOnly ){
			nFlag |= CBregexp::optWordSearch;
		}else{
			nFlag |= CBregexp::optLiteral;
		}
	}
	
	/* 検索パターンのコンパイル */
	return m_pRegexp->Compile( pszPattern, nFlag );
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
	UINT		uOption						//!< grep オプション
){
	//正規表現
	CBregexp *pRegexp = Pattern.GetRegexp();
	
	if( !pRegexp->Match( szSubject, iSubjectSize, iStart, uOption )){
		return false;
	}
	
	// マッチした行がある
	// SearchBuf 使用時は SearchBuf 内の index
	pMatchRange->SetFromX( CLogicInt( pRegexp->GetIndex()));		// マッチ位置 from
	pMatchRange->SetToX  ( CLogicInt( pRegexp->GetLastIndex()));	// マッチ位置 to
	
	return true;
}

// GetNextLine 用 param
class CGetNextLineInfo {
public:
	CDocLine	*m_pDoc;
	CLogicInt	m_iLineNo;
	
	CGetNextLineInfo( CDocLine *pDoc, int iLineNo ) : m_pDoc( pDoc ), m_iLineNo( iLineNo ){};
};

int CSearchAgent::GetNextLine( const wchar_t *&pNextLine, void *pParam ){
	CGetNextLineInfo& DocInfo = *reinterpret_cast<CGetNextLineInfo *>( pParam );
	
	DocInfo.m_pDoc = DocInfo.m_pDoc->GetNextLine();		// 次行取得
	if( !DocInfo.m_pDoc ) return 0;						// 次行なし
	
	int iLen;
	pNextLine = ( wchar_t *)DocInfo.m_pDoc->GetDocLineStrWithEOL( &iLen );
	
	// この行が最終行?
	if( !DocInfo.m_pDoc->GetNextLine()) iLen |= CBregexp::SIZE_NOPARTIAL;
	++DocInfo.m_iLineNo;
	
	return iLen;
}

/*! 単語検索
 * @retval 0:見つからない
 */
int CSearchAgent::SearchWord(
	CLogicPoint				ptSerachBegin,	//!< 検索開始位置
	ESearchDirection		eDirection,		//!< 検索方向
	CLogicRange*			pMatchRange,	//!< [out] マッチ範囲。ロジック単位。
	const CSearchStringPattern&	pattern,	//!< 検索パターン
	UINT					uOption			//!< grep オプション
){
	CLogicInt	nIdxPos		= ptSerachBegin.x;
	CLogicInt	iLineNo		= ptSerachBegin.GetY2();
	CGetNextLineInfo	DocInfo( m_pcDocLineMgr->GetLine( iLineNo ), iLineNo );
	
	bool bRe	= pattern.GetSearchOption().bRegularExp;
	bool bHit	= false;
	
	// 正規表現の場合，次行取得コールバック設定
	if( bRe ){
		pattern.GetRegexp()->SetNextLineCallback( GetNextLine, &DocInfo );
	}
	
	// 前方検索
	if( eDirection & SEARCH_FORWARD ){
		while( NULL != DocInfo.m_pDoc ){
			int iSubjectSize;
			wchar_t *szSubject = ( wchar_t *)DocInfo.m_pDoc->GetDocLineStrWithEOL( &iSubjectSize );
			
			iLineNo = DocInfo.m_iLineNo; // 検索開始行
			if( SearchWord1Line( pattern, szSubject, iSubjectSize, nIdxPos, pMatchRange, uOption )){
				bHit = true;
				break;	// hit
			}
			
			if( !DocInfo.m_pDoc ) break;	// GetNextLine 中で EOF に達した
			
			++DocInfo.m_iLineNo;
			DocInfo.m_pDoc = DocInfo.m_pDoc->GetNextLine();
			nIdxPos = 0;
			
			// 次の行を読んだので optNotBol 解除．
			// optNotBol は，ReplaceAll でしか使用しない，
			//   つまり前方検索しか無いので後方検索には無い．
			uOption &= ~CBregexp::optNotBol;
		}
	}
	
	// 後方検索
	else{
		int iXLimit = -1;	// 検索開始位置
		
		while( NULL != DocInfo.m_pDoc ){
			int iSubjectSize;
			wchar_t *szSubject = ( wchar_t *)DocInfo.m_pDoc->GetDocLineStrWithEOL( &iSubjectSize );
			
			CLogicRange	LastRange;
			LastRange.SetFromX( CLogicInt( -1 ));
			CLogicInt	StartPos( CLogicInt( 0 ));
			
			// re 時は複数行が SearchBuf に溜まっていくので，
			//   行を遡るごとに iXLimit は増加する．
			// 非 re 時は加算することにあまり意味は無い．
			iXLimit = iXLimit < 0 ? nIdxPos : iXLimit + iSubjectSize;
			
			iLineNo = DocInfo.m_iLineNo; // 検索開始行
			
			// 行頭から iXLimit に達するまで連続で検索し，最後に hit したものが後方検索の結果
			while( SearchWord1Line( pattern, szSubject, iSubjectSize, StartPos, pMatchRange, uOption )){
				if( pMatchRange->GetFrom().x >= iXLimit ) break;
				LastRange	= *pMatchRange;
				
				// 長さ 0 の場合は再度マッチしないように 1文字進める
				StartPos	= LastRange.GetFrom().x == LastRange.GetTo().x ?
					LastRange.GetTo().x + 1 : LastRange.GetTo().x;
				
				// Re 時，szSearchBuf の続きから検索する場合に備えて null にする
				if( bRe ) szSubject = nullptr;
			}
			
			// hit した
			if( LastRange.GetFrom().x >= 0 ){
				*pMatchRange = LastRange;
				bHit = true;
				break;
			}
			
			if( !DocInfo.m_pDoc ) break;	// GetNextLine 中で EOF に達した
			
			--DocInfo.m_iLineNo;
			DocInfo.m_pDoc = DocInfo.m_pDoc->GetPrevLine();
		}
	}
	
	if( !bHit ) return 0;
	
	// Y 補正
	if( bRe ){
		pattern.GetRegexp()->GetMatchRange( pMatchRange, pMatchRange, iLineNo );
	}else{
		pMatchRange->SetFromY( iLineNo );	// マッチ行
		pMatchRange->SetToY  ( iLineNo );	// マッチ行
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
