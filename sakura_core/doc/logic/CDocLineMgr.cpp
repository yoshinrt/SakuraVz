﻿/*!	@file
	@brief 行データの管理

	@author Norio Nakatani
	@date 1998/03/05  新規作成
	@date 2001/06/23 N.Nakatani 単語単位で検索する機能を実装
	@date 2001/06/23 N.Nakatani WhereCurrentWord()変更 WhereCurrentWord_2をコールするようにした
	@date 2005/09/25 D.S.Koba GetSizeOfCharで書き換え
*/
/*
	Copyright (C) 1998-2001, Norio Nakatani
	Copyright (C) 2000, genta, ao
	Copyright (C) 2001, genta, jepro, hor
	Copyright (C) 2002, hor, aroka, MIK, Moca, genta, frozen, Azumaiya, YAZAKI
	Copyright (C) 2003, Moca, ryoji, genta, かろと
	Copyright (C) 2004, genta, Moca
	Copyright (C) 2005, D.S.Koba, ryoji, かろと
	Copyright (C) 2018-2022, Sakura Editor Organization

	This source code is designed for sakura editor.
	Please contact the copyright holder to use this code for other purpose.
*/

#include "StdAfx.h"
// Oct 6, 2000 ao
#include <stdio.h>
#include <io.h>
#include <list>
#include "CDocLineMgr.h"
#include "CDocLine.h"// 2002/2/10 aroka ヘッダー整理
#include "charset/charcode.h"
#include "charset/CCodeFactory.h"
#include "charset/CCodeBase.h"
#include "charset/CCodeMediator.h"
//	Jun. 26, 2001 genta	正規表現ライブラリの差し替え
#include "extmodule/CBregexp.h"
#include "_main/global.h"

//	May 15, 2000 genta
#include "CEol.h"
#include "mem/CMemory.h"// 2002/2/10 aroka

#include "io/CFileLoad.h" // 2002/08/30 Moca
#include "io/CIoBridge.h"
#include "basis/SakuraBasis.h"
#include "parse/CWordParse.h"
#include "util/window.h"
#include "util/file.h"
#include "debug/CRunningTimer.h"

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//               コンストラクタ・デストラクタ                  //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

CDocLineMgr::CDocLineMgr()
{
	_Init();
}

CDocLineMgr::~CDocLineMgr()
{
	DeleteAllLine();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                      行データの管理                         //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

//! pPosの直前に新しい行を挿入
CDocLine* CDocLineMgr::InsertNewLine(CDocLine* pPos)
{
	CDocLine* pcDocLineNew = new CDocLine;
	_InsertBeforePos(pcDocLineNew,pPos);
	return pcDocLineNew;
}

//! 最下部に新しい行を挿入
CDocLine* CDocLineMgr::AddNewLine()
{
	CDocLine* pcDocLineNew = new CDocLine;
	_PushBottom(pcDocLineNew);
	return pcDocLineNew;
}

//! 文字列を指定して最下部に新しい行を挿入
CDocLine* CDocLineMgr::AddNewLine( const wchar_t* pData, int nDataLen ){
	//チェーン適用
	CDocLine* pcDocLine = AddNewLine();
	//インスタンス設定
	pcDocLine->SetDocLineString( pData, nDataLen, false );
	
	return pcDocLine;
}

//! 全ての行を削除する
void CDocLineMgr::DeleteAllLine()
{
	int iMaxThreadNum = std::thread::hardware_concurrency();
	
	std::vector<std::thread>	cThread;
	std::vector<CDocLine *>		pDocLineStart( iMaxThreadNum );
	
	for( int iThreadID = 0; iThreadID < iMaxThreadNum; ++iThreadID ){
		pDocLineStart[ iThreadID ] = GetLine( m_nLines * iThreadID / iMaxThreadNum );
	}
	
	for( int iThreadID = 0; iThreadID < iMaxThreadNum; ++iThreadID ){
		// 各スレッドの開始位置特定
		CLogicInt iStart	= m_nLines *   iThreadID       / iMaxThreadNum;
		CLogicInt iEnd		= m_nLines * ( iThreadID + 1 ) / iMaxThreadNum;
		
		#ifdef _DEBUG
			MYTRACE( L"DeleteAllLine %d: %d - %d / %d\n", iThreadID, iStart, iEnd, m_nLines );
		#endif
		
		// delete 本体
		cThread.emplace_back( std::thread(
			[ &, this, iThreadID, iStart, iEnd, pDocLineStart ]{
				CDocLine* pDocLine = pDocLineStart[ iThreadID ];
				for( int i = iStart; i < iEnd; ++i ){
					CDocLine* pDocLineNext = pDocLine->GetNextLine();
					delete pDocLine;
					pDocLine = pDocLineNext;
				}
			}
		));
	}
	
	// join
	for( int i = 0; i < iMaxThreadNum; ++i ){
		cThread[ i ].join();
	}
	
	_Init();
}

//! 行の削除
void CDocLineMgr::DeleteLine( CDocLine* pcDocLineDel )
{
	//Prev切り離し
	if( pcDocLineDel->GetPrevLine() ){
		pcDocLineDel->GetPrevLine()->m_pNext = pcDocLineDel->GetNextLine();
	}
	else{
		m_pDocLineTop = pcDocLineDel->GetNextLine();
	}

	//Next切り離し
	if( pcDocLineDel->GetNextLine() ){
		pcDocLineDel->m_pNext->m_pPrev = pcDocLineDel->GetPrevLine();
	}
	else{
		m_pDocLineBot = pcDocLineDel->GetPrevLine();
	}
	
	//参照切り離し
	if( m_pCodePrevRefer == pcDocLineDel ){
		m_pCodePrevRefer = pcDocLineDel->GetNextLine();
	}

	//データ削除
	delete pcDocLineDel;

	//行数減算
	m_nLines--;
	if( CLogicInt(0) == m_nLines ){
		// データがなくなった
		_Init();
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                   行データへのアクセス                      //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

/*!
	指定された番号の行へのポインタを返す

	@param nLine [in] 行番号
	@return 行オブジェクトへのポインタ。該当行がない場合はNULL。
*/
const CDocLine* CDocLineMgr::GetLine( CLogicInt nLine ) const
{
	CLogicInt nCounter;
	CDocLine* pDocLine;
	if( CLogicInt(0) == m_nLines ){
		return NULL;
	}
	// 2004.03.28 Moca nLineが負の場合のチェックを追加
	if( CLogicInt(0) > nLine || nLine >= m_nLines ){
		return NULL;
	}
	// 2004.03.28 Moca m_pCodePrevReferより、Top,Botのほうが近い場合は、そちらを利用する
	CLogicInt nPrevToLineNumDiff = t_abs( m_nPrevReferLine - nLine );
	if( m_pCodePrevRefer == NULL
	  || nLine < nPrevToLineNumDiff
	  || m_nLines - nLine < nPrevToLineNumDiff
	){
		if( m_pCodePrevRefer == NULL ){
			MY_RUNNINGTIMER( cRunningTimer, L"CDocLineMgr::GetLine() 	m_pCodePrevRefer == NULL" );
		}

		if( nLine < (m_nLines / 2) ){
			nCounter = CLogicInt(0);
			pDocLine = m_pDocLineTop;
			while( pDocLine ){
				if( nLine == nCounter ){
					m_nPrevReferLine = nLine;
					m_pCodePrevRefer = pDocLine;
					m_pDocLineCurrent = pDocLine->GetNextLine();
					return pDocLine;
				}
				pDocLine = pDocLine->GetNextLine();
				nCounter++;
			}
		}
		else{
			nCounter = m_nLines - CLogicInt(1);
			pDocLine = m_pDocLineBot;
			while( NULL != pDocLine ){
				if( nLine == nCounter ){
					m_nPrevReferLine = nLine;
					m_pCodePrevRefer = pDocLine;
					m_pDocLineCurrent = pDocLine->GetNextLine();
					return pDocLine;
				}
				pDocLine = pDocLine->GetPrevLine();
				nCounter--;
			}
		}
	}
	else{
		if( nLine == m_nPrevReferLine ){
			m_nPrevReferLine = nLine;
			m_pDocLineCurrent = m_pCodePrevRefer->GetNextLine();
			return m_pCodePrevRefer;
		}
		else if( nLine > m_nPrevReferLine ){
			nCounter = m_nPrevReferLine + CLogicInt(1);
			pDocLine = m_pCodePrevRefer->GetNextLine();
			while( NULL != pDocLine ){
				if( nLine == nCounter ){
					m_nPrevReferLine = nLine;
					m_pCodePrevRefer = pDocLine;
					m_pDocLineCurrent = pDocLine->GetNextLine();
					return pDocLine;
				}
				pDocLine = pDocLine->GetNextLine();
				++nCounter;
			}
		}
		else{
			nCounter = m_nPrevReferLine - CLogicInt(1);
			pDocLine = m_pCodePrevRefer->GetPrevLine();
			while( NULL != pDocLine ){
				if( nLine == nCounter ){
					m_nPrevReferLine = nLine;
					m_pCodePrevRefer = pDocLine;
					m_pDocLineCurrent = pDocLine->GetNextLine();
					return pDocLine;
				}
				pDocLine = pDocLine->GetPrevLine();
				nCounter--;
			}
		}
	}
	return NULL;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                         実装補助                            //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

void CDocLineMgr::_Init()
{
	m_pDocLineTop = NULL;
	m_pDocLineBot = NULL;
	m_nLines = CLogicInt(0);
	m_nPrevReferLine = CLogicInt(0);
	m_pCodePrevRefer = NULL;
	m_pDocLineCurrent = NULL;
	CDiffManager::getInstance()->SetDiffUse(false);	/* DIFF使用中 */	//@@@ 2002.05.25 MIK     //##後でCDocListener::OnClear (OnAfterClose) を作成し、そこに移動
}

// -- -- チェーン関数 -- -- // 2007.10.11 kobake 作成
//!最下部に挿入
void CDocLineMgr::_PushBottom(CDocLine* pDocLineNew)
{
	if( !m_pDocLineTop ){
		m_pDocLineTop = pDocLineNew;
	}
	pDocLineNew->m_pPrev = m_pDocLineBot;

	if( m_pDocLineBot ){
		m_pDocLineBot->m_pNext = pDocLineNew;
	}
	m_pDocLineBot = pDocLineNew;
	pDocLineNew->m_pNext = NULL;

	++m_nLines;
}

//!pPosの直前に挿入。pPosにNULLを指定した場合は、最下部に追加。
void CDocLineMgr::_InsertBeforePos(CDocLine* pDocLineNew, CDocLine* pPos)
{
	//New.Nextを設定
	pDocLineNew->m_pNext = pPos;

	//New.Prev, Other.Prevを設定
	if(pPos){
		pDocLineNew->m_pPrev = pPos->GetPrevLine();
		pPos->m_pPrev = pDocLineNew;
	}
	else{
		pDocLineNew->m_pPrev = m_pDocLineBot;
		m_pDocLineBot = pDocLineNew;
	}

	//Other.Nextを設定
	if( pDocLineNew->GetPrevLine() ){
		pDocLineNew->GetPrevLine()->m_pNext = pDocLineNew;
	}
	else{
		m_pDocLineTop = pDocLineNew;
	}

	//行数を加算
	++m_nLines;
}

//! pPosの直後に挿入。pPosにNULLを指定した場合は、先頭に追加。
void CDocLineMgr::_InsertAfterPos(CDocLine* pDocLineNew, CDocLine* pPos)
{
	//New.Prevを設定
	pDocLineNew->m_pPrev = pPos;

	//New.Next, Other.Nextを設定
	if( pPos ){
		pDocLineNew->m_pNext = pPos->GetNextLine();
		pPos->m_pNext = pDocLineNew;
	}
	else{
		pDocLineNew->m_pNext = m_pDocLineTop;
		m_pDocLineTop = pDocLineNew;
	}

	//Other.Prevを設定
	if( pDocLineNew->GetNextLine() ){
		pDocLineNew->m_pNext->m_pPrev = pDocLineNew;
	}
	else{
		m_pDocLineBot = pDocLineNew;
	}

	//行数を加算
	m_nLines++;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                         デバッグ                            //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

/*!	@brief CDocLineMgrDEBUG用

	@date 2004.03.18 Moca
		m_pDocLineCurrentとm_pCodePrevReferがデータチェーンの
		要素を指しているかの検証機能を追加．

*/
void CDocLineMgr::DUMP()
{
#ifdef _DEBUG
	MYTRACE( L"------------------------\n" );

	CDocLine* pDocLine;
	CDocLine* pDocLineNext;
	CDocLine* pDocLineEnd = NULL;
	pDocLine = m_pDocLineTop;

	// 正当性を調べる
	bool bIncludeCurrent = false;
	bool bIncludePrevRefer = false;
	CLogicInt nNum = CLogicInt(0);
	if( m_pDocLineTop->m_pPrev != NULL ){
		MYTRACE( L"error: m_pDocLineTop->m_pPrev != NULL\n");
	}
	if( m_pDocLineBot->m_pNext != NULL ){
		MYTRACE( L"error: m_pDocLineBot->m_pNext != NULL\n" );
	}
	while( NULL != pDocLine ){
		if( m_pDocLineCurrent == pDocLine ){
			bIncludeCurrent = true;
		}
		if( m_pCodePrevRefer == pDocLine ){
			bIncludePrevRefer = true;
		}
		if( NULL != pDocLine->GetNextLine() ){
			if( pDocLine->m_pNext == pDocLine ){
				MYTRACE( L"error: pDocLine->m_pPrev Invalid value.\n" );
				break;
			}
			if( pDocLine->m_pNext->m_pPrev != pDocLine ){
				MYTRACE( L"error: pDocLine->m_pNext->m_pPrev != pDocLine.\n" );
				break;
			}
		}else{
			pDocLineEnd = pDocLine;
		}
		pDocLine = pDocLine->GetNextLine();
		nNum++;
	}
	
	if( pDocLineEnd != m_pDocLineBot ){
		MYTRACE( L"error: pDocLineEnd != m_pDocLineBot" );
	}
	
	if( nNum != m_nLines ){
		MYTRACE( L"error: nNum(%d) != m_nLines(%d)\n", nNum, m_nLines );
	}
	if( false == bIncludeCurrent && m_pDocLineCurrent != NULL ){
		MYTRACE( L"error: m_pDocLineCurrent=%08lxh Invalid value.\n", m_pDocLineCurrent );
	}
	if( false == bIncludePrevRefer && m_pCodePrevRefer != NULL ){
		MYTRACE( L"error: m_pCodePrevRefer =%08lxh Invalid value.\n", m_pCodePrevRefer );
	}

	// DUMP
	MYTRACE( L"m_nLines=%d\n", m_nLines );
	MYTRACE( L"m_pDocLineTop=%08lxh\n", m_pDocLineTop );
	MYTRACE( L"m_pDocLineBot=%08lxh\n", m_pDocLineBot );
	pDocLine = m_pDocLineTop;
	while( NULL != pDocLine ){
		pDocLineNext = pDocLine->GetNextLine();
		MYTRACE( L"\t-------\n" );
		MYTRACE( L"\tthis=%08lxh\n", pDocLine );
		MYTRACE( L"\tpPrev; =%08lxh\n", pDocLine->GetPrevLine() );
		MYTRACE( L"\tpNext; =%08lxh\n", pDocLine->GetNextLine() );

		MYTRACE( L"\tm_enumEOLType =%ls\n", pDocLine->GetEol().GetName() );
		MYTRACE( L"\tm_nEOLLen =%d\n", pDocLine->GetEol().GetLen() );

//		MYTRACE( L"\t[%ls]\n", *(pDocLine->m_pLine) );
		MYTRACE( L"\tpDocLine->m_cLine.GetLength()=[%d]\n", pDocLine->GetLengthWithEOL() );
		MYTRACE( L"\t[%ls]\n", pDocLine->GetPtr() );

		pDocLine = pDocLineNext;
	}
	MYTRACE( L"------------------------\n" );
#endif // _DEBUG
	return;
}

void CDocLineMgr::SetEol( const CEol& cEol, CEol* pcOrgEol, bool bForce ){
	
	CDocLine* Line = GetDocLineTop();
	
	// Doc が空
	if( !Line ){
		if( pcOrgEol ) *pcOrgEol = CEol( EEolType::none );
		return;
	}
	
	// オリジナル EOL 保存
	const CEol& cOrgEol = Line->GetEol();
	if( pcOrgEol ) *pcOrgEol = cOrgEol;
	
	// cEol が invalid or 変換前後が同一 EOL なら return
	if(
		!cEol.IsValid() ||
		!bForce && cEol == cOrgEol
	) return;
	
	for(; Line; Line = Line->GetNextLine()){
		
		// 行単位で，変換前後が同一なら変換しない
		if( Line->GetEol() == cEol ) continue;
		
		Line->SetEol( cEol, nullptr );
	}
}

// CDocLineMgr 同士の連結
// pAppendData 側のデータを this の後ろに連結後，pAppendData はクリアされる
void CDocLineMgr::Cat( CDocLineMgr *pAppendData ){
	
	CDocLine*	pAppendTop;
	
	// pAppendData が空なら何もせず return
	if(
		pAppendData == nullptr ||
		( pAppendTop = pAppendData->GetDocLineTop()) == nullptr
	){
		return;
	}
	
	// this が空なら，top は append の top
	if( !m_pDocLineTop ) m_pDocLineTop = pAppendTop;
	
	pAppendTop->m_pPrev = m_pDocLineBot;
	if( m_pDocLineBot ) m_pDocLineBot->m_pNext = pAppendTop;
	
	m_pDocLineBot = pAppendData->GetDocLineBottom();
	
	m_nLines += pAppendData->GetLineCount();
	
	// append data のクリア (delete 時に行データが削除されないように)
	pAppendData->_Init();
}

