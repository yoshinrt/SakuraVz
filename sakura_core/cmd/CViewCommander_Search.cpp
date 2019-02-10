/*!	@file
@brief CViewCommanderクラスのコマンド(検索系 基本形)関数群

	2012/12/17	CViewCommander.cpp,CViewCommander_New.cppから分離
*/
/*
	Copyright (C) 1998-2001, Norio Nakatani
	Copyright (C) 2000-2001, jepro, genta
	Copyright (C) 2001, hor, YAZAKI
	Copyright (C) 2002, hor, YAZAKI, novice, Azumaiya, Moca
	Copyright (C) 2003, かろと
	Copyright (C) 2004, Moca
	Copyright (C) 2005, かろと, Moca, D.S.Koba
	Copyright (C) 2006, genta, ryoji, かろと, yukihane
	Copyright (C) 2007, ryoji, genta
	Copyright (C) 2009, ryoji, genta
	Copyright (C) 2010, ryoji
	Copyright (C) 2011, Moca

	This source code is designed for sakura editor.
	Please contact the copyright holders to use this code for other purpose.
*/

#include "StdAfx.h"
#include "CViewCommander.h"
#include "CViewCommander_inline.h"

#include "dlg/CDlgCancel.h"// 2002/2/8 hor
#include "CSearchAgent.h"
#include "util/window.h"
#include "util/string_ex2.h"
#include <limits.h>
#include "sakura_rc.h"

/*!
検索(ボックス)コマンド実行.
ツールバーの検索ボックスにフォーカスを移動する.
	@date 2006.06.04 yukihane 新規作成
*/
void CViewCommander::Command_SEARCH_BOX( void )
{
	GetEditWindow()->m_cToolbar.SetFocusSearchBox();
}

/* 検索(単語検索ダイアログ) */
void CViewCommander::Command_SEARCH_DIALOG( int nOption )
{
	/* 現在カーソル位置単語または選択範囲より検索等のキーを取得 */
	CNativeW		cmemCurText;
	m_pCommanderView->GetCurrentTextForSearchDlg( cmemCurText );	// 2006.08.23 ryoji ダイアログ専用関数に変更

	/* 検索文字列を初期化 */
	if( 0 < cmemCurText.GetStringLength() ){
		GetEditWindow()->m_cDlgFind.m_strText = cmemCurText.GetStringPtr();
	}
	
	// 検索条件のみセット && 検索文字列 == "" ならエラー
	else if(( nOption & CDlgFind::SCH_BUTTON_MASK ) == CDlgFind::SCH_NODLG ){
		ErrorBeep();
		return;
	}
	
	// オプション固定の設定
	GetEditWindow()->m_cDlgFind.m_nFixedOption = nOption;
	
	/* 検索ダイアログの表示 */
	if( NULL == GetEditWindow()->m_cDlgFind.GetHwnd() ){
		GetEditWindow()->m_cDlgFind.DoModeless( G_AppInstance(), m_pCommanderView->GetHwnd(), (LPARAM)&GetEditWindow()->GetActiveView() );
	}
	else{
		/* アクティブにする */
		ActivateFrameWindow( GetEditWindow()->m_cDlgFind.GetHwnd() );
		::DlgItem_SetText( GetEditWindow()->m_cDlgFind.GetHwnd(), IDC_COMBO_TEXT, cmemCurText.GetStringT() );
	}
	return;
}

/*! 次を検索
	@param bChangeCurRegexp 共有データの検索文字列を使う
	@date 2003.05.22 かろと 無限マッチ対策．行頭・行末処理見直し．
	@date 2004.05.30 Moca bChangeCurRegexp=trueで従来通り。falseで、CEditViewの現在設定されている検索パターンを使う
*/
void CViewCommander::Command_SEARCH_NEXT(
	HWND			hwndParent,
	const WCHAR*	pszNotFoundMessage,
	UINT			uOption,
	CLogicRange*	pcSelectLogic		//!< [out] 選択範囲のロジック版。マッチ範囲を返す。すべて置換/高速モードで使用
)
{
	bool		bSelecting;
	bool		bFlag1 = false;
	bool		bSelectingLock_Old = false;
	bool		bFound = false;
	bool		bDisableSelect = false;
	bool		b0Match = false;		//!< 長さ０でマッチしているか？フラグ by かろと
	CLogicInt	nIdx(0);
	CLayoutInt	nLineNum(0);

	CLayoutRange	sRangeA;
	sRangeA.Set(GetCaret().GetCaretLayoutPos());

	CLayoutRange	sSelectBgn_Old;
	CLayoutRange	sSelect_Old;
	CLayoutInt	nLineNumOld(0);

	// bFastMode
	CLogicInt nLineNumLogic(0);

	bool		bRedo = false;	//	hor
	int			nIdxOld = 0;	//	hor
	int			nSearchResult;

	if( pcSelectLogic ){
		pcSelectLogic->Clear(-1);
	}

	bSelecting = false;
	// 2002.01.16 hor
	// 共通部分のくくりだし
	// 2004.05.30 Moca CEditViewの現在設定されている検索パターンを使えるように
	if(( uOption & CMDSCH_CHANGE_RE ) && !m_pCommanderView->ChangeCurRegexp()) return;
	if( 0 == m_pCommanderView->m_strCurSearchKey.size() ){
		goto end_of_func;
	}
	
	// 検索開始位置を調整
	bFlag1 = false;
	if( NULL == pcSelectLogic && m_pCommanderView->GetSelectionInfo().IsTextSelected() ){	/* テキストが選択されているか */
		/* 矩形範囲選択中でない & 選択状態のロック */
		if( !m_pCommanderView->GetSelectionInfo().IsBoxSelecting() && m_pCommanderView->GetSelectionInfo().m_bSelectingLock ){
			bSelecting = true;
			bSelectingLock_Old = m_pCommanderView->GetSelectionInfo().m_bSelectingLock;

			sSelectBgn_Old = m_pCommanderView->GetSelectionInfo().m_sSelectBgn; //範囲選択(原点)
			sSelect_Old = GetSelect();

			if( PointCompare(m_pCommanderView->GetSelectionInfo().m_sSelectBgn.GetFrom(),GetCaret().GetCaretLayoutPos()) >= 0 ){
				// カーソル移動
				GetCaret().SetCaretLayoutPos(GetSelect().GetFrom());
				if (GetSelect().IsOne()) {
					// 現在、長さ０でマッチしている場合は１文字進める(無限マッチ対策) by かろと
					b0Match = true;
				}
				bFlag1 = true;
			}
			else{
				// カーソル移動
				GetCaret().SetCaretLayoutPos(GetSelect().GetTo());
				if (GetSelect().IsOne()) {
					// 現在、長さ０でマッチしている場合は１文字進める(無限マッチ対策) by かろと
					b0Match = true;
				}
			}
		}
		else{
			/* カーソル移動 */
			GetCaret().SetCaretLayoutPos(GetSelect().GetTo());
			if (GetSelect().IsOne()) {
				// 現在、長さ０でマッチしている場合は１文字進める(無限マッチ対策) by かろと
				b0Match = true;
			}

			/* 現在の選択範囲を非選択状態に戻す */
			m_pCommanderView->GetSelectionInfo().DisableSelectArea( uOption & CMDSCH_REDRAW, false );
			bDisableSelect = true;
		}
	}
	if( NULL == pcSelectLogic ){
		nLineNum = GetCaret().GetCaretLayoutPos().GetY2();
		CLogicInt nLineLen = CLogicInt(0); // 2004.03.17 Moca NULL == pLineのとき、nLineLenが未設定になり落ちるバグ対策
		const CLayout*	pcLayout;
		const wchar_t*	pLine = GetDocument()->m_cLayoutMgr.GetLineStr(nLineNum, &nLineLen, &pcLayout);

		/* 指定された桁に対応する行のデータ内の位置を調べる */
// 2002.02.08 hor EOFのみの行からも次検索しても再検索可能に (2/2)
		nIdx = pcLayout ? m_pCommanderView->LineColumnToIndex( pcLayout, GetCaret().GetCaretLayoutPos().GetX2() ) : CLogicInt(0);
		if( b0Match ) {
			// 現在、長さ０でマッチしている場合は物理行で１文字進める(無限マッチ対策)
			if( nIdx < nLineLen ) {
				// 2005-09-02 D.S.Koba GetSizeOfChar
				nIdx += CLogicInt(CNativeW::GetSizeOfChar(pLine, nLineLen, nIdx) == 2 ? 2 : 1);
			} else {
				// 念のため行末は別処理
				++nIdx;
			}
		}
	}else{
		nLineNumLogic = GetCaret().GetCaretLogicPos().GetY2();
		nIdx = GetCaret().GetCaretLogicPos().GetX2();
	}

	nLineNumOld = nLineNum;	//	hor
	bRedo		= true;		//	hor
	nIdxOld		= nIdx;		//	hor
	
re_do:;
	/* 現在位置より後ろの位置を検索する */
	// 2004.05.30 Moca 引数をGetShareData()からメンバ変数に変更。他のプロセス/スレッドに書き換えられてしまわないように。
	if( NULL == pcSelectLogic ){
		nSearchResult = GetDocument()->m_cLayoutMgr.SearchWord(
			nLineNum,						// 検索開始レイアウト行
			nIdx,							// 検索開始データ位置
			SEARCH_FORWARD,					// 前方検索
			&sRangeA,						// マッチレイアウト範囲
			m_pCommanderView->m_sSearchPattern
		);
	}else{
		nSearchResult = CSearchAgent(&GetDocument()->m_cDocLineMgr).SearchWord(
			CLogicPoint(nIdx, nLineNumLogic),
			SEARCH_FORWARD,					// 前方検索
			pcSelectLogic,
			m_pCommanderView->m_sSearchPattern
		);
	}
	
	if( nSearchResult ){
		// 指定された行のデータ内の位置に対応する桁の位置を調べる
		if( bFlag1 && sRangeA.GetFrom()==GetCaret().GetCaretLayoutPos() ){
			CLogicRange sRange_Logic;
			GetDocument()->m_cLayoutMgr.LayoutToLogic(sRangeA,&sRange_Logic);

			nLineNum = sRangeA.GetTo().GetY2();
			nIdx     = sRange_Logic.GetTo().GetX2();
			if( sRange_Logic.GetFrom() == sRange_Logic.GetTo() ) { // 幅0マッチでの無限ループ対策。
				nIdx += 1; // wchar_t一個分進めるだけでは足りないかもしれないが。
			}
			goto re_do;
		}

		if( bSelecting ){
			/* 現在のカーソル位置によって選択範囲を変更 */
			m_pCommanderView->GetSelectionInfo().ChangeSelectAreaByCurrentCursor( sRangeA.GetTo() );
			m_pCommanderView->GetSelectionInfo().m_bSelectingLock = bSelectingLock_Old;	/* 選択状態のロック */
		}else if( NULL == pcSelectLogic ){
			/* 選択範囲の変更 */
			//	2005.06.24 Moca
			m_pCommanderView->GetSelectionInfo().SetSelectArea( sRangeA );

			if( uOption & CMDSCH_REDRAW ){
				/* 選択領域描画 */
				m_pCommanderView->GetSelectionInfo().DrawSelectArea();
			}
		}

		/* カーソル移動 */
		//	Sep. 8, 2000 genta
		if ( !( uOption & CMDSCH_REPLACEALL )) m_pCommanderView->AddCurrentLineToHistory();	// 2002.02.16 hor すべて置換のときは不要
		if( NULL == pcSelectLogic ){
			GetCaret().MoveCursor( sRangeA.GetFrom(), uOption & CMDSCH_REDRAW, _CARETMARGINRATE_JUMP );
			GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();
		}else{
			GetCaret().MoveCursorFastMode( pcSelectLogic->GetFrom() );
		}
		bFound = TRUE;
	}
	else{
		if( bSelecting ){
			m_pCommanderView->GetSelectionInfo().m_bSelectingLock = bSelectingLock_Old;	/* 選択状態のロック */

			/* 選択範囲の変更 */
			m_pCommanderView->GetSelectionInfo().m_sSelectBgn = sSelectBgn_Old; //範囲選択(原点)
			m_pCommanderView->GetSelectionInfo().m_sSelectOld = sSelect_Old;	// 2011.12.24
			GetSelect().SetFrom(sSelect_Old.GetFrom());
			GetSelect().SetTo(sRangeA.GetFrom());

			/* カーソル移動 */
			GetCaret().MoveCursor( sRangeA.GetFrom(), uOption & CMDSCH_REDRAW, _CARETMARGINRATE_JUMP );
			GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();

			if( uOption & CMDSCH_REDRAW ){
				/* 選択領域描画 */
				m_pCommanderView->GetSelectionInfo().DrawSelectArea();
			}
		}else{
			if( bDisableSelect ){
				// 2011.12.21 ロジックカーソル位置の修正/カーソル線・対括弧の表示
				CLogicPoint ptLogic;
				GetDocument()->m_cLayoutMgr.LayoutToLogic(GetCaret().GetCaretLayoutPos(), &ptLogic);
				GetCaret().SetCaretLogicPos(ptLogic);
				m_pCommanderView->DrawBracketCursorLine( uOption & CMDSCH_REDRAW );
			}
		}
	}

end_of_func:;
// From Here 2002.01.26 hor 先頭（末尾）から再検索
	if(GetDllShareData().m_Common.m_sSearch.m_bSearchAll){
		if(!bFound	&&		// 見つからなかった
			bRedo	&&		// 最初の検索
			!( uOption & CMDSCH_REPLACEALL )	// 全て置換の実行中じゃない
		){
			nLineNum	= CLayoutInt(0);
			nIdx		= CLogicInt(0);
			bRedo		= false;
			goto re_do;		// 先頭から再検索
		}
	}

	if(bFound){
		if(NULL == pcSelectLogic && ((nLineNumOld > nLineNum)||(nLineNumOld == nLineNum && nIdxOld > nIdx)))
			m_pCommanderView->SendStatusMessage(LS(STR_ERR_SRNEXT1));
	}
	else{
		GetCaret().ShowEditCaret();	// 2002/04/18 YAZAKI
		GetCaret().ShowCaretPosInfo();	// 2002/04/18 YAZAKI
		if( !( uOption & CMDSCH_REPLACEALL )){
			m_pCommanderView->SendStatusMessage(LS(STR_ERR_SRNEXT2));
		}
// To Here 2002.01.26 hor

		/* 検索／置換  見つからないときメッセージを表示 */
		if( NULL == pszNotFoundMessage ){
			CNativeW KeyName;
			LimitStringLengthW(m_pCommanderView->m_strCurSearchKey.c_str(), m_pCommanderView->m_strCurSearchKey.size(),
				_MAX_PATH, KeyName);
			if( (size_t)KeyName.GetStringLength() < m_pCommanderView->m_strCurSearchKey.size() ){
				KeyName.AppendString( L"..." );
			}
			AlertNotFound(
				hwndParent,
				uOption & CMDSCH_REPLACEALL,
				LS(STR_ERR_SRNEXT3),
				KeyName.GetStringPtr()
			);
		}
		else{
			AlertNotFound(hwndParent, uOption & CMDSCH_REPLACEALL, _T("%ls"), pszNotFoundMessage);
		}
	}
}

/* 前を検索 */
void CViewCommander::Command_SEARCH_PREV( bool bReDraw, HWND hwndParent )
{
	bool		bSelecting;
	bool		bSelectingLock_Old = false;
	bool		bFound = false;
	bool		bRedo = false;			//	hor
	bool		bDisableSelect = false;
	CLayoutInt	nLineNumOld(0);
	CLogicInt	nIdxOld(0);
	const CLayout* pcLayout = NULL;
	CLayoutInt	nLineNum(0);
	CLogicInt	nIdx(0);

	CLayoutRange sRangeA;
	sRangeA.Set(GetCaret().GetCaretLayoutPos());

	CLayoutRange sSelectBgn_Old;
	CLayoutRange sSelect_Old;

	bSelecting = false;
	// 2002.01.16 hor
	// 共通部分のくくりだし
	if(!m_pCommanderView->ChangeCurRegexp()){
		return;
	}
	if( 0 == m_pCommanderView->m_strCurSearchKey.size() ){
		goto end_of_func;
	}
	if( m_pCommanderView->GetSelectionInfo().IsTextSelected() ){	/* テキストが選択されているか */
		sSelectBgn_Old = m_pCommanderView->GetSelectionInfo().m_sSelectBgn; //範囲選択(原点)
		sSelect_Old = GetSelect();
		
		bSelectingLock_Old = m_pCommanderView->GetSelectionInfo().m_bSelectingLock;

		/* 矩形範囲選択中か */
		if( !m_pCommanderView->GetSelectionInfo().IsBoxSelecting() && m_pCommanderView->GetSelectionInfo().m_bSelectingLock ){	/* 選択状態のロック */
			bSelecting = true;
		}
		else{
			/* 現在の選択範囲を非選択状態に戻す */
			m_pCommanderView->GetSelectionInfo().DisableSelectArea( bReDraw, false );
			bDisableSelect = true;
		}
	}

	nLineNum = GetCaret().GetCaretLayoutPos().GetY2();
	pcLayout = GetDocument()->m_cLayoutMgr.SearchLineByLayoutY( nLineNum );

	if( NULL == pcLayout ){
		// pcLayoutはNULLとなるのは、[EOF]から前検索した場合
		// １行前に移動する処理
		nLineNum--;
		if( nLineNum < 0 ){
			goto end_of_func;
		}
		pcLayout = GetDocument()->m_cLayoutMgr.SearchLineByLayoutY( nLineNum );
		if( NULL == pcLayout ){
			goto end_of_func;
		}
		// カーソル左移動はやめて nIdxは行の長さとしないと[EOF]から改行を前検索した時に最後の改行を検索できない 2003.05.04 かろと
		const CLayout* pCLayout = GetDocument()->m_cLayoutMgr.SearchLineByLayoutY( nLineNum );
		nIdx = CLogicInt(pCLayout->GetDocLineRef()->GetLengthWithEOL() + 1);		// 行末のヌル文字(\0)にマッチさせるために+1 2003.05.16 かろと
	} else {
		/* 指定された桁に対応する行のデータ内の位置を調べる */
		nIdx = m_pCommanderView->LineColumnToIndex( pcLayout, GetCaret().GetCaretLayoutPos().GetX2() );
	}

	bRedo		=	true;		//	hor
	nLineNumOld	=	nLineNum;	//	hor
	nIdxOld		=	nIdx;		//	hor
re_do:;							//	hor
	/* 現在位置より前の位置を検索する */
	if( GetDocument()->m_cLayoutMgr.SearchWord(
		nLineNum,								// 検索開始レイアウト行
		nIdx,									// 検索開始データ位置
		( ESearchDirection )( SEARCH_BACKWARD | SEARCH_PARTIAL ),
												// 後方検索
		&sRangeA,								// マッチレイアウト範囲
		m_pCommanderView->m_sSearchPattern
	) ){
		if( bSelecting ){
			/* 現在のカーソル位置によって選択範囲を変更 */
			m_pCommanderView->GetSelectionInfo().ChangeSelectAreaByCurrentCursor( sRangeA.GetFrom() );
			m_pCommanderView->GetSelectionInfo().m_bSelectingLock = bSelectingLock_Old;	/* 選択状態のロック */
		}else{
			/* 選択範囲の変更 */
			//	2005.06.24 Moca
			m_pCommanderView->GetSelectionInfo().SetSelectArea( sRangeA );

			if( bReDraw ){
				/* 選択領域描画 */
				m_pCommanderView->GetSelectionInfo().DrawSelectArea();
			}
		}
		/* カーソル移動 */
		//	Sep. 8, 2000 genta
		m_pCommanderView->AddCurrentLineToHistory();
		GetCaret().MoveCursor( sRangeA.GetFrom(), bReDraw, _CARETMARGINRATE_JUMP );
		GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();
		bFound = TRUE;
	}else{
		if( bSelecting ){
			m_pCommanderView->GetSelectionInfo().m_bSelectingLock = bSelectingLock_Old;	/* 選択状態のロック */
			/* 選択範囲の変更 */
			m_pCommanderView->GetSelectionInfo().m_sSelectBgn = sSelectBgn_Old;
			GetSelect() = sSelect_Old;

			/* カーソル移動 */
			GetCaret().MoveCursor( sRangeA.GetFrom(), bReDraw, _CARETMARGINRATE_JUMP );
			GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();
			/* 選択領域描画 */
			m_pCommanderView->GetSelectionInfo().DrawSelectArea();
		}else{
			if( bDisableSelect ){
				m_pCommanderView->DrawBracketCursorLine(bReDraw);
			}
		}
	}
end_of_func:;
// From Here 2002.01.26 hor 先頭（末尾）から再検索
	if(GetDllShareData().m_Common.m_sSearch.m_bSearchAll){
		if(!bFound	&&	// 見つからなかった
			bRedo		// 最初の検索
		){
			nLineNum	= GetDocument()->m_cLayoutMgr.GetLineCount()-CLayoutInt(1);
			nIdx		= CLogicInt(MAXLINEKETAS); // ロジック折り返し < レイアウト折り返しという前提
			bRedo		= false;
			goto re_do;	// 末尾から再検索
		}
	}
	if(bFound){
		if((nLineNumOld < nLineNum)||(nLineNumOld == nLineNum && nIdxOld < nIdx))
			m_pCommanderView->SendStatusMessage(LS(STR_ERR_SRPREV1));
	}else{
		m_pCommanderView->SendStatusMessage(LS(STR_ERR_SRPREV2));
// To Here 2002.01.26 hor

		/* 検索／置換  見つからないときメッセージを表示 */
		CNativeW KeyName;
		LimitStringLengthW(m_pCommanderView->m_strCurSearchKey.c_str(), m_pCommanderView->m_strCurSearchKey.size(),
			_MAX_PATH, KeyName);
		if( (size_t)KeyName.GetStringLength() < m_pCommanderView->m_strCurSearchKey.size() ){
			KeyName.AppendString( L"..." );
		}
		AlertNotFound(
			hwndParent,
			false,
			LS(STR_ERR_SRPREV3),	//Jan. 25, 2001 jepro メッセージを若干変更
			KeyName.GetStringPtr()
		);
	}
	return;
}

//置換(置換ダイアログ)
void CViewCommander::Command_REPLACE_DIALOG( int nOption )
{
	BOOL		bSelected = FALSE;

	/* 現在カーソル位置単語または選択範囲より検索等のキーを取得 */
	CNativeW	cmemCurText;
	m_pCommanderView->GetCurrentTextForSearchDlg( cmemCurText );	// 2006.08.23 ryoji ダイアログ専用関数に変更

	/* 検索文字列を初期化 */
	if( 0 < cmemCurText.GetStringLength() ){
		GetEditWindow()->m_cDlgReplace.m_strText = cmemCurText.GetStringPtr();
	}
	if( 0 < GetDllShareData().m_sSearchKeywords.m_aReplaceKeys.size() ){
		if( GetEditWindow()->m_cDlgReplace.m_nReplaceKeySequence < GetDllShareData().m_Common.m_sSearch.m_nReplaceKeySequence ){
			GetEditWindow()->m_cDlgReplace.m_strText2 = GetDllShareData().m_sSearchKeywords.m_aReplaceKeys[0];	// 2006.08.23 ryoji 前回の置換後文字列を引き継ぐ
		}
	}
	
	if ( m_pCommanderView->GetSelectionInfo().IsTextSelected() && !GetSelect().IsLineOne() ) {
		bSelected = TRUE;	//選択範囲をチェックしてダイアログ表示
	}else{
		bSelected = FALSE;	//ファイル全体をチェックしてダイアログ表示
	}
	/* 置換オプションの初期化 */
	GetEditWindow()->m_cDlgReplace.m_nReplaceTarget=0;	/* 置換対象 */
	GetEditWindow()->m_cDlgReplace.m_nPaste=FALSE;		/* 貼り付ける？ */
// To Here 2001.12.03 hor

	// オプション固定の設定
	GetEditWindow()->m_cDlgReplace.m_nFixedOption = nOption;

	/* 置換ダイアログの表示 */
	//	From Here Jul. 2, 2001 genta 置換ウィンドウの2重開きを抑止
	if( !::IsWindow( GetEditWindow()->m_cDlgReplace.GetHwnd() ) ){
		GetEditWindow()->m_cDlgReplace.DoModeless( G_AppInstance(), m_pCommanderView->GetHwnd(), (LPARAM)m_pCommanderView, bSelected );
	}
	else {
		/* アクティブにする */
		ActivateFrameWindow( GetEditWindow()->m_cDlgReplace.GetHwnd() );
		::DlgItem_SetText( GetEditWindow()->m_cDlgReplace.GetHwnd(), IDC_COMBO_TEXT, cmemCurText.GetStringT() );
	}
	//	To Here Jul. 2, 2001 genta 置換ウィンドウの2重開きを抑止
	return;
}

/*! 置換実行
	
	@date 2002/04/08 親ウィンドウを指定するように変更。
	@date 2003.05.17 かろと 長さ０マッチの無限置換回避など
	@date 2011.12.18 Moca オプション・検索キーをDllShareDataからm_cDlgReplace/EditViewベースに変更。文字列長制限の撤廃
*/
void CViewCommander::Command_REPLACE( HWND hwndParent )
{
	// m_sSearchOption選択のための先に適用
	if( !m_pCommanderView->ChangeCurRegexp(false) ){
		return;
	}

	if ( hwndParent == NULL ){	//	親ウィンドウが指定されていなければ、CEditViewが親。
		hwndParent = m_pCommanderView->GetHwnd();
	}
	//2002.02.10 hor
	int nPaste			=	GetEditWindow()->m_cDlgReplace.m_nPaste;

	// From Here 2001.12.03 hor
	if( nPaste && !GetDocument()->m_cDocEditor.IsEnablePaste()){
		OkMessage( hwndParent, LS(STR_ERR_CEDITVIEW_CMD10) );
		::CheckDlgButton( GetEditWindow()->m_cDlgReplace.GetHwnd(), IDC_CHK_PASTE, FALSE );
		::EnableWindow( ::GetDlgItem( GetEditWindow()->m_cDlgReplace.GetHwnd(), IDC_COMBO_TEXT2 ), TRUE );
		return;	//	失敗return;
	}

	// 2002.01.09 hor
	// 選択エリアがあれば、その先頭にカーソルを移す
	if( m_pCommanderView->GetSelectionInfo().IsTextSelected() ){
		if( m_pCommanderView->GetSelectionInfo().IsBoxSelecting() ){
			GetCaret().MoveCursor( GetSelect().GetFrom(), true );
		} else {
			Command_LEFT( false, false );
		}
	}
	// To Here 2002.01.09 hor
	
	// 矩形選択？
//			bBeginBoxSelect = m_pCommanderView->GetSelectionInfo().IsBoxSelecting();

	/* カーソル左移動 */
	//HandleCommand( F_LEFT, true, 0, 0, 0, 0 );	//？？？
	// To Here 2001.12.03 hor

	/* テキスト選択解除 */
	/* 現在の選択範囲を非選択状態に戻す */
	m_pCommanderView->GetSelectionInfo().DisableSelectArea( true );

	// 2004.06.01 Moca 検索中に、他のプロセスによってm_aReplaceKeysが書き換えられても大丈夫なように
	const CNativeW	cMemRepKey( GetEditWindow()->m_cDlgReplace.m_strText2.c_str() );

	BOOL	bRegularExp = m_pCommanderView->m_sCurSearchOption.bRegularExp;
	
	/* 次を検索 */
	Command_SEARCH_NEXT( hwndParent, NULL, CMDSCH_CHANGE_RE, nullptr );

	/* テキストが選択されているか */
	if( m_pCommanderView->GetSelectionInfo().IsTextSelected() ){
		// From Here 2001.12.03 hor
		/* コマンドコードによる処理振り分け */
		/* テキストを貼り付け */
		if(nPaste){
			Command_PASTE( 0x40/*COPYPASTE*/ );
		} else if ( bRegularExp ) { /* 検索／置換  1==正規表現 */
			CBregexp *pRegexp = m_pCommanderView->m_sSearchPattern.GetRegexp();
			
			int iRet = pRegexp->Replace( cMemRepKey.GetStringPtr());
			if( iRet > 0 ){
				Command_INSTEXT( false, pRegexp->GetString(), pRegexp->GetStringLen(), TRUE );
				
				// マッチ幅が 0 の場合，1文字選進む
				if( pRegexp->GetMatchLen() == 0 ){
					CLogicPoint Caret = GetCaret().GetCaretLogicPos();
					CDocLine *pDocLine = GetDocument()->m_cLayoutMgr.m_pcDocLineMgr->GetLine( Caret.GetY());
					
					if( Caret.GetX() < pDocLine->GetLengthWithoutEOL()){
						// hit 位置が改行より前なら，一文字右
						Caret.SetX( Caret.GetX() + 1 );
						Caret.SetY( Caret.GetY());
					}else if( pDocLine->GetNextLine()){
						// 次行があれば，次行先頭
						Caret.SetX( CLogicInt( 0 ));
						Caret.SetY( Caret.GetY() + 1 );
					}
					CLayoutPoint CaretLay;
					GetDocument()->m_cLayoutMgr.LogicToLayout( Caret, &CaretLay );
					GetCaret().MoveCursor( CaretLay, false );
				}
			}else if( iRet < 0 ){
				pRegexp->ShowErrorMsg( hwndParent );
			}
		}else{
			Command_INSTEXT( false, cMemRepKey.GetStringPtr(), cMemRepKey.GetStringLength(), TRUE );
		}

		// To Here 2001.12.03 hor
		/* 最後まで置換した時にOK押すまで置換前の状態が表示されるので、
		** 置換後、次を検索する前に書き直す 2003.05.17 かろと
		*/
		m_pCommanderView->Redraw();

		/* 次を検索 */
		Command_SEARCH_NEXT( hwndParent, LSW(STR_ERR_CEDITVIEW_CMD11), CMDSCH_CHANGE_RE | CMDSCH_REDRAW );
	}
}

/*! すべて置換実行 */
void CViewCommander::Command_REPLACE_ALL()
{
	// m_sSearchOption選択のための先に適用
	if( !m_pCommanderView->ChangeCurRegexp() ){
		return;
	}

	//2002.02.10 hor
	BOOL nPaste			= GetEditWindow()->m_cDlgReplace.m_nPaste;
	BOOL bRegularExp	= m_pCommanderView->m_sCurSearchOption.bRegularExp;
	BOOL bSelectedArea	= GetEditWindow()->m_cDlgReplace.m_bSelectedArea;
	
	GetEditWindow()->m_cDlgReplace.m_bCanceled=false;
	GetEditWindow()->m_cDlgReplace.m_nReplaceCnt=0;

	// From Here 2001.12.03 hor
	if( nPaste && !GetDocument()->m_cDocEditor.IsEnablePaste() ){
		OkMessage( m_pCommanderView->GetHwnd(), LS(STR_ERR_CEDITVIEW_CMD10) );
		::CheckDlgButton( GetEditWindow()->m_cDlgReplace.GetHwnd(), IDC_CHK_PASTE, FALSE );
		::EnableWindow( ::GetDlgItem( GetEditWindow()->m_cDlgReplace.GetHwnd(), IDC_COMBO_TEXT2 ), TRUE );
		return;	// TRUE;
	}
	// To Here 2001.12.03 hor

	bool		bBeginBoxSelect; // 矩形選択？
	if(m_pCommanderView->GetSelectionInfo().IsTextSelected()){
		bBeginBoxSelect=m_pCommanderView->GetSelectionInfo().IsBoxSelecting();
	}
	else{
		bSelectedArea=FALSE;
		bBeginBoxSelect=false;
	}

	/* 表示処理ON/OFF */
	const bool bDrawSwitchOld = m_pCommanderView->SetDrawSwitch( false );
	
	// 画面上端位置保存
	CLayoutInt ViewTop;
	if( !bSelectedArea ){
		ViewTop = m_pCommanderView->GetTextArea().GetViewTopLine();
		Command_JUMPHIST_SET();
	}
	
	int	nAllLineNum; // $$単位混在
	if( !bBeginBoxSelect ){
		nAllLineNum = (Int)GetDocument()->m_cDocLineMgr.GetLineCount();
	}else{
		nAllLineNum = (Int)GetDocument()->m_cLayoutMgr.GetLineCount();
	}
	int	nAllLineNumOrg = nAllLineNum;
	int	nAllLineNumLogicOrg = (Int)GetDocument()->m_cDocLineMgr.GetLineCount();

	/* 進捗表示&中止ダイアログの作成 */
	CDlgCancel	cDlgCancel;
	HWND		hwndCancel = cDlgCancel.DoModeless( G_AppInstance(), m_pCommanderView->GetHwnd(), IDD_REPLACERUNNING );
	::EnableWindow( m_pCommanderView->GetHwnd(), FALSE );
	::EnableWindow( ::GetParent( m_pCommanderView->GetHwnd() ), FALSE );
	::EnableWindow( ::GetParent( ::GetParent( m_pCommanderView->GetHwnd() ) ), FALSE );
	//<< 2002/03/26 Azumaiya
	// 割り算掛け算をせずに進歩状況を表せるように、シフト演算をする。
	int nShiftCount;
	for ( nShiftCount = 0; 300 < nAllLineNum; nShiftCount++ )
	{
		nAllLineNum/=2;
	}
	//>> 2002/03/26 Azumaiya

	/* プログレスバー初期化 */
	HWND		hwndProgress = ::GetDlgItem( hwndCancel, IDC_PROGRESS_REPLACE );
	Progress_SetRange( hwndProgress, 0, nAllLineNum + 1 );
	int			nNewPos = 0;
	int			nOldPos = -1;
	Progress_SetPos( hwndProgress, nNewPos);

	/* 置換個数初期化 */
	int			nReplaceNum = 0;
	HWND		hwndStatic = ::GetDlgItem( hwndCancel, IDC_STATIC_KENSUU );
	TCHAR szLabel[64];
	_itot( nReplaceNum, szLabel, 10 );
	::SendMessage( hwndStatic, WM_SETTEXT, 0, (LPARAM)szLabel );

	CLayoutRange sRangeA;	//選択範囲
	CLogicPoint ptColLineP;

	// From Here 2001.12.03 hor
	if (bSelectedArea){
		/* 選択範囲置換 */
		/* 選択範囲開始位置の取得 */
		sRangeA = GetSelect();

		//	From Here 2007.09.20 genta 矩形範囲の選択置換ができない
		//	左下～右上と選択した場合，m_nSelectColumnTo < m_nSelectColumnFrom となるが，
		//	範囲チェックで colFrom < colTo を仮定しているので，
		//	矩形選択の場合は左上～右下指定になるよう桁を入れ換える．
		if( bBeginBoxSelect && sRangeA.GetTo().x < sRangeA.GetFrom().x )
			std::swap(sRangeA.GetFromPointer()->x,sRangeA.GetToPointer()->x);
		//	To Here 2007.09.20 genta 矩形範囲の選択置換ができない

		GetDocument()->m_cLayoutMgr.LayoutToLogic(
			sRangeA.GetTo(),
			&ptColLineP
		);
		//選択範囲開始位置へ移動
		GetCaret().MoveCursor( sRangeA.GetFrom(), false );
	}
	else{
		/* ファイル全体置換 */
		/* ファイルの先頭に移動 */
		Command_GOFILETOP( false );
	}

	/* テキスト選択解除 */
	/* 現在の選択範囲を非選択状態に戻す */
	m_pCommanderView->GetSelectionInfo().DisableSelectArea( false );
	// To Here 2001.12.03 hor

	//<< 2002/03/26 Azumaiya
	// 速く動かすことを最優先に組んでみました。
	// ループの外で文字列の長さを特定できるので、一時変数化。
	const wchar_t *szREPLACEKEY;		// 置換後文字列。
	bool		bColumnSelect = false;	// 矩形貼り付けを行うかどうか。
	bool		bLineSelect = false;	// ラインモード貼り付けを行うかどうか
	CNativeW	cmemReplacement;				// 置換後文字列のデータ（データを格納するだけで、ループ内ではこの形ではデータを扱いません）。

	// クリップボードからのデータ貼り付けかどうか。
	if( nPaste != 0 )
	{
		// クリップボードからデータを取得。
		if ( !m_pCommanderView->MyGetClipboardData( cmemReplacement, &bColumnSelect, GetDllShareData().m_Common.m_sEdit.m_bEnableLineModePaste? &bLineSelect: NULL ) )
		{
			ErrorBeep();
			m_pCommanderView->SetDrawSwitch(bDrawSwitchOld);

			::EnableWindow( m_pCommanderView->GetHwnd(), TRUE );
			::EnableWindow( ::GetParent( m_pCommanderView->GetHwnd() ), TRUE );
			::EnableWindow( ::GetParent( ::GetParent( m_pCommanderView->GetHwnd() ) ), TRUE );
			return;
		}

		// 矩形貼り付けが許可されていて、クリップボードのデータが矩形選択のとき。
		if ( GetDllShareData().m_Common.m_sEdit.m_bAutoColumnPaste && bColumnSelect )
		{
			// マウスによる範囲選択中
			if( m_pCommanderView->GetSelectionInfo().IsMouseSelecting() )
			{
				ErrorBeep();
				m_pCommanderView->SetDrawSwitch(bDrawSwitchOld);
				::EnableWindow( m_pCommanderView->GetHwnd(), TRUE );
				::EnableWindow( ::GetParent( m_pCommanderView->GetHwnd() ), TRUE );
				::EnableWindow( ::GetParent( ::GetParent( m_pCommanderView->GetHwnd() ) ), TRUE );
				return;
			}

			// 現在のフォントは固定幅フォントである
			if( !GetDllShareData().m_Common.m_sView.m_bFontIs_FIXED_PITCH )
			{
				m_pCommanderView->SetDrawSwitch(bDrawSwitchOld);
				::EnableWindow( m_pCommanderView->GetHwnd(), TRUE );
				::EnableWindow( ::GetParent( m_pCommanderView->GetHwnd() ), TRUE );
				::EnableWindow( ::GetParent( ::GetParent( m_pCommanderView->GetHwnd() ) ), TRUE );
				return;
			}
		}
		else
		// クリップボードからのデータは普通に扱う。
		{
			bColumnSelect = false;
		}
	}
	else
	{
		// 2004.05.14 Moca 全置換の途中で他のウィンドウで置換されるとまずいのでコピーする
		cmemReplacement.SetString( GetEditWindow()->m_cDlgReplace.m_strText2.c_str() );
	}

	CLogicInt nREPLACEKEY;			// 置換後文字列の長さ。
	szREPLACEKEY = cmemReplacement.GetStringPtr(&nREPLACEKEY);

	// 行コピー（MSDEVLineSelect形式）のテキストで末尾が改行になっていなければ改行を追加する
	// ※レイアウト折り返しの行コピーだった場合は末尾が改行になっていない
	if( bLineSelect ){
		if( !WCODE::IsLineDelimiter(szREPLACEKEY[nREPLACEKEY - 1], GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol) ){
			cmemReplacement.AppendString(GetDocument()->m_cDocEditor.GetNewLineCode().GetValue2());
			szREPLACEKEY = cmemReplacement.GetStringPtr( &nREPLACEKEY );
		}
	}

	if( GetDllShareData().m_Common.m_sEdit.m_bConvertEOLPaste ){
		CLogicInt nConvertedTextLen = ConvertEol(szREPLACEKEY, nREPLACEKEY, NULL);
		wchar_t	*pszConvertedText = new wchar_t[nConvertedTextLen];
		ConvertEol(szREPLACEKEY, nREPLACEKEY, pszConvertedText);
		cmemReplacement.SetString(pszConvertedText, nConvertedTextLen);
		szREPLACEKEY = cmemReplacement.GetStringPtr(&nREPLACEKEY);
		delete [] pszConvertedText;
	}

	// 取得にステップがかかりそうな変数などを、一時変数化する。
	// とはいえ、これらの操作をすることによって得をするクロック数は合わせても 1 ループで数十だと思います。
	// 数百クロック毎ループのオーダーから考えてもそんなに得はしないように思いますけど・・・。
	BOOL &bCANCEL = cDlgCancel.m_bCANCEL;
	CDocLineMgr& rDocLineMgr = GetDocument()->m_cDocLineMgr;
	CLayoutMgr& rLayoutMgr = GetDocument()->m_cLayoutMgr;

	//$$ 単位混在
	CLayoutPoint ptOld(0, -1); // 検索後の選択範囲(xはいつもLogic。yは矩形はLayout,通常はLogic)
	/*CLogicInt*/int		lineCnt = 0;		//置換前の行数
	/*CLayoutInt*/int		linDif = (0);		//置換後の行調整
	CLogicXInt  colDif(0);     // 置換後の桁調整
	CLogicPoint boxRight;      // 矩形選択の現在の行の右端。sRangeA.GetTo().x ではなく boxRight.x + colDif を使う。
	/*CLogicInt*/int		linOldLen = (0);	//検査後の行の長さ

	int nLoopCnt = -1;
	CLogicRange cSelectLogic;	// 置換文字列GetSelect()のLogic単位版
	
	// partial 設定，block モード時は無効
	ESearchDirection SearchWordOpt = SEARCH_FORWARD;
	if( !bBeginBoxSelect ) SearchWordOpt = ( ESearchDirection )( SearchWordOpt | SEARCH_PARTIAL );
	
	// 置換ループ
	while( !bCANCEL ){	/* キャンセルされたか */
		
		// 次を検索，検索に引っかからなかったら終了
		if( CSearchAgent( &GetDocument()->m_cDocLineMgr ).SearchWord(
			GetCaret().GetCaretLogicPos(), SearchWordOpt,
			&cSelectLogic, m_pCommanderView->m_sSearchPattern
		) <= 0 ) break;
		
		// 選択
		if( bBeginBoxSelect || nPaste ){
			CLayoutRange cSelectLayout;
			rLayoutMgr.LogicToLayout( cSelectLogic, &cSelectLayout );
			
			GetCaret().MoveCursor( cSelectLayout.GetFrom(), false );
			GetCaret().m_nCaretPosX_Prev = cSelectLayout.GetFrom().GetX2();
			
			m_pCommanderView->GetSelectionInfo().SetSelectArea( cSelectLayout );
		}
		
		/* 処理中のユーザー操作を可能にする */
		if( !::BlockingHook( hwndCancel ) )
		{
			m_pCommanderView->SetDrawSwitch(bDrawSwitchOld);
			::EnableWindow( m_pCommanderView->GetHwnd(), TRUE );
			::EnableWindow( ::GetParent( m_pCommanderView->GetHwnd() ), TRUE );
			::EnableWindow( ::GetParent( ::GetParent( m_pCommanderView->GetHwnd() ) ), TRUE );
			return;// -1;
		}

		nLoopCnt++;
		// 128 ごとに表示。
		if( 0 == (nLoopCnt & 0x7F ) )
		// 時間ごとに進歩状況描画だと時間取得分遅くなると思うが、そちらの方が自然だと思うので・・・。
		// と思ったけど、逆にこちらの方が自然ではないので、やめる。
		{
			if( !bBeginBoxSelect ){
				int nDiff = nAllLineNumOrg - (Int)GetDocument()->m_cDocLineMgr.GetLineCount();
				if( 0 <= nDiff ){
					nNewPos = (nDiff + (Int)cSelectLogic.GetFrom().GetY2()) >> nShiftCount;
				}else{
					nNewPos = ::MulDiv((Int)cSelectLogic.GetFrom().GetY(), nAllLineNum, (Int)GetDocument()->m_cDocLineMgr.GetLineCount());
				}
			}else{
				int nDiff = nAllLineNumOrg - (Int)GetDocument()->m_cLayoutMgr.GetLineCount();
				if( 0 <= nDiff ){
					nNewPos = (nDiff + (Int)GetSelect().GetFrom().GetY2()) >> nShiftCount;
				}else{
					nNewPos = ::MulDiv((Int)GetSelect().GetFrom().GetY(), nAllLineNum, (Int)GetDocument()->m_cLayoutMgr.GetLineCount());
				}
			}
			if( nOldPos != nNewPos ){
				Progress_SetPos( hwndProgress, nNewPos +1 );
				Progress_SetPos( hwndProgress, nNewPos );
				nOldPos = nNewPos;
			}
			_itot( nReplaceNum, szLabel, 10 );
			::SendMessage( hwndStatic, WM_SETTEXT, 0, (LPARAM)szLabel );
		}

		// From Here 2001.12.03 hor
		/* 検索後の位置を確認 */
		if( bSelectedArea )
		{
			// 矩形選択
			//	o レイアウト座標をチェックしながら置換する
			//	o 折り返しがあると変になるかも・・・
			//
			if ( bBeginBoxSelect )
			{
				// 検索時の行数を記憶
				lineCnt = (Int)rLayoutMgr.GetLineCount();
				// 前回と今回の検索マッチ終端(ptOld, ptNew)と今回のマッチ先頭(ptNewFrom)
				CLayoutPoint ptNew     = GetSelect().GetTo();
				CLayoutPoint ptNewFrom = GetSelect().GetFrom();
				CLayoutInt   ptNewX    = ptNew.x; // 上書きされるので保存。
				{ // ptNew.x(ptOld.x)は特殊。
					CLogicPoint logicNew;
					rLayoutMgr.LayoutToLogic(ptNew, &logicNew);
					ptNew.x = (Int)(logicNew.x); // 2016.01.13 矩形でもxは必ずLogic
				}
				if (ptNew.y != ptOld.y) {
					colDif = (0); // リセット
					rLayoutMgr.LayoutToLogic(CLayoutPoint(sRangeA.GetTo().x, ptNew.y), &boxRight); // リセット
				}
				// 矩形範囲を通り過ぎた？
				if (sRangeA.GetTo().y + linDif < ptNew.y) {
					break; // 下へ抜けた。
				}
				if (sRangeA.GetTo().y + linDif == ptNew.y) {
					if (boxRight.x + colDif < (Int)ptNew.x) {
						break; // 最終行の右へ抜けた。
					}
				}
				/*
					矩形選択範囲の左端と文字境界(ロジック座標系)が一致しないときに
					検索開始位置が前に進まないことがある。具体的には次の3×3のテキス
					      トの2と3の桁を選択して「あ」を検索置換しようとした場合、
					123   「あ」の真ん中のレイアウト座標から検索を開始しようとして
					あ3   実際には「あ」の直前から検索が開始されるために、リトライ
					123   が堂々巡りする。レアケースなので事前に座標変換をして確か
					      めるのではなく、事後的に検索結果の一致から検知することにする。
				*/
				const CLayoutInt raggedLeftDiff = ptNew == ptOld ? ptNewFrom.x - sRangeA.GetFrom().x : CLayoutInt(0);
				// 桁は矩形範囲内？
				bool out = false; // とりあえず範囲内(仮)。
				/*
					＊検索で見つかった文字列は複数のレイアウト行に分かれている場合がある。
					＊文字列の先端と後端が矩形範囲に収まっているだけでなく、レイアウトさ
					  れた中間の文字列の文字部分(※)が矩形範囲に収まっていることを確かめ
					  なければいけない。
					  ※インデント部分が矩形範囲から外れているだけで範囲外とはしないよね？
					＊折り返し直前まで選択した場合は選択範囲が次行行頭(left=right=0;インデント))
					  を含む２レイアウト行にまたがることに注意が必要。
					  out = left < right && (...) というのがまさに対応を迫られた痕跡ですよ。
				*/
				const CLayoutInt firstLeft =  ptNewFrom.x - raggedLeftDiff;
				const CLogicInt  lastRight = (Int)ptNew.x - colDif;
				if (ptNewFrom.y == ptNew.y) { // 一番よくあるケースではレイアウトの取得・計算が不要。
					out = firstLeft < sRangeA.GetFrom().x || boxRight.x < lastRight;
				} else {
					for (CLayoutInt ll = ptNewFrom.y; ll <= ptNew.y; ++ll) { // ll = Layout Line
						const CLayout* pLayout = rLayoutMgr.SearchLineByLayoutY(ll);
						CLayoutInt  left = ll == ptNewFrom.y ? firstLeft : pLayout ? pLayout->GetIndent()                 : CLayoutInt(0);
						CLayoutInt right = ll == ptNew.y     ? ptNewX    : pLayout ? pLayout->CalcLayoutWidth(rLayoutMgr) : CLayoutInt(0);
						out = left < right && (left < sRangeA.GetFrom().x || sRangeA.GetTo().x < right);
						if (out) {
							break;
						}
					}
				}
				// Newは Oldになりました。
				ptOld = ptNew;

				if (out) {
					//次の検索開始位置へシフト
					m_pCommanderView->GetSelectionInfo().DisableSelectArea( false ); // 2016.01.13 範囲選択をクリアしないと位置移動できていなかった
					
					CLogicPoint cNextPoint;
					rLayoutMgr.LayoutToLogic(
						CLayoutPoint(
							sRangeA.GetFrom().x,
							ptNewFrom.y + CLayoutInt(firstLeft < sRangeA.GetFrom().x ? 0 : 1)
						), &cNextPoint
					);
					GetCaret().SetCaretLogicPos( cNextPoint );
					continue;
				}
			}
			// 普通の選択
			//	o 物理座標をチェックしながら置換する
			//
			else {
				// 検索時の行数を記憶
				lineCnt = rDocLineMgr.GetLineCount();

				// 検索後の範囲終端
				CLogicPoint ptOldTmp = cSelectLogic.GetTo();
				ptOld.x=(CLayoutInt)ptOldTmp.x; //$$ レイアウト型に無理やりロジック型を代入。気持ち悪い
				ptOld.y=(CLayoutInt)ptOldTmp.y;

				// 置換前の行の長さ(改行は１文字と数える)を保存しておいて、置換前後で行位置が変わった場合に使用
				linOldLen = rDocLineMgr.GetLine(ptOldTmp.GetY2())->GetLengthWithoutEOL() + CLogicInt(1);

				// 行は範囲内？
				// 2007.01.19 ryoji 条件追加: 選択終点が行頭(ptColLineP.x == 0)になっている場合は前の行の行末までを選択範囲とみなす
				// （選択始点が行頭ならその行頭は選択範囲に含み、終点が行頭ならその行頭は選択範囲に含まない、とする）
				// 論理的に少し変と指摘されるかもしれないが、実用上はそのようにしたほうが望ましいケースが多いと思われる。
				// ※行選択で行末までを選択範囲にしたつもりでも、UI上は次の行の行頭にカーソルが行く
				// ※終点の行頭を「^」にマッチさせたかったら１文字以上選択してね、ということで．．．
				// $$ 単位混在しまくりだけど、大丈夫？？
				if ((ptColLineP.y+linDif == (Int)ptOld.y && (ptColLineP.x+colDif < (Int)ptOld.x || ptColLineP.x == 0))
					|| ptColLineP.y+linDif < (Int)ptOld.y) {
					break;
				}
			}
		}
		
		bool bReZeroMatch = false;	// 正規表現 0 幅マッチした
		
		/* コマンドコードによる処理振り分け */
		/* テキストを貼り付け */
		if( nPaste ){
			if ( !bColumnSelect ){
				Command_INSTEXT( false, szREPLACEKEY, nREPLACEKEY, TRUE, bLineSelect );
			}else{
				Command_PASTEBOX(szREPLACEKEY, nREPLACEKEY);
			}
			++nReplaceNum;
		}
		// 正規表現による文字列置換，/g は使わずに 1個ずつ置換
		else if( bRegularExp ){
			CBregexp *pRegexp = m_pCommanderView->m_sSearchPattern.GetRegexp();
			
			int iRet = pRegexp->Replace( cmemReplacement.GetStringPtr());
			if( iRet > 0 ){
				Command_INSTEXT( false, pRegexp->GetString(), pRegexp->GetStringLen(), true, false, !bBeginBoxSelect, bBeginBoxSelect ? nullptr : &cSelectLogic );
				++nReplaceNum;
				
				// マッチ幅が 0 の場合，1文字選進める
				if( pRegexp->GetMatchLen() == 0 ) bReZeroMatch = true;
			}else if( iRet < 0 ){
				pRegexp->ShowErrorMsg( nullptr );
				break;
			}
		}else{
			Command_INSTEXT( false, szREPLACEKEY, nREPLACEKEY, true, false, !bBeginBoxSelect, bBeginBoxSelect ? nullptr : &cSelectLogic );
			++nReplaceNum;
		}

		/* 置換後の位置を確認 */
		if( bSelectedArea )
		{
			// 検索→置換の行補正値取得
			if( bBeginBoxSelect )
			{
				colDif += GetCaret().GetCaretLogicPos().x - Int(ptOld.x); // 矩形でもLogic
				linDif += (Int)(rLayoutMgr.GetLineCount() - lineCnt);
			}
			else{
				// 置換前の検索文字列の最終位置は ptOld
				// 置換後のカーソル位置
				CLogicPoint ptTmp2 = GetCaret().GetCaretLogicPos();
				int linDif_thistime = rDocLineMgr.GetLineCount() - lineCnt;	// 今回置換での行数変化
				linDif += linDif_thistime;
				if( ptColLineP.y + linDif == ptTmp2.y)
				{
					// 最終行で置換した時、又は、置換の結果、選択エリア最終行まで到達した時
					// 最終行なので、置換前後の文字数の増減で桁位置を調整する
					colDif += (Int)ptTmp2.GetX2() - (Int)ptOld.GetX2(); //$$ 単位混在

					// 但し、以下の場合は置換前後で行が異なってしまうので、行の長さで補正する必要がある
					// １）最終行直前で行連結が起こり、行が減っている場合（行連結なので、桁位置は置換後のカーソル桁位置分増加する）
					// 　　ptTmp2.x-ptOld.xだと、\r\n → "" 置換で行連結した場合に、桁位置が負になり失敗する（負とは前行の後ろの方になることなので補正する）
					// 　　今回置換での行数の変化(linDif_thistime)で、最終行が行連結されたかどうかを見ることにする
					// ２）改行を置換した（ptTmp2.y!=ptOld.y）場合、改行を置換すると置換後の桁位置が次行の桁位置になっているため
					//     ptTmp2.x-ptOld.xだと、負の数となり、\r\n → \n や \n → "abc" などで桁位置がずれる
					//     これも前行の長さで調整する必要がある
					if (linDif_thistime < 0 || ptTmp2.y != (Int)ptOld.y) { //$$ 単位混在
						colDif += linOldLen;
					}
				}
			}
		}
		
		// マッチ幅が 0 の場合，1文字選進める
		if( bReZeroMatch ){
			CLogicPoint Caret = GetCaret().GetCaretLogicPos();
			CDocLine *pDocLine = GetDocument()->m_cLayoutMgr.m_pcDocLineMgr->GetLine( Caret.GetY());
			
			if( Caret.GetX() < pDocLine->GetLengthWithoutEOL()){
				// hit 位置が改行より前なら，一文字右
				Caret.SetX( Caret.GetX() + 1 );
				Caret.SetY( Caret.GetY());
			}else if( pDocLine->GetNextLine()){
				// 次行があれば，次行先頭
				Caret.SetX( CLogicInt( 0 ));
				Caret.SetY( Caret.GetY() + 1 );
			}else{
				// 最終行の改行以後，break
				break;
			}
			GetCaret().SetCaretLogicPos( Caret );
		}
	}

	if( !bBeginBoxSelect && 0 < nReplaceNum ){
		// CLayoutMgrの更新(変更有の場合)
		rLayoutMgr._DoLayout(false);
		GetEditWindow()->ClearViewCaretPosInfo();
		if( GetDocument()->m_nTextWrapMethodCur == WRAP_NO_TEXT_WRAP ){
			rLayoutMgr.CalculateTextWidth();
		}
	}
	//>> 2002/03/26 Azumaiya

	_itot( nReplaceNum, szLabel, 10 );
	::SendMessage( hwndStatic, WM_SETTEXT, 0, (LPARAM)szLabel );

	if( !cDlgCancel.IsCanceled() ){
		nNewPos = nAllLineNum;
		Progress_SetPos( hwndProgress, nNewPos + 1 );
		Progress_SetPos( hwndProgress, nNewPos);
	}
	cDlgCancel.CloseDialog( 0 );
	::EnableWindow( m_pCommanderView->GetHwnd(), TRUE );
	::EnableWindow( ::GetParent( m_pCommanderView->GetHwnd() ), TRUE );
	::EnableWindow( ::GetParent( ::GetParent( m_pCommanderView->GetHwnd() ) ), TRUE );

	// From Here 2001.12.03 hor

	/* テキスト選択解除 */
	m_pCommanderView->GetSelectionInfo().DisableSelectArea( false );

	/* カーソル・選択範囲復元 */
	if((!bSelectedArea) ||			// ファイル全体置換
	   (cDlgCancel.IsCanceled())) {		// キャンセルされた
		// 画面位置復帰
		m_pCommanderView->SyncScrollV( m_pCommanderView->ScrollAtV( ViewTop ));
		Command_JUMPHIST_PREV();
	}
	else{
		if (bBeginBoxSelect) {
			// 矩形選択
			m_pCommanderView->GetSelectionInfo().SetBoxSelect(bBeginBoxSelect);
			sRangeA.GetToPointer()->y += linDif;
			if(sRangeA.GetTo().y<0)sRangeA.SetToY(CLayoutInt(0));
		}
		else{
			// 普通の選択
			ptColLineP.x+=colDif;
			if(ptColLineP.x<0)ptColLineP.x=0;
			ptColLineP.y+=linDif;
			if(ptColLineP.y<0)ptColLineP.y=0;
			GetDocument()->m_cLayoutMgr.LogicToLayout(
				ptColLineP,
				sRangeA.GetToPointer()
			);
		}
		if(sRangeA.GetFrom().y<sRangeA.GetTo().y || sRangeA.GetFrom().x<sRangeA.GetTo().x){
			m_pCommanderView->GetSelectionInfo().SetSelectArea( sRangeA );	// 2009.07.25 ryoji
		}
		GetCaret().MoveCursor( sRangeA.GetTo(), true );
		GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();	// 2009.07.25 ryoji
	}
	// To Here 2001.12.03 hor
	GetEditWindow()->m_cDlgReplace.m_bCanceled = (cDlgCancel.IsCanceled() != FALSE);
	GetEditWindow()->m_cDlgReplace.m_nReplaceCnt=nReplaceNum;
	m_pCommanderView->SetDrawSwitch(bDrawSwitchOld);
	ActivateFrameWindow( GetMainWindow() );
}

//検索マークの切替え	// 2001.12.03 hor クリア を 切替え に変更
void CViewCommander::Command_SEARCH_CLEARMARK( void )
{
// From Here 2001.12.03 hor

	//検索マークのセット

	if(m_pCommanderView->GetSelectionInfo().IsTextSelected()){

		// 検索文字列取得
		CNativeW	cmemCurText;
		m_pCommanderView->GetCurrentTextForSearch( cmemCurText, false );

		m_pCommanderView->m_strCurSearchKey = cmemCurText.GetStringPtr();
		if( m_pCommanderView->m_nCurSearchKeySequence < GetDllShareData().m_Common.m_sSearch.m_nSearchKeySequence ){
			m_pCommanderView->m_sCurSearchOption = GetDllShareData().m_Common.m_sSearch.m_sSearchOption;
		}
		m_pCommanderView->m_sCurSearchOption.bRegularExp = false;		//正規表現使わない
		m_pCommanderView->m_sCurSearchOption.bWordOnly = false;		//単語で検索しない

		// 共有データへ登録
		if( cmemCurText.GetStringLength() < _MAX_PATH ){
			CSearchKeywordManager().AddToSearchKeyArr( cmemCurText.GetStringPtr() );
			GetDllShareData().m_Common.m_sSearch.m_sSearchOption = m_pCommanderView->m_sCurSearchOption;
		}
		m_pCommanderView->m_nCurSearchKeySequence = GetDllShareData().m_Common.m_sSearch.m_nSearchKeySequence;
		m_pCommanderView->m_bCurSearchUpdate = true;

		m_pCommanderView->ChangeCurRegexp(false); // 2002.11.11 Moca 正規表現で検索した後，色分けができていなかった

		// 再描画
		m_pCommanderView->RedrawAll();
		return;
	}
// To Here 2001.12.03 hor

	//検索マークのクリア

	m_pCommanderView->m_bCurSrchKeyMark = false;	/* 検索文字列のマーク */
	/* フォーカス移動時の再描画 */
	m_pCommanderView->RedrawAll();
	return;
}

//	Jun. 16, 2000 genta
//	対括弧の検索
void CViewCommander::Command_BRACKETPAIR( void )
{
	CLayoutPoint ptColLine;
	//int nLine, nCol;

	int mode = 3;
	/*
	bit0(in)  : 表示領域外を調べるか？ 0:調べない  1:調べる
	bit1(in)  : 前方文字を調べるか？   0:調べない  1:調べる
	bit2(out) : 見つかった位置         0:後ろ      1:前
	*/
	if( m_pCommanderView->SearchBracket( GetCaret().GetCaretLayoutPos(), &ptColLine, &mode ) ){	// 02/09/18 ai
		//	2005.06.24 Moca
		//	2006.07.09 genta 表示更新漏れ：新規関数にて対応
		m_pCommanderView->MoveCursorSelecting( ptColLine, m_pCommanderView->GetSelectionInfo().m_bSelectingLock );
	}
	else{
		//	失敗した場合は nCol/nLineには有効な値が入っていない.
		//	何もしない
	}
}
