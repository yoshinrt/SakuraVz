/*!	@file
	@brief �e�L�X�g�X�^�b�N
	@author YoshiNRT
*/

#ifndef SAKURA_ENV_CTEXTSTACK_H_
#define SAKURA_ENV_CTEXTSTACK_H_

#include <list>
#include "mem/CNativeW.h"

#define TEXTSTACK_SIZE	( 32 * 1024 )	//!< �e�L�X�g�X�^�b�N�T�C�Y

/*!	@brief �e�L�X�g�X�^�b�N
 * �e�L�X�g�X�^�b�N�� TEXTSTACK_SIZE ���̃����O�o�b�t�@�Ƃ���
 * ���������D1 �v�f���͈ȉ��̒ʂ�D
 * - �f�[�^�T�C�Y (byte �P��)�Cint 3���̃T�C�Y�͊܂܂Ȃ�
 * - ���[�h�t���O
 * - �f�[�^ (int �P�ʂɐL�������)
 * - �f�[�^�T�C�Y (byte �P�ʁC�O�v�f�ւ̃|�C���^����Ɏg�p)
 * �o�b�t�@��[���o�b�t�@�O�[�͂Ȃ����Ă�����̂Ƃ��ď�������D
 * �܂� 1�v�f�̓o�b�t�@��[���o�b�t�@�O�[���܂����ő��݂�����D
 */
class CTextStack
{
public:
	//! �R���X�g���N�^
	CTextStack(){
		m_nTopPtr =
		m_nEndPtr =
		m_nSize	= 0;
	}
	
	//~CTextStack()
	
	/*! @brief �����Ŏw�肳�ꂽ�f�[�^���C�V���ɗ̈�m�ۂ��R�s�[����
	 * @param[in] cmemBuf	�X�^�b�N�ɐςރe�L�X�g
	 * @param[in] uMode		�I�����[�h (= m_nMode)
	 * @retval ����I������ true ��Ԃ��D
	 */
	bool Push( CNativeW* cmemBuf, UINT uMode );
	
	/*! @brief �e�L�X�g�X�^�b�N���� pop
	 * @param[in,out] cmemBuf	�X�^�b�N�ɐςރe�L�X�g
	 * @param[in,out] uMode		�I�����[�h (= m_nMode)
	 * @retval ����I������ true ��Ԃ��D
	 */
	bool Pop( CNativeW* cmemBuf, UINT* puMode, bool bNoPop = false );
	
	//! �v�f���擾
	int GetSize( void ){ return m_nSize; };
	
	enum {
		M_CHAR		= 0,	//<! �����P��
		M_COLUMN	= 1,	//<! ��`
		M_LINE		= 2,	//<! �s�P��
	};
	
private:
	//! �|�C���^���T�C�Y���i�߂�
	int Forward( int nPtr, int nSize ){
		if(( nPtr += nSize ) > TEXTSTACK_SIZE ) nPtr -= TEXTSTACK_SIZE;
		return nPtr;
	}
	
	//! �|�C���^���T�C�Y���߂�
	int Backward( int nPtr, int nSize ){
		if(( nPtr -= nSize ) < 0 ) nPtr += TEXTSTACK_SIZE;
		return nPtr;
	}
	
	//! �T�C�Y�� UINT �A���C�����g
	int AlignSize( int nSize ){
		return ( nSize + ( sizeof( int ) - 1 )) & ~( sizeof( int ) - 1 );
	}
	
	/*! @brief int �A�N�Z�X
	 * @param[in] nPtr �o�C�g�P�ʃ|�C���^�C�������v int �A���C�����g
	 * @retval int �ւ� ptr
	 */
	int* GetIntPtr( int nPtr ){
		return ( int *)(( BYTE *)m_nStack + nPtr );
	}
	
	int		m_nTopPtr;	//<! �擪�|�C���^ (�o�C�g�P��)
	int		m_nEndPtr;	//<! ��[�|�C���^ (�o�C�g�P��)
	int		m_nSize;	//<! �g�p�T�C�Y
	int		m_nStack[ TEXTSTACK_SIZE / sizeof( int )];	//<! �o�b�t�@�C�v int �A���C�����g
};

///////////////////////////////////////////////////////////////////////
#endif /* SAKURA_ENV_CTEXTSTACK_H_ */
