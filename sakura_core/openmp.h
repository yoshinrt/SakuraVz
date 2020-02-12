#pragma once

#ifdef _OPENMP
	#include <omp.h>
	#define OmpMaxThreadNum		omp_get_max_threads()
	#define OmpThreadId			omp_get_thread_num()
	
	class COmpThreadNum {
	public:
		COmpThreadNum( UINT uNum ){
			m_uPrevThreadNum = omp_get_max_threads();
			omp_set_num_threads( uNum );
		}
		
		~COmpThreadNum(){
			omp_set_num_threads( m_uPrevThreadNum );
		}
	private:
		UINT m_uPrevThreadNum;
	};
	
#else
	#define OmpMaxThreadNum		1
	#define OmpThreadId			0
	
	class COmpThreadNum {
	public:
		COmpThreadNum( UINT uNum ){}
	};
#endif
