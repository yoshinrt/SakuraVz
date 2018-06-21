/*!	@file
	@brief テキストスタック
	@author YoshiNRT
*/

#include "StdAfx.h"

bool CTextStack::Push( CNativeW* cmemBuf, UINT uMode ){
	return Push( cmemBuf->GetStringPtr(), ( int )cmemBuf->GetStringLength(), uMode );
}

bool CTextStack::Push( WCHAR *szText, int iLen, UINT uMode ){
	
	//DEBUG_TRACE( _T( ">>Push: t=%04X e=%04X s=%04X\n" ), m_nTopPtr, m_nEndPtr, m_nSize );
	
	// データ長 (バイト単位)
	int nSize		= iLen * sizeof( wchar_t );
	
	// buf 上で占有するデータ長 (バイト単位)
	int nSizeAlign	= AlignSize( nSize ) + sizeof( UINT ) * 3;
	
	// TEXTSTACK_SIZE を超えるものは push しない
	if( nSizeAlign > TEXTSTACK_SIZE ) return false;
	
	// push の結果 TEXTSTACK_SIZE を超える場合，TopPtr を変更し buf を切り詰める
	while( nSizeAlign + m_nSize > TEXTSTACK_SIZE ){
		int nTmpSize = AlignSize( *GetIntPtr( m_nTopPtr )) + sizeof( int ) * 3;
		m_nSize -= nTmpSize;
		m_nTopPtr = Forward( m_nTopPtr, nTmpSize );
	}
	
	// データ長設定
	*GetIntPtr( m_nEndPtr ) = nSize;
	m_nEndPtr = Forward( m_nEndPtr, sizeof( int ));
	
	// フラグ設定
	*GetIntPtr( m_nEndPtr ) = uMode;
	m_nEndPtr = Forward( m_nEndPtr, sizeof( int ));
	
	// バッファにコピー，バッファ後端にかかる場合は，2回に分ける
	if( m_nEndPtr + nSize > TEXTSTACK_SIZE ){
		memcpy( GetIntPtr( m_nEndPtr ), szText, TEXTSTACK_SIZE - m_nEndPtr );
		memcpy(
			GetIntPtr( 0 ),
			( BYTE *)szText + ( TEXTSTACK_SIZE - m_nEndPtr ),
			nSize - ( TEXTSTACK_SIZE - m_nEndPtr )
		);
		
	}else{
		memcpy(( wchar_t *)GetIntPtr( m_nEndPtr ), szText, nSize );
	}
	m_nEndPtr = Forward( m_nEndPtr, AlignSize( nSize ));
	
	// データ長設定
	*GetIntPtr( m_nEndPtr ) = nSize;
	m_nEndPtr = Forward( m_nEndPtr, sizeof( int ));
	
	m_nSize += nSizeAlign;
	
	//DEBUG_TRACE( _T( "<<Push: t=%04X e=%04X s=%04X new=%04X\n" ), m_nTopPtr, m_nEndPtr, m_nSize, nSize );
	
	return true;
}

bool CTextStack::Pop( CNativeW* cmemBuf, UINT* puMode, bool bNoPop ){
	
	//DEBUG_TRACE( _T( ">>Pop: t=%04X e=%04X s=%04X\n" ), m_nTopPtr, m_nEndPtr, m_nSize );
	
	// スタックが空ならリターン
	if( m_nSize == 0 ) return false;
	
	// データ長を得る
	int nEndPtr = m_nEndPtr;
	nEndPtr = Backward( nEndPtr, sizeof( int ));
	int nSize = *GetIntPtr( nEndPtr );
	
	// データ先頭に移動
	nEndPtr = Backward( nEndPtr, AlignSize( nSize ));
	
	// データが buf 後端にかぶっているなら，2回に分けてコピー
	if( cmemBuf ){
		if( nEndPtr + nSize > TEXTSTACK_SIZE ){
			BYTE *pBuf = new BYTE[ nSize ];
			
			memcpy( pBuf, GetIntPtr( nEndPtr ), TEXTSTACK_SIZE - nEndPtr );
			memcpy(
				pBuf + ( TEXTSTACK_SIZE - nEndPtr ),
				GetIntPtr( 0 ),
				nSize - ( TEXTSTACK_SIZE - nEndPtr )
			);
			
			cmemBuf->SetString(( wchar_t *)pBuf, nSize / sizeof( wchar_t ));
			
			delete [] pBuf;
		}else{
			cmemBuf->SetString(( wchar_t *)GetIntPtr( nEndPtr ), nSize / sizeof( wchar_t ));
		}
	}
	
	// フラグ設定
	nEndPtr = Backward( nEndPtr, sizeof( int ));
	if( puMode ) *puMode = *GetIntPtr( nEndPtr );
	
	// データ長分戻す
	if( !bNoPop ){
		m_nEndPtr = Backward( nEndPtr, sizeof( int ));
		m_nSize -= AlignSize( nSize ) + sizeof( UINT ) * 3;
	}
	
	//DEBUG_TRACE( _T( "<<Pop: t=%04X e=%04X s=%04X new=%04X\n" ), m_nTopPtr, m_nEndPtr, m_nSize, nSize );
	
	return true;
}
