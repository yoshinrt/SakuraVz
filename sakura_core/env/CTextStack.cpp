/*!	@file
	@brief �e�L�X�g�X�^�b�N
	@author YoshiNRT
*/

#include "StdAfx.h"

bool CTextStack::Push( CNativeW* cmemBuf, UINT uMode ){
	return Push( cmemBuf->GetStringPtr(), ( int )cmemBuf->GetStringLength(), uMode );
}

bool CTextStack::Push( WCHAR *szText, int iLen, UINT uMode ){
	
	//DEBUG_TRACE( _T( ">>Push: t=%04X e=%04X s=%04X\n" ), m_nTopPtr, m_nEndPtr, m_nSize );
	
	// �f�[�^�� (�o�C�g�P��)
	int nSize		= iLen * sizeof( wchar_t );
	
	// buf ��Ő�L����f�[�^�� (�o�C�g�P��)
	int nSizeAlign	= AlignSize( nSize ) + sizeof( UINT ) * 3;
	
	// TEXTSTACK_SIZE �𒴂�����̂� push ���Ȃ�
	if( nSizeAlign > TEXTSTACK_SIZE ) return false;
	
	// push �̌��� TEXTSTACK_SIZE �𒴂���ꍇ�CTopPtr ��ύX�� buf ��؂�l�߂�
	while( nSizeAlign + m_nSize > TEXTSTACK_SIZE ){
		int nTmpSize = AlignSize( *GetIntPtr( m_nTopPtr )) + sizeof( int ) * 3;
		m_nSize -= nTmpSize;
		m_nTopPtr = Forward( m_nTopPtr, nTmpSize );
	}
	
	// �f�[�^���ݒ�
	*GetIntPtr( m_nEndPtr ) = nSize;
	m_nEndPtr = Forward( m_nEndPtr, sizeof( int ));
	
	// �t���O�ݒ�
	*GetIntPtr( m_nEndPtr ) = uMode;
	m_nEndPtr = Forward( m_nEndPtr, sizeof( int ));
	
	// �o�b�t�@�ɃR�s�[�C�o�b�t�@��[�ɂ�����ꍇ�́C2��ɕ�����
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
	
	// �f�[�^���ݒ�
	*GetIntPtr( m_nEndPtr ) = nSize;
	m_nEndPtr = Forward( m_nEndPtr, sizeof( int ));
	
	m_nSize += nSizeAlign;
	
	//DEBUG_TRACE( _T( "<<Push: t=%04X e=%04X s=%04X new=%04X\n" ), m_nTopPtr, m_nEndPtr, m_nSize, nSize );
	
	return true;
}

bool CTextStack::Pop( CNativeW* cmemBuf, UINT* puMode, bool bNoPop ){
	
	//DEBUG_TRACE( _T( ">>Pop: t=%04X e=%04X s=%04X\n" ), m_nTopPtr, m_nEndPtr, m_nSize );
	
	// �X�^�b�N����Ȃ烊�^�[��
	if( m_nSize == 0 ) return false;
	
	// �f�[�^���𓾂�
	int nEndPtr = m_nEndPtr;
	nEndPtr = Backward( nEndPtr, sizeof( int ));
	int nSize = *GetIntPtr( nEndPtr );
	
	// �f�[�^�擪�Ɉړ�
	nEndPtr = Backward( nEndPtr, AlignSize( nSize ));
	
	// �f�[�^�� buf ��[�ɂ��Ԃ��Ă���Ȃ�C2��ɕ����ăR�s�[
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
	
	// �t���O�ݒ�
	nEndPtr = Backward( nEndPtr, sizeof( int ));
	if( puMode ) *puMode = *GetIntPtr( nEndPtr );
	
	// �f�[�^�����߂�
	if( !bNoPop ){
		m_nEndPtr = Backward( nEndPtr, sizeof( int ));
		m_nSize -= AlignSize( nSize ) + sizeof( UINT ) * 3;
	}
	
	//DEBUG_TRACE( _T( "<<Pop: t=%04X e=%04X s=%04X new=%04X\n" ), m_nTopPtr, m_nEndPtr, m_nSize, nSize );
	
	return true;
}
